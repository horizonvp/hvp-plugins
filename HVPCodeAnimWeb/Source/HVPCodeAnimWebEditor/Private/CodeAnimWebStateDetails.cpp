#include "CodeAnimWebStateDetails.h"

#include "CodeAnimationWeb.h"
#include "CodeAnimWebGraphs.h"
#include "Engine/Blueprint.h"
#include "IDetailPropertyRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyBagDetails.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CodeAnimWebStateDetails"

namespace CodeAnimWebStateDetails
{
	static UCodeAnimationWeb* OuterWeb(const TSharedRef<IPropertyHandle>& Handle, int32 Index = 0)
	{
		TArray<UObject*> Outers;
		Handle->GetOuterObjects(Outers);
		return Outers.IsValidIndex(Index) ? Cast<UCodeAnimationWeb>(Outers[Index]) : nullptr;
	}

	static const FCodeAnimWebStateEntry* SingleEntry(const TSharedRef<IPropertyHandle>& Handle)
	{
		void* Data = nullptr;
		return Handle->GetValueData(Data) == FPropertyAccess::Success
			? static_cast<const FCodeAnimWebStateEntry*>(Data)
			: nullptr;
	}

	/** The override ticks: reads and writes the entry's OverriddenOutputs; defaults come from the Web. */
	class FStateValuesDetails : public FPropertyBagInstanceDataDetails
	{
	public:
		FStateValuesDetails(const TSharedRef<IPropertyHandle>& InEntryHandle, const FConstructParams& Params)
			: FPropertyBagInstanceDataDetails(Params)
			, EntryHandle(InEntryHandle)
		{
		}

	protected:
		struct FOverrideProvider : public IPropertyBagOverrideProvider
		{
			explicit FOverrideProvider(FCodeAnimWebStateEntry& InEntry) : Entry(InEntry) {}

			virtual bool IsPropertyOverridden(const FGuid PropertyID) const override
			{
				return Entry.OverriddenOutputs.Contains(PropertyID);
			}

			virtual void SetPropertyOverride(const FGuid PropertyID, const bool bIsOverridden) const override
			{
				if (bIsOverridden)
				{
					Entry.OverriddenOutputs.AddUnique(PropertyID);
				}
				else
				{
					Entry.OverriddenOutputs.Remove(PropertyID);
				}
			}

			FCodeAnimWebStateEntry& Entry;
		};

		virtual bool HasPropertyOverrides() const override { return true; }

		virtual void PreChangeOverrides() override
		{
			EntryHandle->NotifyPreChange();
		}

		virtual void PostChangeOverrides() override
		{
			EntryHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
			EntryHandle->NotifyFinishedChangingProperties();
		}

		virtual void EnumeratePropertyBags(TSharedPtr<IPropertyHandle> PropertyBagHandle,
			const EnumeratePropertyBagFuncRef& Func) const override
		{
			TArray<UObject*> Outers;
			EntryHandle->GetOuterObjects(Outers);
			EntryHandle->EnumerateRawData([&Func, &Outers](void* RawData, const int32 DataIndex, const int32)
			{
				FCodeAnimWebStateEntry* Entry = static_cast<FCodeAnimWebStateEntry*>(RawData);
				UCodeAnimationWeb* Web = Outers.IsValidIndex(DataIndex) ? Cast<UCodeAnimationWeb>(Outers[DataIndex]) : nullptr;
				if (!Entry || !Web)
				{
					return true;
				}
				FOverrideProvider Provider(*Entry);
				return Func(Web->GetOutputDefaults(), Entry->Values, Provider);
			});
		}

	private:
		TSharedRef<IPropertyHandle> EntryHandle;
	};
}

