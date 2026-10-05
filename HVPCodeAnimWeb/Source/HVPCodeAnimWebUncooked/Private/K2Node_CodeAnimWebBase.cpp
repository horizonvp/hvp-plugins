#include "K2Node_CodeAnimWebBase.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "CodeAnimationWeb.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_GetTransitionOutput.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_CodeAnimWebBase"

const FName UK2Node_CodeAnimWebBase::WebPinName(UEdGraphSchema_K2::PN_Self);
const FName CodeAnimWebNodes::OutputPinName(TEXT("Output"));

// ---------------------------------------------------------------------------
// Output selection
// ---------------------------------------------------------------------------

void CodeAnimWebNodes::GetOutputs(const UClass* WebClass, TArray<const FProperty*>& OutOutputs)
{
	OutOutputs.Reset();
#if WITH_EDITORONLY_DATA
	if (!WebClass)
	{
		return;
	}
	for (TFieldIterator<FProperty> It(WebClass); It; ++It)
	{
		if (It->HasMetaData(CodeAnimWeb::OutputMetaKey))
		{
			OutOutputs.Add(*It);
		}
	}
#endif
}

const FProperty* CodeAnimWebNodes::Resolve(const UClass* WebClass, FCodeAnimOutputSelection& Selection)
{
	if (!WebClass || (Selection.Name.IsNone() && !Selection.Guid.IsValid()))
	{
		return nullptr;
	}

	TArray<const FProperty*> Outputs;
	GetOutputs(WebClass, Outputs);
	for (const FProperty* Output : Outputs)
	{
		if (Output->GetFName() == Selection.Name)
		{
			const FGuid Guid = WebClass->FindPropertyGuidFromName(Selection.Name);
			if (Guid.IsValid())
			{
				Selection.Guid = Guid;
			}
			return Output;
		}
	}

	// Renamed since: the GUID still finds it.
	if (Selection.Guid.IsValid())
	{
		const FName Renamed = WebClass->FindPropertyNameFromGuid(Selection.Guid);
		for (const FProperty* Output : Outputs)
		{
			if (Output->GetFName() == Renamed)
			{
				Selection.Name = Renamed;
				return Output;
			}
		}
	}
	return nullptr;
}

bool CodeAnimWebNodes::RefreshType(const UClass* WebClass, FCodeAnimOutputSelection& Selection)
{
	FEdGraphPinType NewType;
	bool bNewHasValue = false;
	if (const FProperty* Output = Resolve(WebClass, Selection))
	{
		bNewHasValue = GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Output, NewType);
	}

	const bool bChanged = bNewHasValue != Selection.bHasValue || (bNewHasValue && NewType != Selection.ValueType);
	Selection.bHasValue = bNewHasValue;
	Selection.ValueType = bNewHasValue ? NewType : FEdGraphPinType();
	return bChanged;
}

