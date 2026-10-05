#include "CodeAnimWebGraphs.h"

#include "CodeAnimationWeb.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "CodeAnimWebGraphs"

namespace CodeAnimWebGraphs
{
	static const FText GraphCategory = LOCTEXT("GraphCategory", "Code Animation Web");

	/** Text safe as part of a function name. */
	static FString Sanitize(const FString& In)
	{
		FString Out;
		for (const TCHAR Char : In)
		{
			Out.AppendChar(FChar::IsAlnum(Char) ? Char : TEXT('_'));
		}
		return Out.IsEmpty() ? TEXT("Unnamed") : Out;
	}

	static FText StateName(const UCodeAnimationWeb* Defaults, FName Key)
	{
		TArray<TPair<FName, FText>> States;
		Defaults->GetStateList(States);
		for (const TPair<FName, FText>& State : States)
		{
			if (State.Key == Key)
			{
				return State.Value;
			}
		}
		return FText::FromName(Key);
	}

	/** A new function graph whose entry has the given float inputs, in the order the Web fills them. */
	static UEdGraph* CreateGraph(UBlueprint* Blueprint, const FString& BaseName, TConstArrayView<FName> Inputs, const FText& Tooltip)
	{
		const FName Name = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, BaseName);
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, /*bIsUserCreated*/ true, nullptr);

		TArray<UK2Node_FunctionEntry*> Entries;
		Graph->GetNodesOfClass(Entries);
		if (Entries.Num() > 0)
		{
			UK2Node_FunctionEntry* Entry = Entries[0];
			FEdGraphPinType Real;
			Real.PinCategory = UEdGraphSchema_K2::PC_Real;
			Real.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			for (const FName& Input : Inputs)
			{
				Entry->CreateUserDefinedPin(Input, Real, EGPD_Output, /*bUseUniqueName*/ false);
			}
			Entry->MetaData.Category = GraphCategory;
			Entry->MetaData.ToolTip = Tooltip;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return Graph;
	}

	static void Open(UEdGraph* Graph)
	{
		if (Graph)
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Graph);
		}
	}

	/** The Web's class defaults, fetched fresh: a compile can replace them. */
	static UCodeAnimationWeb* DefaultsOf(const UBlueprint* Blueprint)
	{
		return Blueprint && Blueprint->GeneratedClass
			? Cast<UCodeAnimationWeb>(Blueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
	}
}

UEdGraph* CodeAnimWebGraphs::FindGraph(const UBlueprint* Blueprint, const FGuid& Guid)
{
	if (!Blueprint || !Guid.IsValid())
	{
		return nullptr;
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GraphGuid == Guid)
		{
			return Graph;
		}
	}
	return nullptr;
}

FText CodeAnimWebGraphs::ButtonLabel(const UBlueprint* Blueprint, const FGuid& Guid)
{
	return FindGraph(Blueprint, Guid) ? LOCTEXT("OpenGraph", "Open Graph") : LOCTEXT("AddGraph", "+ Graph");
}

