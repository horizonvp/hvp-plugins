#include "CodeAnimationComponent.h"
#include "CodeAnimationsComponent.h"
#include "ShellDelegateUtil.h"
#include "GameflowControllerComponent.h"
#include "GameflowBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "GameFramework/GameStateBase.h"
#include "MediaPlayer.h"
#include "FileMediaSource.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogHVPCodeAnimation);

UCodeAnimationComponent::UCodeAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UCodeAnimationComponent::BeginPlay()
{
	Super::BeginPlay();

	// The player controller (and therefore the gameflow controller) may not exist yet for
	// early-streamed actors; TickComponent retries until the bind succeeds.
	TryBindController();
}

void UCodeAnimationComponent::TryBindController()
{
	if (CachedController)
	{
		return;
	}
	if (UGameflowControllerComponent* Controller = UGameflowBlueprintLibrary::FindOrAddController(this))
	{
		CachedController = Controller;
		Controller->OnStateChange.AddDynamic(this, &UCodeAnimationComponent::HandleStateChange);
		Controller->OnStateChangeRequested.AddDynamic(this, &UCodeAnimationComponent::HandleStateChangeRequested);
		Controller->OnStateChangeInterrupted.AddDynamic(this, &UCodeAnimationComponent::HandleStateChangeInterrupted);

		// Late-join catch-up: an actor streamed in AFTER its state was entered (level bound to a
		// state, or a demand that outran the load) missed the enter broadcast — play now instead.
		// ResetIfPlaying=false keeps this a no-op for anything already started explicitly.
		if (bPlayOnEnter && !bPlayingAnimation && AnyEntryContains(Controller->CurrentState))
		{
			UE_LOG(LogHVPCodeAnimation, Log, TEXT("%s late-join: state '%s' already active on bind"),
				*GetReadableName(), *Controller->CurrentState);
			PlayAnimation(/*Reverse=*/false, /*ResetIfPlaying=*/false);
		}
	}
}

void UCodeAnimationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EndDelayTimer);
	}
	if (CachedController)
	{
		CachedController->OnStateChange.RemoveDynamic(this, &UCodeAnimationComponent::HandleStateChange);
		CachedController->OnStateChangeRequested.RemoveDynamic(this, &UCodeAnimationComponent::HandleStateChangeRequested);
		CachedController->OnStateChangeInterrupted.RemoveDynamic(this, &UCodeAnimationComponent::HandleStateChangeInterrupted);
	}
	Super::EndPlay(EndPlayReason);
}

void UCodeAnimationComponent::SetForwardTarget(UCodeAnimationsComponent* Manager, const FString& AnimationName)
{
	ForwardTarget = Manager;
	ForwardName = AnimationName;
}

// --- Gameflow handlers ---

bool UCodeAnimationComponent::MatchesActiveState(const FString& State) const
{
	return AnyEntryContains(State);
}

bool UCodeAnimationComponent::AnyEntryContains(const FString& State) const
{
	// Subtree-aware: an entry "PMN" also covers "PMN/Step1". A flat entry degenerates to the
	// original exact match.
	for (const FString& Active : ActiveStates)
	{
		if (UGameflowControllerComponent::IsStateWithin(State, Active))
		{
			return true;
		}
	}
	// The original Blueprint merged its deprecated single Active State into the set on BeginPlay.
	return UGameflowControllerComponent::IsStateWithin(State, ActiveStateDeprecated);
}

bool UCodeAnimationComponent::CrossesIntoActive(const FString& OldState, const FString& NewState) const
{
	// Per-entry so flat behavior is unchanged: a flat entry can never contain both sides of a
	// transition, so with flat states this is just "the new state matches" — the original rule.
	auto EntryCrossesIn = [&OldState, &NewState](const FString& Entry)
	{
		return UGameflowControllerComponent::IsStateWithin(NewState, Entry)
			&& !UGameflowControllerComponent::IsStateWithin(OldState, Entry);
	};
	for (const FString& Active : ActiveStates)
	{
		if (EntryCrossesIn(Active))
		{
			return true;
		}
	}
	return !ActiveStateDeprecated.IsEmpty() && EntryCrossesIn(ActiveStateDeprecated);
}

