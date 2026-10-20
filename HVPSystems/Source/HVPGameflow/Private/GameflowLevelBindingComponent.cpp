#include "GameflowLevelBindingComponent.h"

#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameflowBlueprintLibrary.h"
#include "GameflowControllerComponent.h"
#include "TimerManager.h"

UGameflowLevelBindingComponent::UGameflowLevelBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGameflowLevelBindingComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureBoundToController();
}

void UGameflowLevelBindingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (EnsureBoundToController())
	{
		SetComponentTickEnabled(false);
	}
}

bool UGameflowLevelBindingComponent::EnsureBoundToController()
{
	if (CachedController)
	{
		return true;
	}
	UGameflowControllerComponent* Controller = UGameflowBlueprintLibrary::FindOrAddController(this);
	if (!Controller)
	{
		return false;
	}
	CachedController = Controller;
	Controller->OnStateChangeRequested.AddDynamic(this, &UGameflowLevelBindingComponent::HandleStateChangeRequested);
	Controller->OnStateChangeDemanded.AddDynamic(this, &UGameflowLevelBindingComponent::HandleStateChangeDemanded);
	Controller->OnStateChange.AddDynamic(this, &UGameflowLevelBindingComponent::HandleStateChange);
	// Bound late, already inside a preload window: we missed that state change.
	PreloadForState(Controller->CurrentState);
	return true;
}

void UGameflowLevelBindingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		for (TPair<FString, FTimerHandle>& Pending : PendingUnloads)
		{
			World->GetTimerManager().ClearTimer(Pending.Value);
		}
		for (TPair<FString, FTimerHandle>& Pending : PendingPreloads)
		{
			World->GetTimerManager().ClearTimer(Pending.Value);
		}
	}
	PendingUnloads.Empty();
	PendingPreloads.Empty();
	if (CachedController)
	{
		CachedController->OnStateChangeRequested.RemoveDynamic(this, &UGameflowLevelBindingComponent::HandleStateChangeRequested);
		CachedController->OnStateChangeDemanded.RemoveDynamic(this, &UGameflowLevelBindingComponent::HandleStateChangeDemanded);
		CachedController->OnStateChange.RemoveDynamic(this, &UGameflowLevelBindingComponent::HandleStateChange);
	}
	Super::EndPlay(EndPlayReason);
}

FName UGameflowLevelBindingComponent::RequirementName(const FString& BindingKey)
{
	return FName(*(TEXT("LevelBinding:") + BindingKey));
}

void UGameflowLevelBindingComponent::AppendBoundLevels(const FGameflowLevelBinding& Binding,
	TArray<TSoftObjectPtr<UWorld>>& OutLevels)
{
	if (!Binding.Level.IsNull())
	{
		OutLevels.Add(Binding.Level);
	}
	for (const TSoftObjectPtr<UWorld>& Extra : Binding.AdditionalLevels)
	{
		if (!Extra.IsNull())
		{
			OutLevels.AddUnique(Extra);
		}
	}
}

FString UGameflowLevelBindingComponent::MakeInstanceKey(const FString& BindingKey,
	const TSoftObjectPtr<UWorld>& Level)
{
	return BindingKey + TEXT("|") + Level.GetAssetName();
}

ULevelStreaming* UGameflowLevelBindingComponent::FindStreamingLevel(const TSoftObjectPtr<UWorld>& Level) const
{
	const UWorld* World = GetWorld();
	if (!World || Level.IsNull())
	{
		return nullptr;
	}
	const FString BoundPackage = Level.GetLongPackageName();
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(BoundPackage))
		{
			return Streaming;
		}
	}
	return nullptr;
}

void UGameflowLevelBindingComponent::HandleStateChangeRequested(const FString& OldState, const FString& NewState)
{
	EnsureLevelsForState(NewState);
}

void UGameflowLevelBindingComponent::HandleStateChangeDemanded(const FString& OldState, const FString& NewState)
{
	EnsureLevelsForState(NewState);
}

