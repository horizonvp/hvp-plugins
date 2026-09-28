#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CodeAnimationTypes.h"
#include "Kismet/KismetMathLibrary.h"
#include "CodeAnimationComponent.generated.h"

class UGameflowControllerComponent;
class UCodeAnimationComponent;
class UCodeAnimationsComponent;
class UFileMediaSource;
class UMediaPlayer;
class USoundBase;

HVPCODEANIMATION_API DECLARE_LOG_CATEGORY_EXTERN(LogHVPCodeAnimation, Log, All);

// Signatures mirror the original Blueprint dispatchers exactly (String names, double alphas,
// leading Component pin) so consumer bindings keep compiling after the migration.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCodeAnimationStart, UCodeAnimationComponent*, Component);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodeAnimationEnd, UCodeAnimationComponent*, Component, bool, Interrupted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodeAnimationStepEvent, UCodeAnimationComponent*, Component, const FString&, Name);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FCodeAnimationStepTick, UCodeAnimationComponent*, Component, const FString&, Name, double, Alpha, double, ScaledAlpha);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCodeAnimationMovie, const FString&, Name, UFileMediaSource*, Movie, UMediaPlayer*, Player);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodeAnimationSound, const FString&, Name, USoundBase*, Sound);

/**
 * A code-driven animation timeline bound to the gameflow.
 *
 * The component owns a TotalSeconds-long playthrough whose 0-1 alpha is advanced every tick off the
 * server world clock (so playback position is a function of time, not of accumulated frame deltas).
 * Named AnimationSteps carve the timeline into sub-ranges; each step gets Start / Tick / End events,
 * with its Tick carrying both the raw playthrough alpha and the step-local 0-1 alpha remapped through
 * EaseFunction. MovieSteps and SoundSteps fire once when the alpha reaches their trigger percent.
 * Listeners (Blueprint or C++) do the actual visual work — this component only sequences and eases.
 *
 * Gameflow binding (all optional, driven by ActiveStates):
 *  - When the gameflow enters one of ActiveStates and bPlayOnEnter is set, the animation plays.
 *  - When a transition out of one of ActiveStates is requested and bPlayOnExit / bReverseOnExit is
 *    set, the component registers itself as an exit authority, plays the exit (reversed when
 *    bReverseOnExit), and releases the authority when done — holding the state open until its
 *    goodbye animation finishes.
 *  - When a transition is interrupted (demanded through), playback stops; with
 *    bFinalTickOnInterrupt set, every unfinished step receives one final tick at its end value
 *    first, so listeners land on their final pose instead of freezing mid-flight.
 *  - bAutoContinueOnFinish advances the gameflow's transition queue when an enter-side playthrough
 *    completes (after EndDelaySeconds).
 *
 * Events fire on this class's typed delegates AND, when the runtime class is the reparented
 * Content/HVP Blueprint shell, into the shell's original dispatchers via reflection — so consumer
 * Blueprints bound to the old dispatchers keep working unmodified.
 *
 * Native replacement for /Game/HVP/Systems/CodeAnimation/CodeAnimation.
 */
