#pragma once

#include "CoreMinimal.h"
#include "HVPPackageInstaller.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * The Package and Install window: a timeline of the run's steps on the left (a circle each, hollow
 * while waiting, pulsing while running, a check, a cross or a mark when it ends, joined by a line),
 * each step's name, state, time and detail to its right, and the overall result at the bottom.
 * Reads the installer's status every frame; it holds no state of its own.
 */
class SHVPPackageInstallPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHVPPackageInstallPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<FHVPPackageInstaller> InInstaller);

private:
	TSharedRef<SWidget> MakeStepRow(int32 Step);
	TSharedRef<SWidget> MakeStepMarker(int32 Step);
	TSharedRef<SWidget> MakeReport();

	const FHVPInstallStep* GetStep(int32 Step) const;
	FSlateColor GetStateColor(EHVPInstallStepState State) const;
	FText GetStepStatusText(int32 Step) const;
	FSlateColor GetOutcomeColor() const;

	TWeakPtr<FHVPPackageInstaller> Installer;
};
