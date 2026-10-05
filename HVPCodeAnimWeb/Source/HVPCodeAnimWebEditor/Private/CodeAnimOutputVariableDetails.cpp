#include "CodeAnimOutputVariableDetails.h"

#include "BlueprintEditor.h"
#include "CodeAnimationWeb.h"
#include "CodeAnimWebEvents.h"
#include "CodeAnimWebGraphs.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UObject/PropertyWrapper.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CodeAnimOutputVariableDetails"

namespace CodeAnimOutputVariableDetails
{
	static const FText OutputsCategory = LOCTEXT("OutputsCategory", "Animation Outputs");

	static bool IsWebBlueprint(const UBlueprint* Blueprint)
	{
		const UClass* Class = Blueprint ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
		if (!Class && Blueprint)
		{
			Class = Blueprint->ParentClass;
		}
		return Class && Class->IsChildOf(UCodeAnimationWeb::StaticClass());
	}
}

TSharedPtr<IDetailCustomization> FCodeAnimOutputVariableDetails::MakeInstance(TSharedPtr<IBlueprintEditor> BlueprintEditor)
{
	// Only FBlueprintEditor (and its subclasses) hand variable customizations out, so the cast is safe.
	const TSharedPtr<FBlueprintEditor> Editor = StaticCastSharedPtr<FBlueprintEditor>(BlueprintEditor);
	UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
	if (!CodeAnimOutputVariableDetails::IsWebBlueprint(Blueprint))
	{
		return nullptr;
	}
	return MakeShared<FCodeAnimOutputVariableDetails>(Blueprint);
}

void FCodeAnimOutputVariableDetails::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayout.GetObjectsBeingCustomized(Objects);
	const UPropertyWrapper* Wrapper = Objects.Num() == 1 ? Cast<UPropertyWrapper>(Objects[0].Get()) : nullptr;
	const FProperty* Property = Wrapper ? Wrapper->GetProperty() : nullptr;
	UBlueprint* BlueprintPtr = Blueprint.Get();
	if (!Property || !BlueprintPtr)
	{
		return;
	}

	// This Blueprint's own member variables only: not locals, not components, not inherited ones.
	VariableName = Property->GetFName();
	if (FBlueprintEditorUtils::FindNewVariableIndex(BlueprintPtr, VariableName) == INDEX_NONE)
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailLayout.EditCategory(TEXT("CodeAnimationWeb"),
		LOCTEXT("Category", "Code Animation Web"), ECategoryPriority::Important);

	Category.AddCustomRow(LOCTEXT("OutputSearch", "Animation Output"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("OutputLabel", "Animation Output"))
		.ToolTipText(LOCTEXT("OutputTooltip",
			"Drive this variable from the Web's states. It then appears in every state's State Values (after compiling), "
			"and its own On <Output> Changed and On Outputs Changed fire when it changes."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([this]() { return IsOutput() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetOutput(State == ECheckBoxState::Checked); })
	];

	const UEnum* LerpEnum = StaticEnum<ECodeAnimLerpPolicy>();
	Category.AddCustomRow(LOCTEXT("LerpSearch", "Lerp"))
	.IsEnabled(TAttribute<bool>::CreateLambda([this]() { return IsOutput(); }))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("LerpLabel", "Lerp"))
		.ToolTipText(LerpEnum->GetToolTipTextByIndex(0))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SComboButton)
		.OnGetMenuContent_Lambda([this, LerpEnum]()
		{
			FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
			const int32 Num = LerpEnum->NumEnums() - 1;
			for (int32 Index = 0; Index < Num; ++Index)
			{
				const ECodeAnimLerpPolicy Value = static_cast<ECodeAnimLerpPolicy>(LerpEnum->GetValueByIndex(Index));
				Menu.AddMenuEntry(LerpEnum->GetDisplayNameTextByIndex(Index), LerpEnum->GetToolTipTextByIndex(Index),
					FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Value]() { SetLerp(Value); })));
			}
			return Menu.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text_Lambda([this, LerpEnum]() { return LerpEnum->GetDisplayNameTextByValue(static_cast<int64>(GetLerp())); })
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];

	Category.AddCustomRow(LOCTEXT("CustomLerpSearch", "Custom Lerp"))
	.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
	{
		return IsOutput() && GetLerp() == ECodeAnimLerpPolicy::Custom ? EVisibility::Visible : EVisibility::Collapsed;
	}))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("CustomLerpLabel", "Custom Lerp Graph"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.ToolTipText(LOCTEXT("CustomLerpTooltip",
			"The graph that decides this output during every transition, after the automatic blend. Opens it, or creates it."))
		.Text_Lambda([this]()
		{
			UBlueprint* BlueprintPtr = Blueprint.Get();
			return CodeAnimWebGraphs::ButtonLabel(BlueprintPtr, CodeAnimWebGraphs::FindCustomLerpGraph(BlueprintPtr, VariableName));
		})
		.OnClicked_Lambda([this]()
		{
			CodeAnimWebGraphs::OpenOrCreateCustomLerpGraph(Blueprint.Get(), VariableName);
			return FReply::Handled();
		})
	];
}

