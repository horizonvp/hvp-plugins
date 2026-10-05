#include "SGraphPinCodeAnimOutput.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_CodeAnimWebBase.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphPinCodeAnimOutput"

void SGraphPinCodeAnimOutput::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
}

void SGraphPinCodeAnimOutput::RebuildOptions()
{
	Options.Reset();
	if (!GraphPinObj || GraphPinObj->bWasTrashed)
	{
		return;
	}

	bool bAllowsAny = false;
	const UClass* WebClass = CodeAnimWebNodes::GetOutputSourceClass(GraphPinObj->GetOwningNodeUnchecked(), bAllowsAny);
	if (bAllowsAny)
	{
		Options.Add(MakeShared<FName>(NAME_None));
	}

	TArray<const FProperty*> Outputs;
	CodeAnimWebNodes::GetOutputs(WebClass, Outputs);
	for (const FProperty* Output : Outputs)
	{
		Options.Add(MakeShared<FName>(Output->GetFName()));
	}
}

FText SGraphPinCodeAnimOutput::LabelFor(FName Name) const
{
	if (Name.IsNone())
	{
		return LOCTEXT("Any", "(any output)");
	}

	bool bAllowsAny = false;
	const UClass* WebClass = GraphPinObj ? CodeAnimWebNodes::GetOutputSourceClass(GraphPinObj->GetOwningNodeUnchecked(), bAllowsAny) : nullptr;
	const FProperty* Output = WebClass ? WebClass->FindPropertyByName(Name) : nullptr;
	FEdGraphPinType Type;
	if (Output && GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Output, Type))
	{
		return FText::Format(LOCTEXT("Label", "{0}  ({1})"), FText::FromName(Name), UEdGraphSchema_K2::TypeToText(Type));
	}
	return FText::FromName(Name);
}

TSharedRef<SWidget> SGraphPinCodeAnimOutput::GetDefaultValueWidget()
{
	RebuildOptions();

	return SAssignNew(Combo, SComboBox<TSharedPtr<FName>>)
		.OptionsSource(&Options)
		.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
		.OnComboBoxOpening_Lambda([this]()
		{
			RebuildOptions();
			if (Combo.IsValid())
			{
				Combo->RefreshOptions();
			}
		})
		.OnGenerateWidget_Lambda([this](TSharedPtr<FName> Item)
		{
			return SNew(STextBlock).Text(Item.IsValid() ? LabelFor(*Item) : FText::GetEmpty());
		})
		.OnSelectionChanged(this, &SGraphPinCodeAnimOutput::OnSelected)
		.Content()
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (!GraphPinObj || GraphPinObj->bWasTrashed)
				{
					return FText::GetEmpty();
				}
				bool bAllowsAny = false;
				if (!CodeAnimWebNodes::GetOutputSourceClass(GraphPinObj->GetOwningNodeUnchecked(), bAllowsAny))
				{
					return LOCTEXT("NoWeb", "Wire in a Web first");
				}
				const FString Current = GraphPinObj->GetDefaultAsString();
				if (Current.IsEmpty() || Current == TEXT("None"))
				{
					return bAllowsAny ? LabelFor(NAME_None) : LOCTEXT("Pick", "Choose an output");
				}
				return FText::FromString(Current);
			})
		];
}

void SGraphPinCodeAnimOutput::OnSelected(TSharedPtr<FName> Item, ESelectInfo::Type Info)
{
	// Direct is a programmatic selection (RefreshOptions can cause one), not the user choosing.
	if (Info == ESelectInfo::Direct || !Item.IsValid() || !GraphPinObj || GraphPinObj->bWasTrashed)
	{
		return;
	}

	const FString NewValue = Item->IsNone() ? FString() : Item->ToString();
	if (GraphPinObj->GetDefaultAsString() == NewValue)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeOutput", "Change Web Output"));
	GraphPinObj->Modify();
	GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewValue);
}

TSharedPtr<SGraphPin> FCodeAnimOutputPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	// The Output pin of this plugin's nodes only - any other node may have a pin called Output.
	const UEdGraphNode* Node = Pin ? Pin->GetOwningNodeUnchecked() : nullptr;
	const bool bOurs = Node && Node->GetClass()->GetOuterUPackage() == UK2Node_CodeAnimWebBase::StaticClass()->GetOuterUPackage();
	if (bOurs && Pin->PinName == CodeAnimWebNodes::OutputPinName && Pin->Direction == EGPD_Input)
	{
		return SNew(SGraphPinCodeAnimOutput, Pin);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