void FCodeAnimWebStateEntryDetails::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace CodeAnimWebStateDetails;

	const FCodeAnimWebStateEntry* Entry = SingleEntry(PropertyHandle);
	FText Name = Entry ? Entry->DisplayName : LOCTEXT("Multiple", "Multiple Values");
	if (Entry && Entry->bOrphaned)
	{
		Name = FText::Format(LOCTEXT("Orphaned", "{0} (removed from enum)"),
			Entry->DisplayName.IsEmpty() ? FText::FromName(Entry->Key) : Entry->DisplayName);
	}

	FText Summary;
	if (Entry)
	{
		const UCodeAnimationWeb* Web = OuterWeb(PropertyHandle);
		const int32 Total = Web ? Web->GetOutputNames().Num() : 0;
		Summary = Entry->OverriddenOutputs.Num() == 0
			? LOCTEXT("AllDefault", "All outputs at default")
			: FText::Format(LOCTEXT("SomeSet", "{0} of {1} outputs set"), Entry->OverriddenOutputs.Num(), Total);
	}

	const TWeakObjectPtr<UCodeAnimationWeb> WeakWeb = OuterWeb(PropertyHandle);
	const FName Key = Entry ? Entry->Key : NAME_None;
	const bool bCanHaveGraph = Entry && !Entry->bOrphaned && WeakWeb.IsValid();
	const TSharedPtr<IPropertyUtilities> Utilities = CustomizationUtils.GetPropertyUtilities();

	HeaderRow
	.NameContent()
	[
		SNew(STextBlock)
		.Text(Name)
		.Font(CustomizationUtils.GetRegularFont())
		.ColorAndOpacity(Entry && Entry->bOrphaned ? FSlateColor(FLinearColor(1.0f, 0.55f, 0.15f)) : FSlateColor::UseForeground())
	]
	.ValueContent()
	.MinDesiredWidth(260.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Summary)
			.Font(CustomizationUtils.GetRegularFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Visibility(bCanHaveGraph ? EVisibility::Visible : EVisibility::Collapsed)
			.ToolTipText(LOCTEXT("StateGraphTooltip",
				"A graph run every frame this state is in play, on top of the values set here: set outputs from TimeInState "
				"for motion like a wiggle. Opens it, or creates it."))
			.Text_Lambda([WeakWeb, Key]()
			{
				const UCodeAnimationWeb* Web = WeakWeb.Get();
				const FCodeAnimWebStateEntry* E = Web
					? Web->States.FindByPredicate([Key](const FCodeAnimWebStateEntry& S) { return S.Key == Key; })
					: nullptr;
				return CodeAnimWebGraphs::ButtonLabel(Web ? Web->GetWebBlueprint() : nullptr, E ? E->GraphGuid : FGuid());
			})
			.OnClicked_Lambda([WeakWeb, Key, Utilities]()
			{
				CodeAnimWebGraphs::OpenOrCreateStateGraph(WeakWeb.Get(), Key);
				if (Utilities.IsValid())
				{
					Utilities->RequestRefresh();
				}
				return FReply::Handled();
			})
		]
	];
}

void FCodeAnimWebStateEntryDetails::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace CodeAnimWebStateDetails;

	const TSharedPtr<IPropertyHandle> ValuesHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCodeAnimWebStateEntry, Values));
	if (!ValuesHandle.IsValid())
	{
		return;
	}

	FPropertyBagInstanceDataDetails::FConstructParams Params;
	Params.BagStructProperty = ValuesHandle;
	Params.PropUtils = CustomizationUtils.GetPropertyUtilities();
	Params.bAllowContainers = false;
	// Outputs are defined by the Web's variables; here they can only be given values.
	Params.ChildRowFeatures = EPropertyBagChildRowFeatures::Fixed;
	ChildBuilder.AddCustomBuilder(MakeShared<FStateValuesDetails>(PropertyHandle, Params));
}

void FCodeAnimWebStateRefDetails::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	KeyHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCodeAnimWebStateRef, Key));
	Web = CodeAnimWebStateDetails::OuterWeb(PropertyHandle);

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(160.0f)
	[
		SNew(SComboButton)
		.OnGetMenuContent(this, &FCodeAnimWebStateRefDetails::MakeMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(this, &FCodeAnimWebStateRefDetails::GetCurrentText)
			.Font(CustomizationUtils.GetRegularFont())
		]
	];
}

FText FCodeAnimWebStateRefDetails::GetCurrentText() const
{
	FName Key;
	if (!KeyHandle.IsValid())
	{
		return FText::GetEmpty();
	}
	if (KeyHandle->GetValue(Key) != FPropertyAccess::Success)
	{
		return LOCTEXT("MultipleStates", "Multiple Values");
	}
	if (Key.IsNone())
	{
		return LOCTEXT("NoState", "None");
	}

	TArray<TPair<FName, FText>> States;
	if (const UCodeAnimationWeb* WebPtr = Web.Get())
	{
		WebPtr->GetStateList(States);
	}
	for (const TPair<FName, FText>& State : States)
	{
		if (State.Key == Key)
		{
			return State.Value;
		}
	}
	return FText::Format(LOCTEXT("MissingState", "{0} (not in enum)"), FText::FromName(Key));
}

