#include "SGraphPinPrimitiveDataParameter.h"

#include "EdGraph/EdGraphSchema.h"
#include "K2Node_SetNamedPrimitiveData.h"
#include "PrimitiveDataEntryLabel.h"
#include "PrimitiveDataLegend.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphPinPrimitiveDataParameter"

void SGraphPinPrimitiveDataParameter::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
}

UK2Node_SetNamedPrimitiveData* SGraphPinPrimitiveDataParameter::GetNode() const
{
	return GraphPinObj ? Cast<UK2Node_SetNamedPrimitiveData>(GraphPinObj->GetOwningNodeUnchecked()) : nullptr;
}

void SGraphPinPrimitiveDataParameter::RebuildOptions()
{
	Options.Reset();
	const UK2Node_SetNamedPrimitiveData* Node = GetNode();
	if (const UPrimitiveDataLegend* Legend = Node ? Node->GetLegend() : nullptr)
	{
		for (const FPrimitiveDataLegendEntry& Entry : Legend->Parameters)
		{
			Options.Add(MakeShared<FName>(Entry.Name));
		}
	}
}

FText SGraphPinPrimitiveDataParameter::LabelFor(FName Name) const
{
	const UK2Node_SetNamedPrimitiveData* Node = GetNode();
	const UPrimitiveDataLegend* Legend = Node ? Node->GetLegend() : nullptr;
	const FPrimitiveDataLegendEntry* Entry = Legend ? Legend->FindParameter(Name) : nullptr;
	return Entry ? PrimitiveDataEntryLabel(*Entry) : FText::FromName(Name);
}

TSharedRef<SWidget> SGraphPinPrimitiveDataParameter::GetDefaultValueWidget()
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
		.OnSelectionChanged(this, &SGraphPinPrimitiveDataParameter::OnSelected)
		.Content()
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				if (!GraphPinObj || GraphPinObj->bWasTrashed)
				{
					return FText::GetEmpty();
				}
				const UK2Node_SetNamedPrimitiveData* Node = GetNode();
				if (!Node || !Node->GetLegend())
				{
					return LOCTEXT("PickLegend", "Choose a legend first");
				}
				const FString Current = GraphPinObj->GetDefaultAsString();
				return Current.IsEmpty() ? LOCTEXT("PickParameter", "Choose a parameter") : LabelFor(FName(*Current));
			})
		];
}

void SGraphPinPrimitiveDataParameter::OnSelected(TSharedPtr<FName> Item, ESelectInfo::Type Info)
{
	// Direct is a programmatic selection (RefreshOptions can cause one), not the user choosing.
	if (Info == ESelectInfo::Direct || !Item.IsValid() || !GraphPinObj || GraphPinObj->bWasTrashed)
	{
		return;
	}

	const FString NewValue = Item->ToString();
	if (GraphPinObj->GetDefaultAsString() == NewValue)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeParameter", "Change Primitive Data Parameter"));
	GraphPinObj->Modify();
	GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewValue);
}

TSharedPtr<SGraphPin> FPrimitiveDataParameterPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (Pin && Pin->PinName == UK2Node_SetNamedPrimitiveData::ParameterPinName
		&& Cast<UK2Node_SetNamedPrimitiveData>(Pin->GetOwningNodeUnchecked()))
	{
		return SNew(SGraphPinPrimitiveDataParameter, Pin);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