bool UCodeAnimationComponent::CrossesOutOfActive(const FString& OldState, const FString& NewState) const
{
	auto EntryCrossesOut = [&OldState, &NewState](const FString& Entry)
	{
		return UGameflowControllerComponent::IsStateWithin(OldState, Entry)
			&& !UGameflowControllerComponent::IsStateWithin(NewState, Entry);
	};
	for (const FString& Active : ActiveStates)
	{
		if (EntryCrossesOut(Active))
		{
			return true;
		}
	}
	return !ActiveStateDeprecated.IsEmpty() && EntryCrossesOut(ActiveStateDeprecated);
}

void UCodeAnimationComponent::HandleStateChange(const FString& OldState, const FString& NewState)
{
	// Exit side of a DEMANDED transition: the request-side machinery (exit authority, played
	// outro) never ran, so a configured exit would silently not happen and leave its listeners
	// mid-scene. Snap it to the end pose instead. A requested exit that already played marks
	// itself handled; the flag is consumed on every executed change either way.
	const bool bAlreadyHandled = bExitHandledByRequest;
	bExitHandledByRequest = false;
	if (bSnapExitOnDemand && !bAlreadyHandled && (bPlayOnExit || bReverseOnExit))
	{
		const bool bExiting = bPlayOnExitToSubstate
			? AnyEntryContains(OldState) && !OldState.Equals(NewState, ESearchCase::IgnoreCase)
			: CrossesOutOfActive(OldState, NewState);
		if (bExiting)
		{
			SnapExitToEnd();
		}
	}

	if (bPlayOnEnter && CrossesIntoActive(OldState, NewState))
	{
		PlayAnimation(/*Reverse=*/false, /*ResetIfPlaying=*/true);
	}
}

void UCodeAnimationComponent::SnapExitToEnd()
{
	UE_LOG(LogHVPCodeAnimation, Log, TEXT("%s: demanded through — snapping exit to its end pose"),
		*GetReadableName());
	// An instant playthrough: PlayAnimation broadcasts Start (consumers run their takeover),
	// CompleteAnimation lands every step's final tick at the exit's end value and broadcasts
	// End. Completed as INTERRUPTED so there is no end delay, no auto-continue (the demand
	// already advanced the queue), and — as with any reversed/instant playthrough — no media.
	PlayAnimation(bReverseOnExit, /*ResetIfPlaying=*/true);
	CompleteAnimation(/*bInterrupted=*/true);
}

void UCodeAnimationComponent::HandleStateChangeRequested(const FString& OldState, const FString& NewState)
{
	if (!(bPlayOnExit || bReverseOnExit))
	{
		return;
	}
	// Default: only a transition that leaves an active subtree entirely is an exit; moving to a
	// substate ("PMN" -> "PMN/Step1") is not. bPlayOnExitToSubstate widens that to any state
	// change while active.
	const bool bExiting = bPlayOnExitToSubstate
		? AnyEntryContains(OldState) && !OldState.Equals(NewState, ESearchCase::IgnoreCase)
		: CrossesOutOfActive(OldState, NewState);
	if (!bExiting)
	{
		return;
	}

	if (UGameflowControllerComponent* Controller = GetGameflowController())
	{
		Controller->RegisterExitAuthority(ExitAuthorityName());
		bHoldingExitAuthority = true;
	}
	bPlayingExitAnimation = true;
	bExitHandledByRequest = true;
	PlayAnimation(bReverseOnExit, /*ResetIfPlaying=*/true);
}

void UCodeAnimationComponent::HandleStateChangeInterrupted(const FString& OldState, const FString& NewState)
{
	if (!bPlayingAnimation)
	{
		return;
	}
	UE_LOG(LogHVPCodeAnimation, Log, TEXT("Interrupted %s (%s)"), *GetReadableName(), *OldState);

	if (bFinalTickOnInterrupt)
	{
		CompleteAnimation(/*bInterrupted=*/true);
	}
	else
	{
		bPlayingAnimation = false;
		BroadcastEnd(/*bInterrupted=*/true);
	}

	// The controller discards all authorities when a demand interrupts a transition.
	bHoldingExitAuthority = false;
	bPlayingExitAnimation = false;
}

// --- Control ---