void UGameflowLevelBindingComponent::EnsureLevelsForState(const FString& TargetState)
{
	if (!CachedController)
	{
		return;
	}
	for (const TPair<FString, FGameflowLevelBinding>& Pair : Bindings)
	{
		if (!UGameflowControllerComponent::IsStateWithin(TargetState, Pair.Key))
		{
			continue;
		}

		// Heading (back) into this subtree: a scheduled unload no longer applies.
		if (FTimerHandle* Timer = PendingUnloads.Find(Pair.Key))
		{
			GetWorld()->GetTimerManager().ClearTimer(*Timer);
			PendingUnloads.Remove(Pair.Key);
			UE_LOG(LogHVPGameflow, Log, TEXT("LevelBinding '%s': cancelled pending unload (re-entering)"), *Pair.Key);
		}

		// One state may need SEVERAL levels: each gets its own entry requirement, so the
		// state opens only once every one of them is visible.
		TArray<TSoftObjectPtr<UWorld>> Levels;
		AppendBoundLevels(Pair.Value, Levels);
		for (const TSoftObjectPtr<UWorld>& Level : Levels)
		{
			ULevelStreaming* Streaming = FindStreamingLevel(Level);
			if (!Streaming)
			{
				UE_LOG(LogHVPGameflow, Error,
					TEXT("LevelBinding '%s': level '%s' is not a streaming level of this map — cannot gate entry"),
					*Pair.Key, *Level.ToString());
				continue;
			}
			if (Streaming->IsLevelVisible())
			{
				continue;
			}

			const FString InstanceKey = MakeInstanceKey(Pair.Key, Level);
			if (PendingLoads.Contains(InstanceKey))
			{
				continue; // already streaming + gated (a request then a demand, etc.)
			}
			CachedController->RegisterEntryRequirement(RequirementName(InstanceKey));
			PendingLoads.Add(InstanceKey, Level);
			Streaming->OnLevelShown.AddUniqueDynamic(this, &UGameflowLevelBindingComponent::HandleAnyLevelShown);
			Streaming->SetShouldBeLoaded(true);
			Streaming->SetShouldBeVisible(true);
			UE_LOG(LogHVPGameflow, Log, TEXT("LevelBinding '%s': streaming in %s for '%s'"),
				*Pair.Key, *Level.GetAssetName(), *TargetState);
		}
	}
}

void UGameflowLevelBindingComponent::HandleAnyLevelShown()
{
	for (auto It = PendingLoads.CreateIterator(); It; ++It)
	{
		ULevelStreaming* Streaming = FindStreamingLevel(It->Value);
		if (Streaming && Streaming->IsLevelVisible())
		{
			// One tick of grace so every freshly-begun actor's own gameflow bindings exist before
			// the state flips and enter events broadcast.
			ClearRequirementNextTick(It->Key);
			It.RemoveCurrent();
		}
	}
}

void UGameflowLevelBindingComponent::ClearRequirementNextTick(const FString& BindingKey)
{
	GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this, BindingKey]()
		{
			UE_LOG(LogHVPGameflow, Log, TEXT("LevelBinding '%s': level ready"), *BindingKey);
			if (CachedController)
			{
				CachedController->ClearEntryRequirement(RequirementName(BindingKey));
			}
		}));
}