UEdGraph* CodeAnimWebGraphs::OpenOrCreateStateGraph(UCodeAnimationWeb* Defaults, FName StateKey, bool bOpen)
{
	UBlueprint* Blueprint = Defaults ? Defaults->GetWebBlueprint() : nullptr;
	FCodeAnimWebStateEntry* Entry = Blueprint
		? Defaults->States.FindByPredicate([StateKey](const FCodeAnimWebStateEntry& E) { return E.Key == StateKey; })
		: nullptr;
	if (!Entry)
	{
		return nullptr;
	}
	if (UEdGraph* Existing = FindGraph(Blueprint, Entry->GraphGuid))
	{
		if (bOpen)
		{
			Open(Existing);
		}
		return Existing;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddStateGraph", "Add State Graph"));
	Blueprint->Modify();
	UEdGraph* Graph = CreateGraph(Blueprint, TEXT("State_") + Sanitize(StateName(Defaults, StateKey).ToString()),
		{ CodeAnimWeb::TimeInStateParam },
		LOCTEXT("StateGraphTooltip",
			"Runs every frame this state is in play, after its State Values are applied. Set outputs from TimeInState "
			"(seconds since the Web headed for this state); any output left alone keeps its State Value."));

	if (UCodeAnimationWeb* Fresh = DefaultsOf(Blueprint))
	{
		if (FCodeAnimWebStateEntry* FreshEntry = Fresh->States.FindByPredicate(
			[StateKey](const FCodeAnimWebStateEntry& E) { return E.Key == StateKey; }))
		{
			Fresh->Modify();
			FreshEntry->GraphGuid = Graph->GraphGuid;
			FreshEntry->GraphFunction = Graph->GetFName();
		}
	}
	if (bOpen)
	{
		Open(Graph);
	}
	return Graph;
}

UEdGraph* CodeAnimWebGraphs::OpenOrCreateTransitionGraph(UCodeAnimationWeb* Defaults, int32 TransitionIndex, bool bOpen)
{
	UBlueprint* Blueprint = Defaults ? Defaults->GetWebBlueprint() : nullptr;
	if (!Blueprint || !Defaults->Transitions.IsValidIndex(TransitionIndex))
	{
		return nullptr;
	}
	const FCodeAnimWebTransition& Transition = Defaults->Transitions[TransitionIndex];
	if (UEdGraph* Existing = FindGraph(Blueprint, Transition.GraphGuid))
	{
		if (bOpen)
		{
			Open(Existing);
		}
		return Existing;
	}

	const FString Name = FString::Printf(TEXT("Transition_%s_%s_%s"),
		*Sanitize(StateName(Defaults, Transition.From.Key).ToString()),
		Transition.bTwoWay ? TEXT("And") : TEXT("To"),
		*Sanitize(StateName(Defaults, Transition.To.Key).ToString()));

	const FScopedTransaction Transaction(LOCTEXT("AddTransitionGraph", "Add Transition Graph"));
	Blueprint->Modify();
	UEdGraph* Graph = CreateGraph(Blueprint, Name, { CodeAnimWeb::AlphaParam, CodeAnimWeb::ProgressParam },
		LOCTEXT("TransitionGraphTooltip",
			"Runs every frame of this transition, after the automatic blend. Alpha is eased, Progress linear, both 0-1; "
			"use Get From Output / Get To Output for the two ends. Outputs left alone keep the automatic blend. "
			"A two-way transition played backwards runs this with the ends swapped and alpha reversed."));

	if (UCodeAnimationWeb* Fresh = DefaultsOf(Blueprint))
	{
		if (Fresh->Transitions.IsValidIndex(TransitionIndex))
		{
			Fresh->Modify();
			Fresh->Transitions[TransitionIndex].GraphGuid = Graph->GraphGuid;
			Fresh->Transitions[TransitionIndex].GraphFunction = Graph->GetFName();
		}
	}
	if (bOpen)
	{
		Open(Graph);
	}
	return Graph;
}

FGuid CodeAnimWebGraphs::FindCustomLerpGraph(UBlueprint* Blueprint, FName OutputName)
{
	const UCodeAnimationWeb* Defaults = DefaultsOf(Blueprint);
	const FGuid Output = Blueprint ? FBlueprintEditorUtils::FindMemberVariableGuidByName(Blueprint, OutputName) : FGuid();
	const FCodeAnimCustomLerp* Lerp = Defaults
		? Defaults->CustomLerps.FindByPredicate([Output](const FCodeAnimCustomLerp& L) { return L.Output == Output; })
		: nullptr;
	return Lerp ? Lerp->GraphGuid : FGuid();
}

UEdGraph* CodeAnimWebGraphs::OpenOrCreateCustomLerpGraph(UBlueprint* Blueprint, FName OutputName, bool bOpen)
{
	UCodeAnimationWeb* Defaults = DefaultsOf(Blueprint);
	const FGuid Output = Blueprint ? FBlueprintEditorUtils::FindMemberVariableGuidByName(Blueprint, OutputName) : FGuid();
	if (!Defaults || !Output.IsValid())
	{
		return nullptr;
	}
	if (UEdGraph* Existing = FindGraph(Blueprint, FindCustomLerpGraph(Blueprint, OutputName)))
	{
		if (bOpen)
		{
			Open(Existing);
		}
		return Existing;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddCustomLerpGraph", "Add Custom Lerp Graph"));
	Blueprint->Modify();
	UEdGraph* Graph = CreateGraph(Blueprint, TEXT("Lerp_") + Sanitize(OutputName.ToString()),
		{ CodeAnimWeb::AlphaParam, CodeAnimWeb::ProgressParam },
		FText::Format(LOCTEXT("LerpGraphTooltip",
			"Runs every frame of every transition, after the automatic blend and any transition graph, to decide {0}. "
			"Alpha is eased, Progress linear, both 0-1; use Get From Output / Get To Output for the two ends. "
			"It may set other outputs too - a mesh swap driving a scale, say."), FText::FromName(OutputName)));

	if (UCodeAnimationWeb* Fresh = DefaultsOf(Blueprint))
	{
		Fresh->Modify();
		FCodeAnimCustomLerp* Lerp = Fresh->CustomLerps.FindByPredicate([Output](const FCodeAnimCustomLerp& L) { return L.Output == Output; });
		if (!Lerp)
		{
			Lerp = &Fresh->CustomLerps.AddDefaulted_GetRef();
			Lerp->Output = Output;
		}
		Lerp->GraphGuid = Graph->GraphGuid;
		Lerp->GraphFunction = Graph->GetFName();
	}
	if (bOpen)
	{
		Open(Graph);
	}
	return Graph;
}

#undef LOCTEXT_NAMESPACE