UClass* CodeAnimWebNodes::GetOutputSourceClass(const UEdGraphNode* Node, bool& bOutAllowsAny)
{
	bOutAllowsAny = false;
	if (const UK2Node_GetTransitionOutput* Get = Cast<UK2Node_GetTransitionOutput>(Node))
	{
		return Get->GetWebClass();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Web pin
// ---------------------------------------------------------------------------

UClass* UK2Node_CodeAnimWebBase::GetWebClass() const
{
	return WebClass ? WebClass.Get() : UCodeAnimationWeb::StaticClass();
}

UEdGraphPin* UK2Node_CodeAnimWebBase::CreateWebPin()
{
	UEdGraphPin* Pin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, GetWebClass(), WebPinName);
	Pin->PinFriendlyName = LOCTEXT("WebPin", "Web");
	Pin->PinToolTip = LOCTEXT("WebPinTooltip",
		"The Code Animation Web. The node takes on its type: its outputs, states and variables.").ToString();
	return Pin;
}

UClass* UK2Node_CodeAnimWebBase::ResolveWebClassFromPin() const
{
	const UEdGraphPin* Pin = FindPin(WebPinName);
	const UEdGraphPin* Linked = Pin && Pin->LinkedTo.Num() > 0 ? Pin->LinkedTo[0] : nullptr;

	UClass* Class = nullptr;
	if (Linked)
	{
		if (Linked->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
		{
			const UBlueprint* Owner = FBlueprintEditorUtils::FindBlueprintForNode(Linked->GetOwningNode());
			Class = Owner ? Owner->GeneratedClass.Get() : nullptr;
		}
		else
		{
			Class = Cast<UClass>(Linked->PinType.PinSubCategoryObject.Get());
		}
	}
	else if (const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(this))
	{
		// Unwired means self: typed only inside a Web's own Blueprint.
		Class = Blueprint->GeneratedClass.Get();
	}

	if (Class)
	{
		// A reinstanced or skeleton class resolves to the class it stands for.
		Class = Class->GetAuthoritativeClass();
	}
	return Class && Class->IsChildOf(UCodeAnimationWeb::StaticClass()) ? Class : nullptr;
}

void UK2Node_CodeAnimWebBase::GetWebTypedPins(TArray<UEdGraphPin*>& OutPins) const
{
	if (UEdGraphPin* Pin = FindPin(WebPinName))
	{
		OutPins.Add(Pin);
	}
}

void UK2Node_CodeAnimWebBase::RefreshWebClass()
{
	UClass* NewClass = ResolveWebClassFromPin();
	if (NewClass == WebClass)
	{
		return;
	}

	Modify();
	WebClass = NewClass;

	TArray<UEdGraphPin*> Typed;
	GetWebTypedPins(Typed);
	for (UEdGraphPin* Pin : Typed)
	{
		Pin->Modify();
		Pin->PinType.PinSubCategoryObject = GetWebClass();
	}

	OnWebClassChanged();
	NotifyChanged();
}

void UK2Node_CodeAnimWebBase::NotifyChanged()
{
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyNodeChanged(this);
	}
	if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

void UK2Node_CodeAnimWebBase::NotifyPinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::NotifyPinConnectionListChanged(Pin);
	if (Pin && Pin->PinName == WebPinName && Pin->Direction == EGPD_Input)
	{
		RefreshWebClass();
	}
}

void UK2Node_CodeAnimWebBase::PostReconstructNode()
{
	Super::PostReconstructNode();
	RefreshWebClass();
}

void UK2Node_CodeAnimWebBase::PreloadRequiredAssets()
{
	if (WebClass)
	{
		PreloadObject(WebClass);
	}
	Super::PreloadRequiredAssets();
}

bool UK2Node_CodeAnimWebBase::CheckWebSource(FKismetCompilerContext& CompilerContext)
{
	const UEdGraphPin* Pin = FindPin(WebPinName);
	if (Pin && Pin->LinkedTo.Num() == 0)
	{
		const UClass* Context = CompilerContext.Blueprint ? CompilerContext.Blueprint->ParentClass.Get() : nullptr;
		if (!Context || !Context->IsChildOf(UCodeAnimationWeb::StaticClass()))
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("NoWeb", "@@ needs a Code Animation Web wired into Web.").ToString(), this);
			return false;
		}
	}
	return true;
}

bool UK2Node_CodeAnimWebBase::HasExternalDependencies(TArray<UStruct*>* OptionalOutput) const
{
	const UBlueprint* Blueprint = GetBlueprint();
	const bool bExternal = WebClass && WebClass->ClassGeneratedBy != Blueprint;
	if (bExternal && OptionalOutput)
	{
		OptionalOutput->AddUnique(WebClass);
	}
	const bool bSuper = Super::HasExternalDependencies(OptionalOutput);
	return bExternal || bSuper;
}

FSlateIcon UK2Node_CodeAnimWebBase::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), TEXT("Kismet.AllClasses.FunctionIcon"));
	return Icon;
}

void UK2Node_CodeAnimWebBase::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionKey);
		check(Spawner);
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

FText UK2Node_CodeAnimWebBase::GetMenuCategory() const
{
	return LOCTEXT("Category", "Code Animation Web");
}

#undef LOCTEXT_NAMESPACE
