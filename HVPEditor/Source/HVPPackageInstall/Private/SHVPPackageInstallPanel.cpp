#include "SHVPPackageInstallPanel.h"

#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformProcess.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "HVPPackageInstall"

namespace HVPPackageInstallPanel
{
	constexpr float MarkerSize = 20.f;
	constexpr float LineWidth = 2.f;

	FText FormatSeconds(double Seconds)
	{
		const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FText::FromString(FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60));
	}
}

void SHVPPackageInstallPanel::Construct(const FArguments& InArgs, TWeakPtr<FHVPPackageInstaller> InInstaller)
{
	Installer = InInstaller;

	TSharedRef<SVerticalBox> Timeline = SNew(SVerticalBox);
	for (int32 Step = 0; Step < FHVPInstallStatus::Num; ++Step)
	{
		Timeline->AddSlot().AutoHeight()[MakeStepRow(Step)];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(16.f, 14.f))
		[
			SNew(SVerticalBox)

			// Header: what this run is.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PanelTitle", "Package and Install to Headset"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 12.f)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FStyleColors::Foreground)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Text_Lambda([this]()
				{
					const TSharedPtr<FHVPPackageInstaller> Pinned = Installer.Pin();
					if (!Pinned || Pinned->GetStatus().Outcome == EHVPInstallOutcome::None)
					{
						FText WhyNot;
						if (Pinned && !Pinned->CanStart(&WhyNot))
						{
							return WhyNot;
						}
						return LOCTEXT("NotRun", "Not run yet. Platforms > Package and Install to Headset starts it.");
					}
					return FText::Format(LOCTEXT("Subtitle", "{0}  ·  Android ASTC  ·  {1}"),
						FText::FromString(Pinned->GetStatus().Configuration), FText::FromString(Pinned->GetStatus().ArchiveDirectory));
				})
			]

			// The timeline.
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()[Timeline]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f)
			[
				SNew(SSeparator).Thickness(1.f)
			]

			// The result.
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeReport()
			]
		]
	];
}

const FHVPInstallStep* SHVPPackageInstallPanel::GetStep(int32 Step) const
{
	const TSharedPtr<FHVPPackageInstaller> Pinned = Installer.Pin();
	return Pinned && Pinned->GetStatus().Steps.IsValidIndex(Step) ? &Pinned->GetStatus().Steps[Step] : nullptr;
}

FSlateColor SHVPPackageInstallPanel::GetStateColor(EHVPInstallStepState State) const
{
	switch (State)
	{
	case EHVPInstallStepState::Running:   return FStyleColors::Primary;
	case EHVPInstallStepState::Succeeded: return FStyleColors::Success;
	case EHVPInstallStepState::Failed:    return FStyleColors::Error;
	case EHVPInstallStepState::Canceled:  return FStyleColors::Warning;
	default:                              return FStyleColors::Foreground;
	}
}

TSharedRef<SWidget> SHVPPackageInstallPanel::MakeStepMarker(int32 Step)
{
	// A filled circle in the state's colour (a ring while waiting or skipped), with a check, a cross
	// or nothing on top; the running one breathes.
	auto State = [this, Step]() { const FHVPInstallStep* S = GetStep(Step); return S ? S->State : EHVPInstallStepState::Pending; };
	auto Hollow = [State]() { return State() == EHVPInstallStepState::Pending || State() == EHVPInstallStepState::Skipped; };

	return SNew(SBox)
		.WidthOverride(HVPPackageInstallPanel::MarkerSize)
		.HeightOverride(HVPPackageInstallPanel::MarkerSize)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
				.DesiredSizeOverride(FVector2D(HVPPackageInstallPanel::MarkerSize))
				.ColorAndOpacity_Lambda([this, State, Hollow]()
				{
					if (Hollow())
					{
						return FSlateColor(FStyleColors::White25);
					}
					FLinearColor Color = GetStateColor(State()).GetSpecifiedColor();
					if (State() == EHVPInstallStepState::Running)
					{
						Color.A = 0.55f + 0.45f * FMath::Sin(FPlatformTime::Seconds() * 4.0);
					}
					return FSlateColor(Color);
				})
			]
			// The ring's hole.
			+ SOverlay::Slot().Padding(3.f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.FilledCircle"))
				.DesiredSizeOverride(FVector2D(HVPPackageInstallPanel::MarkerSize - 6.f))
				.ColorAndOpacity(FStyleColors::Panel)
				.Visibility_Lambda([Hollow]() { return Hollow() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(12.f))
				.ColorAndOpacity(FStyleColors::Panel)
				.Image_Lambda([State]()
				{
					switch (State())
					{
					case EHVPInstallStepState::Succeeded: return FAppStyle::GetBrush("Icons.Check");
					case EHVPInstallStepState::Failed:
					case EHVPInstallStepState::Canceled:  return FAppStyle::GetBrush("Icons.X");
					default:                              return FAppStyle::GetNoBrush();
					}
				})
			]
		];
}

