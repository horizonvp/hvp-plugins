#include "GameflowConfigActor.h"

#include "Engine/World.h"
#include "GameflowBlueprintLibrary.h"
#include "GameflowControllerComponent.h"
#include "TimerManager.h"

AGameflowConfigActor::AGameflowConfigActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void AGameflowConfigActor::BeginPlay()
{
	Super::BeginPlay();

	// Bindings go in unconditionally and first: they have to be in place before anything requests a
	// state, including whoever drives the flow when this actor isn't the one starting it.
	InstallLevelBindings();

	if (!bApplyOnBeginPlay)
	{
		return;
	}
	if (StartDelaySeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(StartDelayTimer, this, &AGameflowConfigActor::Apply, StartDelaySeconds, /*bLoop=*/false);
	}
	else
	{
		Apply();
	}
}

void AGameflowConfigActor::Apply()
{
	InstallLevelBindings();
	if (!StartInitialSequence())
	{
		// No player controller yet, so no gameflow controller to start. Retry every frame until
		// there is one; Tick turns itself back off the moment it succeeds.
		SetActorTickEnabled(true);
	}
}

void AGameflowConfigActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (StartInitialSequence())
	{
		SetActorTickEnabled(false);
	}
}

UGameflowLevelBindingComponent* AGameflowConfigActor::InstallLevelBindings()
{
	if (bBindingsInstalled)
	{
		return TargetBindingComponent;
	}
	if (LevelBindings.IsEmpty())
	{
		bBindingsInstalled = true;
		return nullptr;
	}

	TargetBindingComponent = ResolveBindingComponent();
	if (!TargetBindingComponent)
	{
		UE_LOG(LogHVPGameflow, Error, TEXT("%s: no level-binding component available — %d binding(s) not installed"),
			*GetName(), LevelBindings.Num());
		return nullptr;
	}
	bBindingsInstalled = true;

	for (const TPair<FString, FGameflowLevelBinding>& Pair : LevelBindings)
	{
		const FString Key = Pair.Key.TrimStartAndEnd();
		if (Key.IsEmpty())
		{
			UE_LOG(LogHVPGameflow, Warning, TEXT("%s: level binding with an empty state key ignored"), *GetName());
			continue;
		}

		// TMap<FString> keys compare case-insensitively, matching how the gameflow compares states.
		const bool bAlreadyBound = TargetBindingComponent->Bindings.Contains(Key);
		if (bAlreadyBound && !bOverrideExistingBindings)
		{
			UE_LOG(LogHVPGameflow, Log,
				TEXT("%s: '%s' is already bound on %s — keeping the placed binding"),
				*GetName(), *Key, *GetNameSafe(TargetBindingComponent->GetOwner()));
			continue;
		}

		TargetBindingComponent->Bindings.Add(Key, Pair.Value);
		UE_LOG(LogHVPGameflow, Log, TEXT("%s: bound '%s' -> %s%s"),
			*GetName(), *Key, *Pair.Value.Level.GetAssetName(),
			bAlreadyBound ? TEXT(" (overriding)") : TEXT(""));
	}

	return TargetBindingComponent;
}

bool AGameflowConfigActor::StartInitialSequence()
{
	if (bSequenceStarted)
	{
		return true;
	}

	UGameflowControllerComponent* Controller = UGameflowBlueprintLibrary::FindOrAddController(this);
	if (!Controller)
	{
		return false;
	}
	bSequenceStarted = true;

	TArray<FString> Queue;
	Queue.Reserve(InitialStates.Num());
	for (const FString& State : InitialStates)
	{
		const FString Trimmed = State.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			UE_LOG(LogHVPGameflow, Warning, TEXT("%s: empty entry in Initial States ignored"), *GetName());
			continue;
		}
		Queue.Add(Trimmed);
	}
	if (Queue.IsEmpty())
	{
		return true;
	}

	if (bSkipIfFlowAlreadyStarted && (!Controller->CurrentState.IsEmpty() || !Controller->TransitionQueue.IsEmpty()))
	{
		UE_LOG(LogHVPGameflow, Log,
			TEXT("%s: gameflow already running (state '%s') — initial sequence skipped"),
			*GetName(), *Controller->CurrentState);
		return true;
	}

	// Every binding component has to be listening before the first state is requested, or that
	// state's sublevel never registers its entry requirement and the state is entered early. Their
	// own BeginPlay usually handles it; when the player controller spawned late they are waiting on
	// tick, and tick order is no guarantee. This is.
	for (UGameflowLevelBindingComponent* Component : UGameflowBlueprintLibrary::GetAllGameflowLevelBindings(this))
	{
		Component->EnsureBoundToController();
	}

	UE_LOG(LogHVPGameflow, Log, TEXT("%s: starting initial sequence — %d state(s), first '%s'"),
		*GetName(), Queue.Num(), *Queue[0]);
	Controller->StartTransitionQueue(Queue);
	return true;
}

UGameflowLevelBindingComponent* AGameflowConfigActor::ResolveBindingComponent()
{
	// A component on a Blueprint subclass of this actor is unambiguous authoring — prefer it over
	// whatever the world sweep turns up first.
	if (UGameflowLevelBindingComponent* Own = FindComponentByClass<UGameflowLevelBindingComponent>())
	{
		return Own;
	}
	return UGameflowBlueprintLibrary::FindOrAddLevelBinding(this);
}
