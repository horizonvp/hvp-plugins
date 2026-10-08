#include "SetNamedInstanceDataMultiDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "InstanceDataLegend.h"
#include "K2Node_SetNamedInstanceDataMulti.h"
#include "PrimitiveDataEntryLabel.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SetNamedInstanceDataMultiDetails"

TSharedRef<IDetailCustomization> FSetNamedInstanceDataMultiDetails::MakeInstance()
{
	return MakeShared<FSetNamedInstanceDataMultiDetails>();
}

FSetNamedInstanceDataMultiDetails::~FSetNamedInstanceDataMultiDetails()
{
	if (UK2Node_SetNamedInstanceDataMulti* Current = Node.Get())
	{
		Current->OnParametersOffered.Remove(OfferedHandle);
	}
}

void FSetNamedInstanceDataMultiDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	Builder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FSetNamedInstanceDataMultiDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	// One node at a time: ticking across several nodes with different legends has no clear meaning.
	Node = Objects.Num() == 1 ? Cast<UK2Node_SetNamedInstanceDataMulti>(Objects[0].Get()) : nullptr;
	if (!Node.IsValid())
	{
		return;
	}

	OfferedHandle = Node->OnParametersOffered.AddLambda([WeakBuilder = Builder]()
	{
		if (const TSharedPtr<IDetailLayoutBuilder> Pinned = WeakBuilder.Pin())
		{
			Pinned->ForceRefreshDetails();
		}
	});

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Parameters"), LOCTEXT("Category", "Parameters"));

	Category.AddCustomRow(LOCTEXT("WriteFilter", "write slots floats instance"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.AutoWrapText(true)
			.Text(this, &FSetNamedInstanceDataMultiDetails::DescribeWrite)
		];

	const UInstanceDataLegend* Legend = Node->GetLegend();
	if (!Legend)
	{
		return;
	}

	for (const FInstanceDataLegendEntry& Entry : Legend->Parameters)
	{
		const FGuid Id = Entry.Id;
		const FText Label = PrimitiveDataEntryLabel(Entry);
		TWeakObjectPtr<UK2Node_SetNamedInstanceDataMulti> WeakNode = Node;

		Category.AddCustomRow(Label)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(Label)
			]
			.ValueContent()
			[
				SNew(SCheckBox)
				.IsEnabled(Entry.HasSlot())
				.IsChecked_Lambda([WeakNode, Id]()
				{
					const UK2Node_SetNamedInstanceDataMulti* Current = WeakNode.Get();
					return Current && Current->IsParameterSelected(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakNode, Id](ECheckBoxState State)
				{
					if (UK2Node_SetNamedInstanceDataMulti* Current = WeakNode.Get())
					{
						const FScopedTransaction Transaction(LOCTEXT("Toggle", "Change Instance Data Parameters"));
						Current->SetParameterSelected(Id, State == ECheckBoxState::Checked);
					}
				})
			];
	}
}

FText FSetNamedInstanceDataMultiDetails::DescribeWrite() const
{
	const UK2Node_SetNamedInstanceDataMulti* Current = Node.Get();
	if (!Current)
	{
		return FText::GetEmpty();
	}
	const UInstanceDataLegend* Legend = Current->GetLegend();
	if (!Legend)
	{
		return LOCTEXT("NoLegend", "Choose an Instance Data Legend on the node first.");
	}

	int32 First = 0;
	int32 Count = 0;
	uint64 Mask = 0;
	if (!Current->GetWriteRange(First, Count, Mask))
	{
		return LOCTEXT("Nothing", "Tick the parameters to set. Each gets a pin on the node.");
	}

	const FText Range = Count > 1
		? FText::Format(LOCTEXT("Range", "slots {0}-{1}"), FText::AsNumber(First), FText::AsNumber(First + Count - 1))
		: FText::Format(LOCTEXT("Single", "slot {0}"), FText::AsNumber(First));
	return FText::Format(LOCTEXT("Write",
		"One write per instance: {0}; floats in between that are not ticked keep their values. "
		"The mesh needs Num Custom Data Floats of at least {1} (this legend's Floats Per Instance is {2})."),
		Range, FText::AsNumber(First + Count), FText::AsNumber(Legend->FloatsPerInstance));
}

#undef LOCTEXT_NAMESPACE