TSharedRef<SWidget> FCodeAnimWebStateRefDetails::MakeMenu()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);

	auto Add = [this, &Menu](const FName Key, const FText& Label)
	{
		Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Key]()
		{
			if (KeyHandle.IsValid())
			{
				KeyHandle->SetValue(Key);
			}
		})));
	};

	Add(NAME_None, LOCTEXT("NoState", "None"));
	TArray<TPair<FName, FText>> States;
	if (const UCodeAnimationWeb* WebPtr = Web.Get())
	{
		WebPtr->GetStateList(States);
	}
	if (States.Num() == 0)
	{
		Menu.AddMenuEntry(LOCTEXT("NoEnum", "Pick the Web's States enum first"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}
	for (const TPair<FName, FText>& State : States)
	{
		Add(State.Key, State.Value);
	}
	return Menu.MakeWidget();
}

void FCodeAnimWebTransitionDetails::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TWeakObjectPtr<UCodeAnimationWeb> WeakWeb = CodeAnimWebStateDetails::OuterWeb(PropertyHandle);
	const int32 Index = PropertyHandle->GetIndexInArray();
	const TSharedPtr<IPropertyUtilities> Utilities = CustomizationUtils.GetPropertyUtilities();

	// "Idle -> Blocked", or with a two-headed arrow for a two-way transition, read live off the Web so it
	// follows the pickers below as they change.
	auto Title = [WeakWeb, Index]()
	{
		const UCodeAnimationWeb* Web = WeakWeb.Get();
		if (!Web || !Web->Transitions.IsValidIndex(Index))
		{
			return LOCTEXT("Transition", "Transition");
		}
		TArray<TPair<FName, FText>> States;
		Web->GetStateList(States);
		auto NameOf = [&States](FName Key)
		{
			for (const TPair<FName, FText>& State : States)
			{
				if (State.Key == Key)
				{
					return State.Value;
				}
			}
			return Key.IsNone() ? LOCTEXT("Unset", "?") : FText::FromName(Key);
		};
		const FCodeAnimWebTransition& T = Web->Transitions[Index];
		return FText::Format(T.bTwoWay ? LOCTEXT("TwoWay", "{0}  ↔  {1}") : LOCTEXT("OneWay", "{0}  →  {1}"),
			NameOf(T.From.Key), NameOf(T.To.Key));
	};

	HeaderRow
	.NameContent()
	[
		SNew(STextBlock)
		.Text_Lambda(Title)
		.Font(CustomizationUtils.GetRegularFont())
	]
	.ValueContent()
	[
		SNew(SButton)
		.ToolTipText(LOCTEXT("TransitionGraphTooltip",
			"A graph run every frame of this transition, after the automatic blend, with Alpha and the two ends to hand. "
			"Opens it, or creates it."))
		.Text_Lambda([WeakWeb, Index]()
		{
			const UCodeAnimationWeb* Web = WeakWeb.Get();
			const bool bValid = Web && Web->Transitions.IsValidIndex(Index);
			return CodeAnimWebGraphs::ButtonLabel(Web ? Web->GetWebBlueprint() : nullptr, bValid ? Web->Transitions[Index].GraphGuid : FGuid());
		})
		.OnClicked_Lambda([WeakWeb, Index, Utilities]()
		{
			CodeAnimWebGraphs::OpenOrCreateTransitionGraph(WeakWeb.Get(), Index);
			if (Utilities.IsValid())
			{
				Utilities->RequestRefresh();
			}
			return FReply::Handled();
		})
	];
}

void FCodeAnimWebTransitionDetails::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Index);
		if (!Child.IsValid())
		{
			continue;
		}
		// Timing's fields sit directly under the transition, as ShowOnlyInnerProperties would lay them out.
		if (Child->GetProperty() && Child->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FCodeAnimWebTransition, Timing))
		{
			uint32 NumTiming = 0;
			Child->GetNumChildren(NumTiming);
			for (uint32 TimingIndex = 0; TimingIndex < NumTiming; ++TimingIndex)
			{
				ChildBuilder.AddProperty(Child->GetChildHandle(TimingIndex).ToSharedRef());
			}
			continue;
		}
		ChildBuilder.AddProperty(Child.ToSharedRef());
	}
}

#undef LOCTEXT_NAMESPACE
