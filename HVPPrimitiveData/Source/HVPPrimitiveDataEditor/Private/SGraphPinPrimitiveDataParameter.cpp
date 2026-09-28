#include "SGraphPinPrimitiveDataParameter.h"

#include "EdGraph/EdGraphSchema.h"
#include "K2Node_SetIndexedPrimitiveData.h"
#include "PrimitiveDataIndex.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphPinPrimitiveDataParameter"

void SGraphPinPrimitiveDataParameter::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
}

UK2Node_SetIndexedPrimitiveData* SGraphPinPrimitiveDataParameter::GetNode() const
{
	return GraphPinObj ? Cast<UK2Node_SetIndexedPrimitiveData>(GraphPinObj->GetOwningNodeUnchecked()) : nullptr;
}

void SGraphPinPrimitiveDataParameter::RebuildOptions()
{
	Options.Reset();
	const UK2Node_SetIndexedPrimitiveData* Node = GetNode();
	if (const UPrimitiveDataIndex* Index = Node ? Node->GetIndex() : nullptr)
	{
		for (const FPrimitiveDataIndexEntry& Entry : Index->Parameters)
		{
			Options.Add(MakeShared<FName>(Entry.Name));
		}
	}
}

FText SGraphPinPrimitiveDataParameter::LabelFor(FName Name) const
{
	const UK2Node_SetIndexedPrimitiveData* Node = GetNode();
	const UPrimitiveDataIndex* Index = Node ? Node->GetIndex() : nullptr;
	const FPrimitiveDataIndexEntry* Entry = Index ? Index->FindParameter(Name) : nullptr;
	if (!Entry)
	{
		return FText::FromName(Name);
	}

	// The slot is shown, not hidden: the whole point is that nobody has to go and look it up, but
	// being able to see it at a glance is still useful when reading a material alongside.
	if (!Entry->HasSlot())
	{
		return FText::Format(LOCTEXT("NoSlot", "{0}  (no slot - index full)"), FText::FromName(Entry->Name));
	}
	if (Entry->Type == EPrimitiveDataParameterType::Vector)
	{
		return FText::Format(LOCTEXT("VectorLabel", "{0}  (color, {1}-{2})"),
			FText::FromName(Entry->Name), FText::AsNumber(Entry->Slot), FText::AsNumber(Entry->Slot + 3));
	}
	return FText::Format(LOCTEXT("ScalarLabel", "{0}  (scalar, {1})"),
		FText::FromName(Entry->Name), FText::AsNumber(Entry->Slot));
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
				const UK2Node_SetIndexedPrimitiveData* Node = GetNode();
				if (!Node || !Node->GetIndex())
				{
					return LOCTEXT("PickIndex", "Choose an index first");
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
	if (Pin && Pin->PinName == UK2Node_SetIndexedPrimitiveData::ParameterPinName
		&& Cast<UK2Node_SetIndexedPrimitiveData>(Pin->GetOwningNodeUnchecked()))
	{
		return SNew(SGraphPinPrimitiveDataParameter, Pin);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