FText SHVPPackageInstallPanel::GetStepStatusText(int32 Step) const
{
	const FHVPInstallStep* S = GetStep(Step);
	if (!S)
	{
		return FText::GetEmpty();
	}
	const double Now = FPlatformTime::Seconds();
	switch (S->State)
	{
	case EHVPInstallStepState::Running:
		return FText::Format(LOCTEXT("StatusRunning", "In progress  {0}"), HVPPackageInstallPanel::FormatSeconds(Now - S->StartSeconds));
	case EHVPInstallStepState::Succeeded:
		return Step == FHVPInstallStatus::Done ? LOCTEXT("StatusComplete", "Complete")
			: FText::Format(LOCTEXT("StatusSucceeded", "Done  {0}"), HVPPackageInstallPanel::FormatSeconds(S->EndSeconds - S->StartSeconds));
	case EHVPInstallStepState::Failed:   return LOCTEXT("StatusFailed", "Failed");
	case EHVPInstallStepState::Canceled: return LOCTEXT("StatusCanceled", "Canceled");
	case EHVPInstallStepState::Skipped:  return LOCTEXT("StatusSkipped", "Skipped");
	default:                             return LOCTEXT("StatusPending", "Waiting");
	}
}

TSharedRef<SWidget> SHVPPackageInstallPanel::MakeStepRow(int32 Step)
{
	const bool bLast = Step == FHVPInstallStatus::Num - 1;
	auto State = [this, Step]() { const FHVPInstallStep* S = GetStep(Step); return S ? S->State : EHVPInstallStepState::Pending; };

	return SNew(SHorizontalBox)

		// Left: the marker, and the line down to the next one.
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeStepMarker(Step)
			]
			+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).Padding(0.f, 2.f)
			[
				SNew(SBox)
				.WidthOverride(HVPPackageInstallPanel::LineWidth)
				.MinDesiredHeight(22.f)
				.Visibility(bLast ? EVisibility::Collapsed : EVisibility::Visible)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("WhiteBrush"))
					.ColorAndOpacity_Lambda([this, State]()
					{
						// Lit once the step above is behind us.
						const EHVPInstallStepState S = State();
						return S == EHVPInstallStepState::Succeeded || S == EHVPInstallStepState::Skipped
							? FSlateColor(FStyleColors::Success) : FSlateColor(FStyleColors::White25);
					})
				]
			]
		]

		// Right: name and state on one line, detail under it.
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.Text_Lambda([this, Step]() { const FHVPInstallStep* S = GetStep(Step); return S ? S->Name : FText::GetEmpty(); })
					.ColorAndOpacity_Lambda([State]()
					{
						const EHVPInstallStepState S = State();
						return S == EHVPInstallStepState::Pending || S == EHVPInstallStepState::Skipped
							? FSlateColor(FStyleColors::Foreground) : FSlateColor(FStyleColors::ForegroundHover);
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.Text_Lambda([this, Step]() { return GetStepStatusText(Step); })
					.ColorAndOpacity_Lambda([this, State]()
					{
						const EHVPInstallStepState S = State();
						return S == EHVPInstallStepState::Pending || S == EHVPInstallStepState::Skipped
							? FSlateColor(FStyleColors::Foreground) : GetStateColor(S);
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FStyleColors::Foreground)
				.AutoWrapText(true)
				.Text_Lambda([this, Step]() { const FHVPInstallStep* S = GetStep(Step); return S ? FText::FromString(S->Detail) : FText::GetEmpty(); })
			]
		];
}