void UCodeAnimationComponent::PlayAnimation(bool Reverse, bool ResetIfPlaying)
{
	if (bPlayingAnimation && !ResetIfPlaying)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EndDelayTimer);
	}

	StepsStarted.Reset();
	StepsFinished.Reset();
	MoviesTriggered.Reset();
	SoundsTriggered.Reset();

	bReversed = Reverse;
	bHasCompleted = false;
	bPlayingAnimation = true;
	bMediaLengthResolved = false;
	PlaythroughStartTime = NowSeconds();
	CurrentPlaythroughSeconds = 0.0;
	CurrentPlaythroughAlpha = bReversed ? 1.0 : 0.0;
	// Nothing has been opened yet -- TriggerMovieStep runs from the first TickMedia -- so every
	// player still reports a zero duration and this is the no-media fallback, NOT the real length.
	// TickComponent grows it as the durations arrive; see bMediaLengthResolved.
	TotalSecondsWithMedia = ComputeFinishSeconds() + EndDelaySeconds;

	BroadcastStart();
}

void UCodeAnimationComponent::StopAnimation(bool FireEndEvent)
{
	if (!bPlayingAnimation)
	{
		return;
	}
	UE_LOG(LogHVPCodeAnimation, Log, TEXT("Stopping %s"), *GetReadableName());
	bPlayingAnimation = false;
	if (bHoldingExitAuthority)
	{
		ReleaseExitAuthority();
	}
	bPlayingExitAnimation = false;
	if (FireEndEvent)
	{
		BroadcastEnd(/*bInterrupted=*/false);
	}
}

bool UCodeAnimationComponent::IsStepPlaying(const FString& StepName) const
{
	const FName Key(*StepName);
	return StepsStarted.Contains(Key) && !StepsFinished.Contains(Key);
}

bool UCodeAnimationComponent::IsStepFinished(const FString& StepName) const
{
	return StepsFinished.Contains(FName(*StepName));
}

void UCodeAnimationComponent::ContinueToNextState()
{
	if (UGameflowControllerComponent* Controller = GetGameflowController())
	{
		Controller->ContinueToNextState();
	}
}

UGameflowControllerComponent* UCodeAnimationComponent::GetGameflowController() const
{
	if (CachedController)
	{
		return CachedController;
	}
	return UGameflowBlueprintLibrary::FindOrAddController(this);
}

// --- Playback ---

double UCodeAnimationComponent::NowSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

void UCodeAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TryBindController();

	if (!bPlayingAnimation)
	{
		return;
	}

	const double Elapsed = NowSeconds() - PlaythroughStartTime - StartDelaySeconds;
	if (Elapsed < 0.0)
	{
		return;
	}
	CurrentPlaythroughSeconds = Elapsed;

	const double ForwardProgress = TotalSeconds > 0.0
		? FMath::Clamp(Elapsed / TotalSeconds, 0.0, 1.0)
		: 1.0;
	CurrentPlaythroughAlpha = bReversed ? 1.0 - ForwardProgress : ForwardProgress;

	TickSteps(CurrentPlaythroughAlpha);
	if (!bReversed)
	{
		TickMedia(ForwardProgress);
	}

	// A movie only knows its duration once its player has opened, which happens asynchronously
	// after TriggerMovieStep -- so the published length has to be re-evaluated as durations arrive
	// rather than computed once up front. Consumers poll this to time their own teardown
	// (VATPopupScreen_BP fades its stereo layer at TotalSecondsWithMedia - 0.7), and one that reads
	// it while it still holds the no-media fallback fades out mid-clip: measured on device as the
	// quad vanishing 1.3s into a 33.5s film that then played on invisibly behind it.
	//
	// Max rather than assign, because GetDuration only ever goes 0 -> real and a player closed
	// mid-playthrough must not shrink a length someone has already read. This restores the original
	// Blueprint's Tick Movie Step, which MAX-accumulated the same expression every tick; the port
	// computed it once in PlayAnimation and then not again until the step timeline ended, which is
	// the window that let consumers read the fallback.
	if (!bReversed && !bMediaLengthResolved)
	{
		TotalSecondsWithMedia = FMath::Max(TotalSecondsWithMedia, ComputeFinishSeconds() + EndDelaySeconds);
		bMediaLengthResolved = MoviesTriggered.Num() == MovieSteps.Num() && !IsWaitingOnUnpreparedMovie();
	}

	if (ForwardProgress >= 1.0)
	{
		// The step timeline is done, but a forward playthrough holds the state open until its
		// longest sound/movie step has also finished (the Blueprint's "Total Seconds with Media") —
		// otherwise VO gets cut off by the auto-continue. Reversed exits don't play media and
		// complete with the timeline.
		const double FinishSeconds = bReversed ? 0.0 : ComputeFinishSeconds();
		if (bReversed)
		{
			// Forward playthroughs maintain this above, where it can only grow -- assigning here
			// would let it drop back to the fallback after a player closed.
			TotalSecondsWithMedia = FinishSeconds + EndDelaySeconds;
		}
		if (bReversed || (NowSeconds() - PlaythroughStartTime >= FinishSeconds && !IsWaitingOnUnpreparedMovie()))
		{
			CompleteAnimation(/*bInterrupted=*/false);
		}
	}
}