UCLASS(ClassGroup = (HVP), meta = (BlueprintSpawnableComponent), BlueprintType, Blueprintable)
class HVPCODEANIMATION_API UCodeAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCodeAnimationComponent();

	// --- Timeline ---

	/** Length of the playthrough in seconds (the Blueprint's "Total Seconds"; BP default was 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation", meta = (ClampMin = "0.0"))
	double TotalSeconds = 0.0;

	/** TotalSeconds extended to cover any movie/sound step that outlives the timeline (computed on play). */
	UPROPERTY(BlueprintReadOnly, Category = "Code Animation")
	double TotalSecondsWithMedia = 0.0;

	/** Delay between PlayAnimation (or state entry) and the timeline actually starting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation", meta = (ClampMin = "0.0"))
	double StartDelaySeconds = 0.0;

	/** Delay after the timeline completes before auto-continue / exit-authority release. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation", meta = (ClampMin = "0.0"))
	double EndDelaySeconds = 0.0;

	/** Easing applied to each step's local 0-1 alpha (not to the overall playthrough alpha). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TEnumAsByte<EEasingFunc::Type> EaseFunction = EEasingFunc::Linear;

	/** Blend exponent for EaseIn/EaseOut/EaseInOut easing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	float BlendExp = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TArray<FAnimationStep> AnimationSteps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TArray<FMovieStep> MovieSteps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation")
	TArray<FSoundStep> SoundSteps;

	// --- Gameflow binding ---

	/** Gameflow states this animation belongs to (a Set of Strings, matching the Blueprint; compared case-insensitively). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow")
	TSet<FString> ActiveStates;

	/** Legacy single active state; still honored alongside ActiveStates, like the original Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow", meta = (DisplayName = "Active State (Deprecated)"))
	FString ActiveStateDeprecated;

	/** Play automatically when the gameflow enters one of ActiveStates (BP default was OFF). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow")
	bool bPlayOnEnter = false;

	/** Play forward as the exit animation when leaving one of ActiveStates (held by exit authority). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow")
	bool bPlayOnExit = false;

	/** Play in reverse as the exit animation when leaving one of ActiveStates (wins over bPlayOnExit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow")
	bool bReverseOnExit = false;

	/**
	 * Hierarchical states: by default the exit side only fires when the gameflow leaves an active
	 * subtree entirely — "PMN" -> "PMN/Step1" plays nothing for a component active on "PMN". Set
	 * this to also fire the exit on transitions that stay inside the subtree (any state change
	 * while active counts as an exit). No effect on flat states.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow")
	bool bPlayOnExitToSubstate = false;

	/** When an enter-side playthrough finishes, continue the gameflow transition queue (BP default ON). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow")
	bool bAutoContinueOnFinish = true;

	/** On interruption, deliver one final tick at each unfinished step's end value before stopping (BP default ON). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow")
	bool bFinalTickOnInterrupt = true;

	/**
	 * When a DEMANDED transition leaves an active subtree, an exit animation configured here
	 * (bPlayOnExit / bReverseOnExit) never gets to play — demands skip the request-side exit
	 * machinery by design. With this set, the exit SNAPS instead: Start, one final tick per step
	 * at the exit's end value, End(interrupted) — so listeners land on the exit-end pose rather
	 * than being left mid-scene as if the exit never existed. The bFinalTickOnInterrupt
	 * philosophy, extended to exits that never started. No media fires and the transition queue
	 * is not advanced (the demand already moved it).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Code Animation|Gameflow")
	bool bSnapExitOnDemand = true;

	// --- Runtime state ---

	UPROPERTY(BlueprintReadOnly, Category = "Code Animation")
	bool bPlayingAnimation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Code Animation")
	bool bPlayingExitAnimation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Code Animation")
	bool bReversed = false;

	/** Playthrough alpha 0-1 (already direction-corrected: runs 1 -> 0 while reversed). */
	UPROPERTY(BlueprintReadOnly, Category = "Code Animation")
	double CurrentPlaythroughAlpha = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Code Animation")
	double CurrentPlaythroughSeconds = 0.0;

	// --- Events ---

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationStart StartAnimation;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationEnd EndAnimation;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationStepEvent StartAnimationStep;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationStepTick TickAnimationStep;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationStepEvent EndAnimationStep;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationMovie StartMovie;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation")
	FCodeAnimationSound StartSound;

	// --- Control ---

	/**
	 * Start (or restart) the playthrough. When already playing, does nothing unless ResetIfPlaying.
	 * Media steps only fire on forward playthroughs — a reversed exit doesn't re-trigger VO.
	 */
	UFUNCTION(BlueprintCallable, Category = "Code Animation")
	void PlayAnimation(bool Reverse = false, bool ResetIfPlaying = false);

	/** Halt playback where it is; releases a held exit authority. FireEndEvent also raises EndAnimation. */
	UFUNCTION(BlueprintCallable, Category = "Code Animation")
	void StopAnimation(bool FireEndEvent = false);

	/** True once a playthrough has run to completion (and none is currently running). */
	UFUNCTION(BlueprintPure, Category = "Code Animation")
	void IsAnimationFinished(bool& Finished) const { Finished = !bPlayingAnimation && bHasCompleted; }

	UFUNCTION(BlueprintPure, Category = "Code Animation")
	bool IsStepPlaying(const FString& StepName) const;

	UFUNCTION(BlueprintPure, Category = "Code Animation")
	bool IsStepFinished(const FString& StepName) const;

	/** Ask the gameflow to move on to the next queued state. */
	UFUNCTION(BlueprintCallable, Category = "Code Animation")
	void ContinueToNextState();

	UFUNCTION(BlueprintPure, Category = "Code Animation")
	UGameflowControllerComponent* GetGameflowController() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Route this component's events through a CodeAnimations manager under the given name. */
	void SetForwardTarget(UCodeAnimationsComponent* Manager, const FString& AnimationName);

