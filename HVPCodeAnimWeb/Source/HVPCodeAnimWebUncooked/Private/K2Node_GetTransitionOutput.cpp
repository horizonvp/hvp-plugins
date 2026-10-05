#include "K2Node_GetTransitionOutput.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "CodeAnimationWeb.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "KismetCompiler.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_GetTransitionOutput"

const FName UK2Node_GetTransitionOutput::ValuePinName(TEXT("Value"));

UClass* UK2Node_GetTransitionOutput::GetWebClass() const
{
	// The skeleton class: always current with the Blueprint's variables, compiled or not.
	const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(this);
	UClass* Class = Blueprint ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
	if (!Class && Blueprint)
	{
		Class = Blueprint->GeneratedClass.Get();
	}
	return Class && Class->IsChildOf(UCodeAnimationWeb::StaticClass()) ? Class : nullptr;
}

void UK2Node_GetTransitionOutput::AllocateDefaultPins()
{
	UEdGraphPin* OutputPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, CodeAnimWebNodes::OutputPinName);
	OutputPin->bNotConnectable = true;
	OutputPin->DefaultValue = Output.Name.IsNone() ? FString() : Output.Name.ToString();
	OutputPin->PinToolTip = LOCTEXT("OutputTooltip", "The output to read.").ToString();

	CreateValuePin();
	Super::AllocateDefaultPins();
}

void UK2Node_GetTransitionOutput::CreateValuePin()
{
	if (Output.bHasValue)
	{
		UEdGraphPin* Value = CreatePin(EGPD_Output, Output.ValueType, ValuePinName);
		Value->PinFriendlyName = FText::FromName(Output.Name);
	}
}

void UK2Node_GetTransitionOutput::RefreshValuePin()
{
	const bool bChanged = CodeAnimWebNodes::RefreshType(GetWebClass(), Output);
	UEdGraphPin* Existing = FindPin(ValuePinName);
	if (!bChanged && (Existing != nullptr) == Output.bHasValue)
	{
		if (Existing)
		{
			Existing->PinFriendlyName = FText::FromName(Output.Name);
		}
		return;
	}

	Modify();
	if (Existing)
	{
		Existing->BreakAllPinLinks();
		RemovePin(Existing);
	}
	CreateValuePin();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

void UK2Node_GetTransitionOutput::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	if (Pin && Pin->PinName == CodeAnimWebNodes::OutputPinName)
	{
		Modify();
		Output.Name = Pin->DefaultValue.IsEmpty() ? NAME_None : FName(*Pin->DefaultValue);
		Output.Guid.Invalidate();
		RefreshValuePin();
	}
}

void UK2Node_GetTransitionOutput::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	CodeAnimWebNodes::RefreshType(GetWebClass(), Output);
	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UK2Node_GetTransitionOutput::PostReconstructNode()
{
	Super::PostReconstructNode();
	if (UEdGraphPin* OutputPin = FindPin(CodeAnimWebNodes::OutputPinName))
	{
		OutputPin->DefaultValue = Output.Name.IsNone() ? FString() : Output.Name.ToString();
	}
}

FText UK2Node_GetTransitionOutput::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::MenuTitle || Output.Name.IsNone())
	{
		return bTo ? LOCTEXT("ToMenu", "Get To Output") : LOCTEXT("FromMenu", "Get From Output");
	}
	return FText::Format(bTo ? LOCTEXT("ToTitle", "To {0}") : LOCTEXT("FromTitle", "From {0}"), FText::FromName(Output.Name));
}

FText UK2Node_GetTransitionOutput::GetTooltipText() const
{
	return bTo
		? LOCTEXT("ToTooltip", "In a transition or Custom Lerp graph: the output's value in the state being transitioned to.")
		: LOCTEXT("FromTooltip", "In a transition or Custom Lerp graph: the output's value where the transition started from - live, if that was itself moving.");
}

FSlateIcon UK2Node_GetTransitionOutput::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), TEXT("Kismet.AllClasses.FunctionIcon"));
	return Icon;
}

bool UK2Node_GetTransitionOutput::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	const UClass* Parent = Blueprint ? Blueprint->ParentClass.Get() : nullptr;
	return Parent && Parent->IsChildOf(UCodeAnimationWeb::StaticClass()) && Super::IsCompatibleWithGraph(TargetGraph);
}

void UK2Node_GetTransitionOutput::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}
	for (const bool bToEnd : { false, true })
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionKey);
		check(Spawner);
		Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
			[bToEnd](UEdGraphNode* Node, bool /*bIsTemplateNode*/) { CastChecked<UK2Node_GetTransitionOutput>(Node)->bTo = bToEnd; });
		Spawner->DefaultMenuSignature.MenuName = bToEnd ? LOCTEXT("ToMenu", "Get To Output") : LOCTEXT("FromMenu", "Get From Output");
		Spawner->DefaultMenuSignature.Tooltip = bToEnd
			? LOCTEXT("ToTooltip", "In a transition or Custom Lerp graph: the output's value in the state being transitioned to.")
			: LOCTEXT("FromTooltip", "In a transition or Custom Lerp graph: the output's value where the transition started from - live, if that was itself moving.");
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UK2Node_GetTransitionOutput::GetMenuCategory() const
{
	return LOCTEXT("Category", "Code Animation Web");
}

FText UK2Node_GetTransitionOutput::GetKeywords() const
{
	return LOCTEXT("Keywords", "code animation web transition from to output lerp");
}

void UK2Node_GetTransitionOutput::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);
	FCodeAnimOutputSelection Selection = Output;
	if (!CodeAnimWebNodes::Resolve(GetWebClass(), Selection))
	{
		MessageLog.Error(*FText::Format(LOCTEXT("NoOutput", "@@: '{0}' is not one of this Web's Animation Outputs."),
			FText::FromName(Output.Name)).ToString(), this);
	}
}

void UK2Node_GetTransitionOutput::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	FCodeAnimOutputSelection Selection = Output;
	const FProperty* OutputProperty = CodeAnimWebNodes::Resolve(GetWebClass(), Selection);
	UEdGraphPin* Value = FindPin(ValuePinName);
	if (!OutputProperty || !Value)
	{
		BreakAllNodeLinks();
		return;
	}

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();
	UK2Node_CallFunction* Read = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	Read->FunctionReference.SetExternalMember(
		GET_FUNCTION_NAME_CHECKED(UCodeAnimationWeb, GetTransitionEndpoint), UCodeAnimationWeb::StaticClass());
	Read->AllocateDefaultPins();
	Schema->TrySetDefaultValue(*Read->FindPinChecked(TEXT("Output")), OutputProperty->GetName());
	Schema->TrySetDefaultValue(*Read->FindPinChecked(TEXT("bTo")), bTo ? TEXT("true") : TEXT("false"));
	UEdGraphPin* ReadValue = Read->FindPinChecked(TEXT("Value"));
	ReadValue->PinType = Output.ValueType;
	CompilerContext.MovePinLinksToIntermediate(*Value, *ReadValue);

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
