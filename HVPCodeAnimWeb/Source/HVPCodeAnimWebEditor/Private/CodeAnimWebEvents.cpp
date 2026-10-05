#include "CodeAnimWebEvents.h"

#include "BlueprintEditorLibrary.h"
#include "CodeAnimationWeb.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "CodeAnimWebEvents"

namespace CodeAnimWebEvents
{
	static FString Compact(FName Name)
	{
		return Name.ToString().Replace(TEXT(" "), TEXT(""));
	}

	/** The signature an output's dispatcher should have: the Web as its own class, then the new value. */
	static TArray<TPair<FName, FEdGraphPinType>> SignatureFor(const UBlueprint* Blueprint, const FBPVariableDescription& Output)
	{
		FEdGraphPinType WebType;
		WebType.PinCategory = UEdGraphSchema_K2::PC_Object;
		WebType.PinSubCategoryObject = Blueprint->GeneratedClass;
		return {
			{ TEXT("Web"), WebType },
			{ FName(TEXT("New") + Compact(Output.VarName)), Output.VarType },
		};
	}

	/** On Outputs Changed's signature: the Web as its own class, then the names of the outputs that changed. */
	static TArray<TPair<FName, FEdGraphPinType>> AllOutputsSignatureFor(const UBlueprint* Blueprint)
	{
		FEdGraphPinType WebType;
		WebType.PinCategory = UEdGraphSchema_K2::PC_Object;
		WebType.PinSubCategoryObject = Blueprint->GeneratedClass;
		FEdGraphPinType Names;
		Names.PinCategory = UEdGraphSchema_K2::PC_Name;
		Names.ContainerType = EPinContainerType::Array;
		return {
			{ TEXT("Web"), WebType },
			{ TEXT("ChangedOutputs"), Names },
		};
	}

	/** Rewrites a dispatcher's parameters if they are not exactly Params. True if it did. */
	static bool EnsureSignature(UBlueprint* Blueprint, FName EventName, const TArray<TPair<FName, FEdGraphPinType>>& Params)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::GetDelegateSignatureGraphByName(Blueprint, EventName);
		TArray<UK2Node_FunctionEntry*> Entries;
		if (Graph)
		{
			Graph->GetNodesOfClass(Entries);
		}
		if (Entries.Num() == 0)
		{
			return false;
		}

		UK2Node_FunctionEntry* Entry = Entries[0];
		bool bMatches = Entry->UserDefinedPins.Num() == Params.Num();
		for (int32 Index = 0; bMatches && Index < Params.Num(); ++Index)
		{
			const TSharedPtr<FUserPinInfo>& Pin = Entry->UserDefinedPins[Index];
			bMatches = Pin.IsValid() && Pin->PinName == Params[Index].Key && Pin->PinType == Params[Index].Value;
		}
		if (bMatches)
		{
			return false;
		}

		Entry->Modify();
		while (Entry->UserDefinedPins.Num() > 0)
		{
			Entry->RemoveUserDefinedPin(Entry->UserDefinedPins[0]);
		}
		for (const TPair<FName, FEdGraphPinType>& Param : Params)
		{
			Entry->CreateUserDefinedPin(Param.Key, Param.Value, EGPD_Output, /*bUseUniqueName*/ false);
		}
		return true;
	}
}

FName CodeAnimWebEvents::EventNameFor(FName OutputName)
{
	return FName(TEXT("On") + Compact(OutputName) + TEXT("Changed"));
}