private:
	void TryBindController();

	/** Does any active entry (or the deprecated single state) contain State in its subtree? */
	bool AnyEntryContains(const FString& State) const;

	/** Did the Old->New transition cross INTO some active entry's subtree (enter boundary)? */
	bool CrossesIntoActive(const FString& OldState, const FString& NewState) const;

	/** Did the Old->New transition cross OUT of some active entry's subtree (exit boundary)? */
	bool CrossesOutOfActive(const FString& OldState, const FString& NewState) const;

	UFUNCTION()
	void HandleStateChange(const FString& OldState, const FString& NewState);

	UFUNCTION()
	void HandleStateChangeRequested(const FString& OldState, const FString& NewState);

	UFUNCTION()
	void HandleStateChangeInterrupted(const FString& OldState, const FString& NewState);

	bool MatchesActiveState(const FString& State) const;

	/** Server-synchronised now, in seconds; falls back to world time without a game state. */
	double NowSeconds() const;

	/** Advance step / media events to the given direction-corrected alpha. */
	void TickSteps(double Alpha);
	void TickMedia(double ForwardProgress);
	void FinishStep(const FAnimationStep& Step, double FinalScaledAlpha);
	double ScaleStepAlpha(const FAnimationStep& Step, double Alpha) const;

	/**
	 * Seconds from playthrough start until the animation may complete: the step timeline
	 * (StartDelay + TotalSeconds) extended by every media step's tail. Movie durations only become
	 * known once their player has opened, so this is re-evaluated each tick while waiting.
	 */
	double ComputeFinishSeconds() const;

	/** True while a triggered movie's player has no duration yet (opening in flight). */
	bool IsWaitingOnUnpreparedMovie() const;

	/** Run every unfinished step to its end value and broadcast EndAnimation. */
	void CompleteAnimation(bool bInterrupted);
	void HandleEndDelayElapsed();

	void ReleaseExitAuthority();

	/**
	 * Instant exit playthrough for a demanded-through transition: Start, final step ticks at
	 * the exit's end value (0 reversed / 1 forward), End(interrupted). Interrupt semantics —
	 * no media, no auto-continue, no end delay.
	 */
	void SnapExitToEnd();

	// Broadcast helpers: fire the native delegate, the reparented BP shell's original dispatcher
	// (via reflection, when present), and the owning CodeAnimations manager (when set).
	void BroadcastStart();
	void BroadcastEnd(bool bInterrupted);
	void BroadcastStepStart(const FString& StepName);
	void BroadcastStepTick(const FString& StepName, double Alpha, double ScaledAlpha);
	void BroadcastStepEnd(const FString& StepName);

	void TriggerMovieStep(const FMovieStep& Step);
	void TriggerSoundStep(const FSoundStep& Step);

	FName ExitAuthorityName() const;

	double PlaythroughStartTime = 0.0;
	bool bHasCompleted = false;
	bool bHoldingExitAuthority = false;

	/**
	 * Set when the request path started this component's exit playthrough, consumed by the
	 * next executed state change — so a requested exit that already played (holding the state
	 * open) isn't ALSO snapped by bSnapExitOnDemand when the transition finally executes.
	 */
	bool bExitHandledByRequest = false;

	/**
	 * Set once every movie step has triggered and reported a duration, which switches off the
	 * per-tick re-evaluation of TotalSecondsWithMedia. A player that errors counts as resolved --
	 * IsWaitingOnUnpreparedMovie stops waiting on it -- so a clip that never opens settles on the
	 * no-media length instead of polling for the rest of the playthrough.
	 */
	bool bMediaLengthResolved = false;

	TSet<FName> StepsStarted;
	TSet<FName> StepsFinished;
	TSet<int32> MoviesTriggered;
	TSet<int32> SoundsTriggered;

	FTimerHandle EndDelayTimer;

	UPROPERTY(Transient)
	TWeakObjectPtr<UCodeAnimationsComponent> ForwardTarget;
	FString ForwardName;

	UPROPERTY(Transient)
	TObjectPtr<UGameflowControllerComponent> CachedController;
};
