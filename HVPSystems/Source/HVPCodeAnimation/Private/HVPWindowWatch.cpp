#include "HVPWindowWatch.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogHVPWindow, Log, All);

bool UHVPWindowWatchSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
#endif
}

void UHVPWindowWatchSubsystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastScanSeconds > 1.0)
	{
		LastScanSeconds = Now;
		ScanActors();
	}

	for (int32 i = Watched.Num() - 1; i >= 0; --i)
	{
		FWatched& W = Watched[i];
		AActor* Actor = W.Actor.Get();
		if (!Actor)
		{
			UE_LOG(LogHVPWindow, Warning, TEXT("[Window] %s DESTROYED"), *W.Label);
			Watched.RemoveAt(i);
			continue;
		}

		const bool bHidden = Actor->IsHidden();
		if (bHidden != W.bHidden)
		{
			UE_LOG(LogHVPWindow, Log, TEXT("[Window] %s %s"), *W.Label, bHidden ? TEXT("HIDDEN") : TEXT("SHOWN"));
			W.bHidden = bHidden;
		}

		const USceneComponent* Root = Actor->GetRootComponent();
		if (!Root)
		{
			continue;
		}
		const FVector Scale = Root->GetRelativeScale3D();
		const FVector Location = Actor->GetActorLocation();

		if (!Scale.Equals(W.RootScale, 0.001))
		{
			if (!W.bScaleEpisode)
			{
				W.bScaleEpisode = true;
				W.EpisodeStartScale = W.RootScale;
				UE_LOG(LogHVPWindow, Log, TEXT("[Window] %s SCALE-CHANGE begins from %s"),
					*W.Label, *W.RootScale.ToCompactString());
			}
			W.RootScale = Scale;
			W.LastScaleChange = Now;
		}
		else if (W.bScaleEpisode && Now - W.LastScaleChange > 0.4)
		{
			W.bScaleEpisode = false;
			UE_LOG(LogHVPWindow, Log, TEXT("[Window] %s SCALE-CHANGE settled at %s (from %s)"),
				*W.Label, *Scale.ToCompactString(), *W.EpisodeStartScale.ToCompactString());
		}

		if (!Location.Equals(W.Location, 0.5))
		{
			if (!W.bMoveEpisode)
			{
				W.bMoveEpisode = true;
				W.EpisodeStartLocation = W.Location;
				UE_LOG(LogHVPWindow, Log, TEXT("[Window] %s MOVE begins from %s"),
					*W.Label, *W.Location.ToCompactString());
			}
			W.Location = Location;
			W.LastMoveChange = Now;
		}
		else if (W.bMoveEpisode && Now - W.LastMoveChange > 0.4)
		{
			W.bMoveEpisode = false;
			UE_LOG(LogHVPWindow, Log, TEXT("[Window] %s MOVE settled at %s (from %s)"),
				*W.Label, *Location.ToCompactString(), *W.EpisodeStartLocation.ToCompactString());
		}
	}
}

void UHVPWindowWatchSubsystem::ScanActors()
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		const FString ClassName = Actor->GetClass()->GetName();
		if (!ClassName.Contains(TEXT("VATPopupScreen"))
			&& !ClassName.Contains(TEXT("TextPopupScreen"))
			&& !ClassName.Contains(TEXT("SteroActor"))
			&& !ClassName.Contains(TEXT("StereoLayer")))
		{
			continue;
		}
		const bool bAlready = Watched.ContainsByPredicate(
			[Actor](const FWatched& W) { return W.Actor.Get() == Actor; });
		if (bAlready)
		{
			continue;
		}
		FWatched W;
		W.Actor = Actor;
		W.Label = FString::Printf(TEXT("%s(%s)"), *Actor->GetActorNameOrLabel(), *Actor->GetName());
		W.bHidden = Actor->IsHidden();
		if (const USceneComponent* Root = Actor->GetRootComponent())
		{
			W.RootScale = Root->GetRelativeScale3D();
		}
		W.Location = Actor->GetActorLocation();
		UE_LOG(LogHVPWindow, Log, TEXT("[Window] watching %s (hidden=%d scale=%s owner=%s attachedTo=%s)"),
			*W.Label, W.bHidden ? 1 : 0, *W.RootScale.ToCompactString(),
			*GetNameSafe(Actor->GetOwner()),
			Actor->GetAttachParentActor() ? *Actor->GetAttachParentActor()->GetActorNameOrLabel() : TEXT("none"));
		Watched.Add(MoveTemp(W));
	}
}