void UGameflowLevelBindingComponent::HandleStateChange(const FString& OldState, const FString& NewState)
{
	for (const TPair<FString, FGameflowLevelBinding>& Pair : Bindings)
	{
		if (!Pair.Value.bUnloadOnLeave
			|| !UGameflowControllerComponent::IsStateWithin(OldState, Pair.Key)
			|| UGameflowControllerComponent::IsStateWithin(NewState, Pair.Key))
		{
			continue;
		}

		const FString Key = Pair.Key;
		if (Pair.Value.UnloadDelaySeconds > 0.0)
		{
			FTimerHandle& Timer = PendingUnloads.FindOrAdd(Key);
			GetWorld()->GetTimerManager().SetTimer(Timer,
				FTimerDelegate::CreateWeakLambda(this, [this, Key]()
				{
					PendingUnloads.Remove(Key);
					UnloadBinding(Key);
				}),
				Pair.Value.UnloadDelaySeconds, /*bLoop=*/false);
			UE_LOG(LogHVPGameflow, Log, TEXT("LevelBinding '%s': unload scheduled in %.1fs"),
				*Key, Pair.Value.UnloadDelaySeconds);

			if (Pair.Value.bHideDuringUnloadDelay)
			{
				HideBinding(Key);
			}
		}
		else
		{
			UnloadBinding(Key);
		}
	}

	// After the unloads, so a level unloaded by this same change is seen as still on its way out.
	PreloadForState(NewState);
}

bool UGameflowLevelBindingComponent::IsLevelNeededElsewhere(const FString& BindingKey,
	const TSoftObjectPtr<UWorld>& Level) const
{
	// Several bindings (and several levels per binding) may point at the SAME level - back-to-back
	// sections served by one scene. Checked at the moment of acting, so delayed unloads are caught too.
	if (!CachedController)
	{
		return false;
	}
	const FString BoundPackage = Level.GetLongPackageName();
	for (const TPair<FString, FGameflowLevelBinding>& Other : Bindings)
	{
		if (Other.Key == BindingKey
			|| !UGameflowControllerComponent::IsStateWithin(CachedController->CurrentState, Other.Key))
		{
			continue;
		}
		TArray<TSoftObjectPtr<UWorld>> OtherLevels;
		AppendBoundLevels(Other.Value, OtherLevels);
		for (const TSoftObjectPtr<UWorld>& OtherLevel : OtherLevels)
		{
			if (OtherLevel.GetLongPackageName() == BoundPackage)
			{
				UE_LOG(LogHVPGameflow, Log,
					TEXT("LevelBinding '%s': keeping %s — binding '%s' still covers current state '%s'"),
					*BindingKey, *Level.GetAssetName(), *Other.Key, *CachedController->CurrentState);
				return true;
			}
		}
	}
	return false;
}

void UGameflowLevelBindingComponent::HideBinding(const FString& BindingKey)
{
	const FGameflowLevelBinding* Binding = Bindings.Find(BindingKey);
	if (!Binding)
	{
		return;
	}

	TArray<TSoftObjectPtr<UWorld>> Levels;
	AppendBoundLevels(*Binding, Levels);
	for (const TSoftObjectPtr<UWorld>& Level : Levels)
	{
		ULevelStreaming* Streaming = FindStreamingLevel(Level);
		if (!Streaming || IsLevelNeededElsewhere(BindingKey, Level))
		{
			continue;
		}
		// A load that never finished can't hold the (long-gone) transition open.
		const FString InstanceKey = MakeInstanceKey(BindingKey, Level);
		if (PendingLoads.Remove(InstanceKey) > 0 && CachedController)
		{
			CachedController->ClearEntryRequirement(RequirementName(InstanceKey));
		}
		Streaming->SetShouldBeVisible(false);
		UE_LOG(LogHVPGameflow, Log, TEXT("LevelBinding '%s': hid %s until the unload"),
			*BindingKey, *Level.GetAssetName());
	}
}

void UGameflowLevelBindingComponent::PreloadForState(const FString& State)
{
	for (const TPair<FString, FGameflowLevelBinding>& Pair : Bindings)
	{
		if (!Pair.Value.PreloadDuringState.IsEmpty()
			&& UGameflowControllerComponent::IsStateWithin(State, Pair.Value.PreloadDuringState)
			&& !PendingPreloads.Contains(Pair.Key))
		{
			TryPreload(Pair.Key);
		}
	}
}