bool FCodeAnimOutputVariableDetails::IsOutput() const
{
	FString Value;
	return Blueprint.IsValid() && FBlueprintEditorUtils::GetBlueprintVariableMetaData(
		Blueprint.Get(), VariableName, nullptr, CodeAnimWeb::OutputMetaKey, Value);
}

void FCodeAnimOutputVariableDetails::SetOutput(bool bOutput)
{
	UBlueprint* BlueprintPtr = Blueprint.Get();
	if (!BlueprintPtr || bOutput == IsOutput())
	{
		return;
	}

	const FScopedTransaction Transaction(bOutput
		? LOCTEXT("MakeOutput", "Make Animation Output")
		: LOCTEXT("UnmakeOutput", "Remove Animation Output"));
	BlueprintPtr->Modify();

	if (bOutput)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(BlueprintPtr, VariableName, nullptr,
			CodeAnimWeb::OutputMetaKey, TEXT("true"));

		// Gathered under one heading, unless the variable was already filed somewhere on purpose.
		const FText Category = FBlueprintEditorUtils::GetBlueprintVariableCategory(BlueprintPtr, VariableName, nullptr);
		if (Category.IsEmpty() || Category.EqualTo(UEdGraphSchema_K2::VR_DefaultCategory))
		{
			FBlueprintEditorUtils::SetBlueprintVariableCategory(BlueprintPtr, VariableName, nullptr,
				CodeAnimOutputVariableDetails::OutputsCategory, /*bDontRecompile*/ true);
		}
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BlueprintPtr, VariableName, nullptr, CodeAnimWeb::OutputMetaKey);
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BlueprintPtr, VariableName, nullptr, CodeAnimWeb::LerpMetaKey);
	}

	// The output's On <Output> Changed dispatcher appears (or goes) straight away; the rest takes
	// effect on compile, like any other change to a variable.
	CodeAnimWebEvents::Reconcile(BlueprintPtr);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BlueprintPtr);
}

ECodeAnimLerpPolicy FCodeAnimOutputVariableDetails::GetLerp() const
{
	FString Value;
	if (Blueprint.IsValid() && FBlueprintEditorUtils::GetBlueprintVariableMetaData(
		Blueprint.Get(), VariableName, nullptr, CodeAnimWeb::LerpMetaKey, Value))
	{
		const int64 Lerp = StaticEnum<ECodeAnimLerpPolicy>()->GetValueByNameString(Value);
		if (Lerp != INDEX_NONE)
		{
			return static_cast<ECodeAnimLerpPolicy>(Lerp);
		}
	}
	return ECodeAnimLerpPolicy::Auto;
}

void FCodeAnimOutputVariableDetails::SetLerp(ECodeAnimLerpPolicy Lerp)
{
	UBlueprint* BlueprintPtr = Blueprint.Get();
	if (!BlueprintPtr || Lerp == GetLerp())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetLerp", "Set Animation Output Lerp"));
	BlueprintPtr->Modify();
	if (Lerp == ECodeAnimLerpPolicy::Auto)
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BlueprintPtr, VariableName, nullptr, CodeAnimWeb::LerpMetaKey);
	}
	else
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(BlueprintPtr, VariableName, nullptr, CodeAnimWeb::LerpMetaKey,
			StaticEnum<ECodeAnimLerpPolicy>()->GetNameStringByValue(static_cast<int64>(Lerp)));
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(BlueprintPtr);
}

#undef LOCTEXT_NAMESPACE
