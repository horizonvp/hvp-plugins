#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HVPWindowWatch.generated.h"

/**
 * Diagnostic-only (non-shipping) observer for the video popup windows: watches every
 * VATPopupScreen / SteroActor / StereoLayer actor and logs, from the actor's own state, when the
 * window actually appears, moves, scales, hides, or dies — independent of what any system claims
 * to be doing. Log channel: LogHVPWindow.
 */
UCLASS()
class UHVPWindowWatchSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHVPWindowWatchSubsystem, STATGROUP_Tickables); }

private:
	struct FWatched
	{
		TWeakObjectPtr<AActor> Actor;
		FString Label;
		bool bHidden = false;
		FVector RootScale = FVector::OneVector;
		FVector Location = FVector::ZeroVector;
		bool bScaleEpisode = false;
		bool bMoveEpisode = false;
		double LastScaleChange = 0.0;
		double LastMoveChange = 0.0;
		FVector EpisodeStartScale = FVector::OneVector;
		FVector EpisodeStartLocation = FVector::ZeroVector;
	};

	void ScanActors();

	TArray<FWatched> Watched;
	double LastScanSeconds = -100.0;
};