void UGameflowLevelBindingComponent::TryPreload(const FString& BindingKey)
{
	const FGameflowLevelBinding* Binding = Bindings.Find(BindingKey);
	if (!Binding || !CachedController)
	{
		StopPreloadRetry(BindingKey);
		return;
	}

	// Out of the window, or already into the bound subtree (where the entry path owns the load).
	const FString& Current = CachedController->CurrentState;
	if (!UGameflowControllerComponent::IsStateWithin(Current, Binding->PreloadDuringState)
		|| UGameflowControllerComponent::IsStateWithin(Current, BindingKey))
	{
		StopPreloadRetry(BindingKey);
		return;
	}

	bool bWaiting = false;
	TArray<TSoftObjectPtr<UWorld>> Levels;
	AppendBoundLevels(*Binding, Levels);
	for (const TSoftObjectPtr<UWorld>& Level : Levels)
	{
		ULevelStreaming* Streaming = FindStreamingLevel(Level);
		if (!Streaming)
		{
			UE_LOG(LogHVPGameflow, Error, TEXT("LevelBinding '%s': level '%s' is not a streaming level of this map — cannot preload"),
				*BindingKey, *Level.ToString());
			continue;
		}

		if (Streaming->ShouldBeLoaded() && !PendingUnloads.Contains(BindingKey))
		{
			continue;	// already loaded or on its way in
		}

		// The last session's copy must be fully gone first: reloading a level that never finished
		// unloading would hand back its actors with their old state.
		if (PendingUnloads.Contains(BindingKey) || Streaming->GetLevelStreamingState() != ELevelStreamingState::Unloaded)
		{
			bWaiting = true;
			continue;
		}

		Streaming->SetShouldBeLoaded(true);
		UE_LOG(LogHVPGameflow, Log, TEXT("LevelBinding '%s': preloading %s (hidden) during '%s'"),
			*BindingKey, *Level.GetAssetName(), *Current);
	}

	if (!bWaiting)
	{
		StopPreloadRetry(BindingKey);
	}
	else if (!PendingPreloads.Contains(BindingKey))
	{
		UE_LOG(LogHVPGameflow, Log, TEXT("LevelBinding '%s': preload waiting for a previous copy to finish unloading"), *BindingKey);
		FTimerHandle& Timer = PendingPreloads.Add(BindingKey);
		const FString Key = BindingKey;
		GetWorld()->GetTimerManager().SetTimer(Timer,
			FTimerDelegate::CreateWeakLambda(this, [this, Key]() { TryPreload(Key); }),
			0.25f, /*bLoop=*/true);
	}
}

void UGameflowLevelBindingComponent::StopPreloadRetry(const FString& BindingKey)
{
	if (FTimerHandle* Timer = PendingPreloads.Find(BindingKey))
	{
		GetWorld()->GetTimerManager().ClearTimer(*Timer);
		PendingPreloads.Remove(BindingKey);
	}
}

void UGameflowLevelBindingComponent::UnloadBinding(const FString& BindingKey)
{
	const FGameflowLevelBinding* Binding = Bindings.Find(BindingKey);
	if (!Binding)
	{
		return;
	}

	TArray<TSoftObjectPtr<UWorld>> Levels;
	AppendBoundLevels(*Binding, Levels);
	for (const TSoftObjectPtr<UWorld>& Level : Levels)
	{
		ULevelStreaming* Streaming = FindStreamingLevel(Level);
		// Book-end guard: leaving one section must not unload a level another binding still needs
		// for the CURRENT state.
		if (!Streaming || IsLevelNeededElsewhere(BindingKey, Level))
		{
			continue;
		}

		// A load that never finished can't hold the (long-gone) transition open.
		const FString InstanceKey = MakeInstanceKey(BindingKey, Level);
		if (PendingLoads.Remove(InstanceKey) > 0 && CachedController)
		{
			CachedController->ClearEntryRequirement(RequirementName(InstanceKey));
		}
		Streaming->SetShouldBeVisible(false);
		Streaming->SetShouldBeLoaded(false);
		UE_LOG(LogHVPGameflow, Log, TEXT("LevelBinding '%s': unloading %s"),
			*BindingKey, *Level.GetAssetName());
	}
}
