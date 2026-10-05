#include "SetNamedPrimitiveDataMultiDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "K2Node_SetNamedPrimitiveDataMulti.h"
#include "PrimitiveDataEntryLabel.h"
#include "PrimitiveDataLegend.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SetNamedPrimitiveDataMultiDetails"

TSharedRef<IDetailCustomization> FSetNamedPrimitiveDataMultiDetails::MakeInstance()
{
	return MakeShared<FSetNamedPrimitiveDataMultiDetails>();
}

FSetNamedPrimitiveDataMultiDetails::~FSetNamedPrimitiveDataMultiDetails()
{
	if (UK2Node_SetNamedPrimitiveDataMulti* Current = Node.Get())
	{
		Current->OnParametersOffered.Remove(OfferedHandle);
	}
}

void FSetNamedPrimitiveDataMultiDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	Builder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FSetNamedPrimitiveDataMultiDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	// One node at a time: ticking across several nodes with different legends has no clear meaning.
	Node = Objects.Num() == 1 ? Cast<UK2Node_SetNamedPrimitiveDataMulti>(Objects[0].Get()) : nullptr;
	if (!Node.IsValid())
	{
		return;
	}

	// A different legend, or the legend edited: rebuild the list.
	OfferedHandle = Node->OnParametersOffered.AddLambda([WeakBuilder = Builder]()
	{
		if (const TSharedPtr<IDetailLayoutBuilder> Pinned = WeakBuilder.Pin())
		{
			Pinned->ForceRefreshDetails();
		}
	});

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Parameters"), LOCTEXT("Category", "Parameters"));

	Category.AddCustomRow(LOCTEXT("WriteFilter", "write slots update"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.AutoWrapText(true)
			.Text(this, &FSetNamedPrimitiveDataMultiDetails::DescribeWrite)
		];

	const UPrimitiveDataLegend* Legend = Node->GetLegend();
	if (!Legend)
	{
		return;
	}

	// Built once per refresh of the panel; OnParametersOffered refreshes it when the list changes.
	for (const FPrimitiveDataLegendEntry& Entry : Legend->Parameters)
	{
		const FGuid Id = Entry.Id;
		const FText Label = PrimitiveDataEntryLabel(Entry);
		TWeakObjectPtr<UK2Node_SetNamedPrimitiveDataMulti> WeakNode = Node;

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
					const UK2Node_SetNamedPrimitiveDataMulti* Current = WeakNode.Get();
					return Current && Current->IsParameterSelected(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakNode, Id](ECheckBoxState State)
				{
					if (UK2Node_SetNamedPrimitiveDataMulti* Current = WeakNode.Get())
					{
						const FScopedTransaction Transaction(LOCTEXT("Toggle", "Change Primitive Data Parameters"));
						Current->SetParameterSelected(Id, State == ECheckBoxState::Checked);
					}
				})
			];
	}
}

FText FSetNamedPrimitiveDataMultiDetails::DescribeWrite() const
{
	const UK2Node_SetNamedPrimitiveDataMulti* Current = Node.Get();
	if (!Current)
	{
		return FText::GetEmpty();
	}
	if (!Current->GetLegend())
	{
		return LOCTEXT("NoLegend", "Choose a Primitive Data Legend on the node first.");
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
	const uint64 Full = Count >= 64 ? ~uint64(0) : (uint64(1) << Count) - 1;
	FText Write = Mask == Full
		? FText::Format(LOCTEXT("Contiguous", "One update per mesh: {0}."), Range)
		: FText::Format(LOCTEXT("Gapped",
			"One update per mesh: {0}. Slots in between that are not ticked are read back off the mesh and kept as they are."),
			Range);
	if (Current->bWriteDefaults)
	{
		Write = FText::Format(LOCTEXT("Defaults",
			"{0}\nWrites the saved defaults, not runtime data: for construction scripts. Rebuilds the mesh's render state, so not every frame."),
			Write);
	}
	return Write;
}

#undef LOCTEXT_NAMESPACE