double UCodeAnimationComponent::ScaleStepAlpha(const FAnimationStep& Step, double Alpha) const
{
	const double Local = FMath::GetMappedRangeValueClamped(
		FVector2D(Step.StartPercent, Step.EndPercent), FVector2D(0.0, 1.0), Alpha);
	return UKismetMathLibrary::Ease(0.0f, 1.0f, static_cast<float>(Local), EaseFunction, BlendExp);
}

void UCodeAnimationComponent::TickSteps(double Alpha)
{
	for (const FAnimationStep& Step : AnimationSteps)
	{
		if (StepsFinished.Contains(Step.Name))
		{
			continue;
		}

		const double RangeMin = FMath::Min(Step.StartPercent, Step.EndPercent);
		const double RangeMax = FMath::Max(Step.StartPercent, Step.EndPercent);
		const bool bInRange = Alpha >= RangeMin && Alpha <= RangeMax;
		const bool bStarted = StepsStarted.Contains(Step.Name);
		const FString StepName = Step.Name.ToString();

		if (bInRange)
		{
			if (!bStarted)
			{
				StepsStarted.Add(Step.Name);
				BroadcastStepStart(StepName);
			}
			BroadcastStepTick(StepName, Alpha, ScaleStepAlpha(Step, Alpha));
			continue;
		}

		// Passed beyond the step's range in the direction of travel (possibly skipping the whole
		// range between two ticks): guarantee the step its start, one final tick, and its end.
		const bool bPassed = bReversed ? (Alpha < RangeMin) : (Alpha > RangeMax);
		if (bPassed)
		{
			if (!bStarted)
			{
				StepsStarted.Add(Step.Name);
				BroadcastStepStart(StepName);
			}
			FinishStep(Step, bReversed ? 0.0 : 1.0);
		}
	}
}

void UCodeAnimationComponent::FinishStep(const FAnimationStep& Step, double FinalScaledAlpha)
{
	const FString StepName = Step.Name.ToString();
	BroadcastStepTick(StepName, CurrentPlaythroughAlpha, FinalScaledAlpha);
	StepsFinished.Add(Step.Name);
	BroadcastStepEnd(StepName);
}

void UCodeAnimationComponent::TickMedia(double ForwardProgress)
{
	for (int32 i = 0; i < MovieSteps.Num(); ++i)
	{
		if (!MoviesTriggered.Contains(i) && ForwardProgress >= MovieSteps[i].StartPositionPercent)
		{
			MoviesTriggered.Add(i);
			TriggerMovieStep(MovieSteps[i]);
		}
	}
	for (int32 i = 0; i < SoundSteps.Num(); ++i)
	{
		if (!SoundsTriggered.Contains(i) && ForwardProgress >= SoundSteps[i].StartPositionPercent)
		{
			SoundsTriggered.Add(i);
			TriggerSoundStep(SoundSteps[i]);
		}
	}
}

void UCodeAnimationComponent::TriggerMovieStep(const FMovieStep& Step)
{
	if (Step.Player && Step.Movie)
	{
		Step.Player->OpenSource(Step.Movie);
	}
	const FString Name = Step.Name.ToString();
	StartMovie.Broadcast(Name, Step.Movie, Step.Player);
	HVPShellDelegates::Fire(this, TEXT("Start Movie"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Name"), Name);
		HVPShellDelegates::SetObj(Sig, Parms, TEXT("Movie"), Step.Movie);
		HVPShellDelegates::SetObj(Sig, Parms, TEXT("Player"), Step.Player);
	});
}

