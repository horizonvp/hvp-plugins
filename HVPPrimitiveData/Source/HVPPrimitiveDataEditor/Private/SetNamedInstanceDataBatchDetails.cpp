#include "SetNamedInstanceDataBatchDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "InstanceDataLegend.h"
#include "K2Node_SetNamedInstanceDataBatch.h"
#include "PrimitiveDataEntryLabel.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SetNamedInstanceDataBatchDetails"

TSharedRef<IDetailCustomization> FSetNamedInstanceDataBatchDetails::MakeInstance()
{
	return MakeShared<FSetNamedInstanceDataBatchDetails>();
}

FSetNamedInstanceDataBatchDetails::~FSetNamedInstanceDataBatchDetails()
{
	if (UK2Node_SetNamedInstanceDataBatch* Current = Node.Get())
	{
		Current->OnParametersOffered.Remove(OfferedHandle);
	}
}

void FSetNamedInstanceDataBatchDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	Builder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FSetNamedInstanceDataBatchDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	// One node at a time: ticking across several nodes with different legends has no clear meaning.
	Node = Objects.Num() == 1 ? Cast<UK2Node_SetNamedInstanceDataBatch>(Objects[0].Get()) : nullptr;
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

	Category.AddCustomRow(LOCTEXT("WriteFilter", "write slots floats instance batch"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.AutoWrapText(true)
			.Text(this, &FSetNamedInstanceDataBatchDetails::DescribeWrite)
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
		TWeakObjectPtr<UK2Node_SetNamedInstanceDataBatch> WeakNode = Node;

		Category.AddCustomRow(Label)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(Label)
			]
			.ValueContent()
			.MinDesiredWidth(200.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsEnabled(Entry.HasSlot())
					.ToolTipText(LOCTEXT("SetTooltip", "Set this parameter."))
					.IsChecked_Lambda([WeakNode, Id]()
					{
						const UK2Node_SetNamedInstanceDataBatch* Current = WeakNode.Get();
						return Current && Current->IsParameterSelected(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([WeakNode, Id](ECheckBoxState State)
					{
						if (UK2Node_SetNamedInstanceDataBatch* Current = WeakNode.Get())
						{
							const FScopedTransaction Transaction(LOCTEXT("Toggle", "Change Instance Data Parameters"));
							Current->SetParameterSelected(Id, State == ECheckBoxState::Checked);
						}
					})
					[
						SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).Text(LOCTEXT("Set", "Set"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(12.f, 0.f, 0.f, 0.f)
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("PerInstanceTooltip",
						"Take an array with a value per instance (the k-th value for the k-th index) rather than one value for them all."))
					.IsEnabled_Lambda([WeakNode, Id]()
					{
						const UK2Node_SetNamedInstanceDataBatch* Current = WeakNode.Get();
						return Current && Current->IsParameterSelected(Id);
					})
					.IsChecked_Lambda([WeakNode, Id]()
					{
						const UK2Node_SetNamedInstanceDataBatch* Current = WeakNode.Get();
						return Current && Current->IsParameterPerInstance(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([WeakNode, Id](ECheckBoxState State)
					{
						if (UK2Node_SetNamedInstanceDataBatch* Current = WeakNode.Get())
						{
							const FScopedTransaction Transaction(LOCTEXT("TogglePerInstance", "Change Instance Data Per Instance"));
							Current->SetParameterPerInstance(Id, State == ECheckBoxState::Checked);
						}
					})
					[
						SNew(STextBlock).Font(IDetailLayoutBuilder::GetDetailFont()).Text(LOCTEXT("PerInstance", "Per Instance"))
					]
				]
			];
	}
}

FText FSetNamedInstanceDataBatchDetails::DescribeWrite() const
{
	const UK2Node_SetNamedInstanceDataBatch* Current = Node.Get();
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
		return LOCTEXT("Nothing", "Tick the parameters to set. Each gets a pin on the node; Per Instance makes it an array.");
	}

	return FText::Format(LOCTEXT("Write",
		"One call for all the instances, looping in C++: consecutive ascending indices are written as one block, any "
		"other order row by row. Per instance arrays give the k-th value to the k-th index. "
		"The mesh needs Num Custom Data Floats of at least {0} (this legend's Floats Per Instance is {1})."),
		FText::AsNumber(First + Count), FText::AsNumber(Legend->FloatsPerInstance));
}

#undef LOCTEXT_NAMESPACE
