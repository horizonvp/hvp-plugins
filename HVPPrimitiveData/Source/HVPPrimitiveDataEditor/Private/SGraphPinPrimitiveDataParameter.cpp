#include "SGraphPinPrimitiveDataParameter.h"

#include "EdGraph/EdGraphSchema.h"
#include "InstanceDataLegend.h"
#include "K2Node_SetNamedInstanceData.h"
#include "K2Node_SetNamedPrimitiveData.h"
#include "PrimitiveDataEntryLabel.h"
#include "PrimitiveDataLegend.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphPinPrimitiveDataParameter"

void SGraphPinPrimitiveDataParameter::Construct(const FArguments& InArgs, UEdGraphPin* InPin, FNamedDataParameterSource InSource)
{
	Source = MoveTemp(InSource);
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
}

void SGraphPinPrimitiveDataParameter::RebuildOptions()
{
	Options.Reset();
	TArray<FName> Names;
	if (Source.GetNames && Source.GetNames(Names))
	{
		for (const FName Name : Names)
		{
			Options.Add(MakeShared<FName>(Name));
		}
	}
}

FText SGraphPinPrimitiveDataParameter::LabelFor(FName Name) const
{
	return Source.Label ? Source.Label(Name) : FText::FromName(Name);
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
				TArray<FName> Names;
				if (!Source.GetNames || !Source.GetNames(Names))
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

namespace PrimitiveDataParameterPin
{
	/** A source reading whichever legend the node has chosen, through the node's own GetLegend. */
	template <typename TNode>
	FNamedDataParameterSource SourceFor(UEdGraphPin* Pin)
	{
		TWeakObjectPtr<TNode> WeakNode = Cast<TNode>(Pin->GetOwningNodeUnchecked());
		FNamedDataParameterSource Source;
		Source.GetNames = [WeakNode](TArray<FName>& OutNames)
		{
			const TNode* Node = WeakNode.Get();
			const auto* Legend = Node ? Node->GetLegend() : nullptr;
			if (!Legend)
			{
				return false;
			}
			for (const auto& Entry : Legend->Parameters)
			{
				OutNames.Add(Entry.Name);
			}
			return true;
		};
		Source.Label = [WeakNode](FName Name)
		{
			const TNode* Node = WeakNode.Get();
			const auto* Legend = Node ? Node->GetLegend() : nullptr;
			const auto* Entry = Legend ? Legend->FindParameter(Name) : nullptr;
			return Entry ? PrimitiveDataEntryLabel(*Entry) : FText::FromName(Name);
		};
		return Source;
	}
}

TSharedPtr<SGraphPin> FPrimitiveDataParameterPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (!Pin)
	{
		return nullptr;
	}
	UEdGraphNode* Node = Pin->GetOwningNodeUnchecked();
	if (Pin->PinName == UK2Node_SetNamedPrimitiveData::ParameterPinName && Cast<UK2Node_SetNamedPrimitiveData>(Node))
	{
		return SNew(SGraphPinPrimitiveDataParameter, Pin, PrimitiveDataParameterPin::SourceFor<UK2Node_SetNamedPrimitiveData>(Pin));
	}
	if (Pin->PinName == UK2Node_SetNamedInstanceData::ParameterPinName && Cast<UK2Node_SetNamedInstanceData>(Node))
	{
		return SNew(SGraphPinPrimitiveDataParameter, Pin, PrimitiveDataParameterPin::SourceFor<UK2Node_SetNamedInstanceData>(Pin));
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