FSlateColor SHVPPackageInstallPanel::GetOutcomeColor() const
{
	const TSharedPtr<FHVPPackageInstaller> Pinned = Installer.Pin();
	switch (Pinned ? Pinned->GetStatus().Outcome : EHVPInstallOutcome::None)
	{
	case EHVPInstallOutcome::Running:   return FStyleColors::Primary;
	case EHVPInstallOutcome::Succeeded: return FStyleColors::Success;
	case EHVPInstallOutcome::Failed:    return FStyleColors::Error;
	case EHVPInstallOutcome::Canceled:  return FStyleColors::Warning;
	default:                            return FStyleColors::Foreground;
	}
}

TSharedRef<SWidget> SHVPPackageInstallPanel::MakeReport()
{
	auto Status = [this]() -> const FHVPInstallStatus* { const TSharedPtr<FHVPPackageInstaller> P = Installer.Pin(); return P ? &P->GetStatus() : nullptr; };

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				.ColorAndOpacity_Lambda([this]() { return GetOutcomeColor(); })
				.Text_Lambda([Status]()
				{
					const FHVPInstallStatus* S = Status();
					return S && S->Outcome != EHVPInstallOutcome::None ? S->Headline : LOCTEXT("NoResult", "No result yet");
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FStyleColors::Foreground)
				.Text_Lambda([Status]()
				{
					const FHVPInstallStatus* S = Status();
					if (!S || S->Outcome == EHVPInstallOutcome::None)
					{
						return FText::GetEmpty();
					}
					const double End = S->Outcome == EHVPInstallOutcome::Running ? FPlatformTime::Seconds() : S->EndSeconds;
					return HVPPackageInstallPanel::FormatSeconds(End - S->StartSeconds);
				})
			]
		]
		// The report: selectable, so errors can be copied.
		+ SVerticalBox::Slot().AutoHeight().MaxHeight(150.f).Padding(0.f, 6.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(8.f)
			.Visibility_Lambda([Status]() { const FHVPInstallStatus* S = Status(); return S && !S->Report.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SMultiLineEditableText)
					.IsReadOnly(true)
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
					.Text_Lambda([Status]() { const FHVPInstallStatus* S = Status(); return S ? FText::FromString(S->Report) : FText::GetEmpty(); })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OutputLog", "Output Log"))
				.ToolTipText(LOCTEXT("OutputLogTip", "The full UAT output is under UATHelper."))
				.OnClicked_Lambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog")); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenFolder", "Open Folder"))
				.IsEnabled_Lambda([Status]() { const FHVPInstallStatus* S = Status(); return S && !S->ArchiveDirectory.IsEmpty(); })
				.OnClicked_Lambda([Status]()
				{
					if (const FHVPInstallStatus* S = Status())
					{
						const FString Flavor = S->ArchiveDirectory / TEXT("Android_ASTC");
						FPlatformProcess::ExploreFolder(*(FPaths::DirectoryExists(Flavor) ? Flavor : S->ArchiveDirectory));
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.Text_Lambda([Status]()
				{
					const FHVPInstallStatus* S = Status();
					return S && S->Outcome != EHVPInstallOutcome::None ? LOCTEXT("RunAgain", "Package and Install Again") : LOCTEXT("Run", "Package and Install");
				})
				.IsEnabled_Lambda([this]() { const TSharedPtr<FHVPPackageInstaller> P = Installer.Pin(); return P && P->CanStart(); })
				.OnClicked_Lambda([this]()
				{
					if (const TSharedPtr<FHVPPackageInstaller> P = Installer.Pin())
					{
						P->Start();
					}
					return FReply::Handled();
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