void UCodeAnimationComponent::TriggerSoundStep(const FSoundStep& Step)
{
	AActor* Target = Step.Player ? Step.Player.Get() : GetOwner();
	if (Target && Step.Sound)
	{
		UAudioComponent* AudioComponent = nullptr;
		TInlineComponentArray<UAudioComponent*> AudioComponents(Target);
		for (UAudioComponent* Candidate : AudioComponents)
		{
			if (Step.AudioComponentTag.IsNone() || Candidate->ComponentHasTag(Step.AudioComponentTag))
			{
				AudioComponent = Candidate;
				break;
			}
		}
		if (AudioComponent)
		{
			UE_LOG(LogHVPCodeAnimation, Log, TEXT("Playing sound %s"), *Step.Sound->GetName());
			AudioComponent->SetSound(Step.Sound);
			AudioComponent->Play();
		}
		else
		{
			UE_LOG(LogHVPCodeAnimation, Warning, TEXT("Sound step '%s': no AudioComponent tagged '%s' on %s"),
				*Step.Name.ToString(), *Step.AudioComponentTag.ToString(), *Target->GetName());
		}
	}
	const FString Name = Step.Name.ToString();
	StartSound.Broadcast(Name, Step.Sound);
	HVPShellDelegates::Fire(this, TEXT("Start Sound"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Name"), Name);
		HVPShellDelegates::SetObj(Sig, Parms, TEXT("Sound"), Step.Sound);
	});
}

bool UCodeAnimationComponent::IsWaitingOnUnpreparedMovie() const
{
	// A triggered movie whose player hasn't opened yet reports duration 0 — completing then would
	// cut it off before it starts. Hold until it either prepares or errors out.
	for (const int32 Index : MoviesTriggered)
	{
		if (MovieSteps.IsValidIndex(Index))
		{
			const FMovieStep& Step = MovieSteps[Index];
			if (Step.Player && Step.Movie
				&& Step.Player->GetDuration().GetTotalSeconds() <= 0.0
				&& !Step.Player->HasError())
			{
				return true;
			}
		}
	}
	return false;
}

void UCodeAnimationComponent::CompleteAnimation(bool bInterrupted)
{
	CurrentPlaythroughAlpha = bReversed ? 0.0 : 1.0;

	for (const FAnimationStep& Step : AnimationSteps)
	{
		if (StepsFinished.Contains(Step.Name))
		{
			continue;
		}
		if (!StepsStarted.Contains(Step.Name))
		{
			StepsStarted.Add(Step.Name);
			BroadcastStepStart(Step.Name.ToString());
		}
		FinishStep(Step, bReversed ? 0.0 : 1.0);
	}

	bPlayingAnimation = false;
	bHasCompleted = true;
	BroadcastEnd(bInterrupted);

	if (bInterrupted)
	{
		return;
	}

	if (EndDelaySeconds > 0.0)
	{
		GetWorld()->GetTimerManager().SetTimer(
			EndDelayTimer, this, &UCodeAnimationComponent::HandleEndDelayElapsed, static_cast<float>(EndDelaySeconds), false);
	}
	else
	{
		HandleEndDelayElapsed();
	}
}

void UCodeAnimationComponent::HandleEndDelayElapsed()
{
	if (bHoldingExitAuthority)
	{
		ReleaseExitAuthority();
		bPlayingExitAnimation = false;
	}
	else if (bAutoContinueOnFinish)
	{
		ContinueToNextState();
	}
}

void UCodeAnimationComponent::ReleaseExitAuthority()
{
	bHoldingExitAuthority = false;
	if (UGameflowControllerComponent* Controller = GetGameflowController())
	{
		Controller->ClearExitAuthority(ExitAuthorityName());
	}
}

FName UCodeAnimationComponent::ExitAuthorityName() const
{
	const AActor* Owner = GetOwner();
	return FName(*FString::Printf(TEXT("%s.%s"), Owner ? *Owner->GetName() : TEXT("None"), *GetName()));
}