bool CodeAnimWebEvents::Reconcile(UBlueprint* Blueprint)
{
	const UClass* Parent = Blueprint ? Blueprint->ParentClass.Get() : nullptr;
	if (!Parent || !Parent->IsChildOf(UCodeAnimationWeb::StaticClass()) || !Blueprint->GeneratedClass)
	{
		// Not a Web, or never compiled: the Web parameter needs the generated class to name.
		return false;
	}

	// Outputs and existing dispatchers, copied: adding and renaming variables reallocates NewVariables.
	TArray<FBPVariableDescription> Outputs;
	TMap<FString, FName> EventByOutput;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			if (Variable.HasMetaData(CodeAnimWeb::EventMetaKey))
			{
				EventByOutput.Add(Variable.GetMetaData(CodeAnimWeb::EventMetaKey), Variable.VarName);
			}
		}
		else if (Variable.HasMetaData(CodeAnimWeb::OutputMetaKey))
		{
			Outputs.Add(Variable);
		}
	}

	bool bChanged = false;

	// Dispatchers whose output is gone or no longer an output.
	for (const TPair<FString, FName>& Event : EventByOutput)
	{
		if (Event.Key == CodeAnimWeb::AllOutputsEventKey)
		{
			continue;
		}
		const bool bLive = Outputs.ContainsByPredicate(
			[&Event](const FBPVariableDescription& Output) { return Output.VarGuid.ToString() == Event.Key; });
		if (!bLive)
		{
			UBlueprintEditorLibrary::RemoveEventDispatcher(Blueprint, Event.Value);
			bChanged = true;
		}
	}

	for (const FBPVariableDescription& Output : Outputs)
	{
		const FString OwnerKey = Output.VarGuid.ToString();
		const FName Wanted = EventNameFor(Output.VarName);
		FName EventName;

		if (const FName* Existing = EventByOutput.Find(OwnerKey))
		{
			EventName = *Existing;
			// The output was renamed: follow it, if the new name is free.
			if (EventName != Wanted && FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, Wanted.ToString()) == Wanted)
			{
				FBlueprintEditorUtils::RenameMemberVariable(Blueprint, EventName, Wanted);
				EventName = Wanted;
				bChanged = true;
			}
		}
		else
		{
			EventName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, Wanted.ToString());
			if (!UBlueprintEditorLibrary::AddEventDispatcher(Blueprint, EventName))
			{
				continue;
			}
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, EventName, nullptr, CodeAnimWeb::EventMetaKey, OwnerKey);
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, EventName, nullptr, FBlueprintMetadata::MD_Tooltip,
				FText::Format(LOCTEXT("EventTooltip", "Fired whenever {0} changes, with the Web and the new value. Made by the Web for its "
					"Animation Output; it follows the output's name and type, and goes if the output does."),
					FText::FromName(Output.VarName)).ToString());
			FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, EventName, nullptr,
				LOCTEXT("EventCategory", "Animation Outputs"), /*bDontRecompile*/ true);
			bChanged = true;
		}

		bChanged |= EnsureSignature(Blueprint, EventName, SignatureFor(Blueprint, Output));
	}

	// On Outputs Changed: every Web has one, outputs or not, so it can be bound before any are added.
	{
		FName EventName;
		if (const FName* Existing = EventByOutput.Find(CodeAnimWeb::AllOutputsEventKey))
		{
			EventName = *Existing;
		}
		else
		{
			EventName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, OutputsChangedEventName.ToString());
			if (UBlueprintEditorLibrary::AddEventDispatcher(Blueprint, EventName))
			{
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, EventName, nullptr, CodeAnimWeb::EventMetaKey,
					CodeAnimWeb::AllOutputsEventKey);
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, EventName, nullptr, FBlueprintMetadata::MD_Tooltip,
					LOCTEXT("AllEventTooltip", "Fired at most once per update, after every output is written, with the Web and the "
						"names of the outputs that changed. For re-applying the Web as a whole; each output also has its own "
						"On <Output> Changed. Made by the Web.").ToString());
				FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, EventName, nullptr,
					LOCTEXT("EventCategory", "Animation Outputs"), /*bDontRecompile*/ true);
				bChanged = true;
			}
			else
			{
				EventName = NAME_None;
			}
		}
		if (!EventName.IsNone())
		{
			bChanged |= EnsureSignature(Blueprint, EventName, AllOutputsSignatureFor(Blueprint));
		}
	}

	if (bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return bChanged;
}

#undef LOCTEXT_NAMESPACE