double UCodeAnimationComponent::ComputeFinishSeconds() const
{
	double Finish = StartDelaySeconds + TotalSeconds;
	for (const FMovieStep& Step : MovieSteps)
	{
		// A player only knows its duration once the source has opened (which happens when the
		// step triggers), hence per-tick re-evaluation while waiting to complete.
		const double MediaTail = Step.Player ? Step.Player->GetDuration().GetTotalSeconds() : 0.0;
		Finish = FMath::Max(Finish, StartDelaySeconds + Step.StartPositionPercent * TotalSeconds + MediaTail);
	}
	for (const FSoundStep& Step : SoundSteps)
	{
		double SoundTail = Step.Sound ? static_cast<double>(Step.Sound->GetDuration()) : 0.0;
		if (SoundTail >= INDEFINITELY_LOOPING_DURATION)
		{
			// A looping sound must not hold the state open forever.
			SoundTail = 0.0;
		}
		Finish = FMath::Max(Finish, StartDelaySeconds + Step.StartPositionPercent * TotalSeconds + SoundTail);
	}
	return Finish;
}

// --- Broadcast: native delegate + BP shell dispatcher + manager forwarding ---

void UCodeAnimationComponent::BroadcastStart()
{
	UE_LOG(LogHVPCodeAnimation, Log, TEXT("START ANIMATION %s (gameflow state '%s')"),
		*GetReadableName(), CachedController ? *CachedController->CurrentState : TEXT("?"));
	StartAnimation.Broadcast(this);
	HVPShellDelegates::Fire(this, TEXT("Start Animation"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetObj(Sig, Parms, TEXT("Component"), this);
	});
	if (UCodeAnimationsComponent* Manager = ForwardTarget.Get())
	{
		Manager->NotifyStart(ForwardName);
	}
}

void UCodeAnimationComponent::BroadcastEnd(bool bInterrupted)
{
	UE_LOG(LogHVPCodeAnimation, Log, TEXT("END ANIMATION %s at %.2fs (interrupted=%d, gameflow state '%s')"),
		*GetReadableName(), CurrentPlaythroughSeconds, bInterrupted ? 1 : 0,
		CachedController ? *CachedController->CurrentState : TEXT("?"));
	EndAnimation.Broadcast(this, bInterrupted);
	HVPShellDelegates::Fire(this, TEXT("End Animation"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetObj(Sig, Parms, TEXT("Component"), this);
		HVPShellDelegates::SetBool(Sig, Parms, TEXT("Inturrupted"), bInterrupted);
	});
	if (UCodeAnimationsComponent* Manager = ForwardTarget.Get())
	{
		Manager->NotifyEnd(ForwardName, bInterrupted);
	}
}

void UCodeAnimationComponent::BroadcastStepStart(const FString& StepName)
{
	StartAnimationStep.Broadcast(this, StepName);
	HVPShellDelegates::Fire(this, TEXT("Start Animation Step"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetObj(Sig, Parms, TEXT("Component"), this);
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Name"), StepName);
	});
	if (UCodeAnimationsComponent* Manager = ForwardTarget.Get())
	{
		Manager->NotifyStepStart(ForwardName, StepName);
	}
}

void UCodeAnimationComponent::BroadcastStepTick(const FString& StepName, double Alpha, double ScaledAlpha)
{
	TickAnimationStep.Broadcast(this, StepName, Alpha, ScaledAlpha);
	HVPShellDelegates::Fire(this, TEXT("Tick Animation Step"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetObj(Sig, Parms, TEXT("Component"), this);
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Name"), StepName);
		HVPShellDelegates::SetDouble(Sig, Parms, TEXT("Alpha"), Alpha);
		HVPShellDelegates::SetDouble(Sig, Parms, TEXT("Scaled Alpha"), ScaledAlpha);
	});
	if (UCodeAnimationsComponent* Manager = ForwardTarget.Get())
	{
		Manager->NotifyStepTick(ForwardName, StepName, Alpha, ScaledAlpha);
	}
}

void UCodeAnimationComponent::BroadcastStepEnd(const FString& StepName)
{
	EndAnimationStep.Broadcast(this, StepName);
	HVPShellDelegates::Fire(this, TEXT("End Animation Step"), [&](UFunction* Sig, uint8* Parms)
	{
		HVPShellDelegates::SetObj(Sig, Parms, TEXT("Component"), this);
		HVPShellDelegates::SetStr(Sig, Parms, TEXT("Name"), StepName);
	});
	if (UCodeAnimationsComponent* Manager = ForwardTarget.Get())
	{
		Manager->NotifyStepEnd(ForwardName, StepName);
	}
}
