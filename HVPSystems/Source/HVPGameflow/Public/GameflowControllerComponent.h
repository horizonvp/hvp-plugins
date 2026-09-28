#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameflowControllerComponent.generated.h"

class UExitAuthorityList;

HVPGAMEFLOW_API DECLARE_LOG_CATEGORY_EXTERN(LogHVPGameflow, Log, All);

// Param names match the Blueprint originals ("Old State" / "New State") as closely as C++ allows.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGameflowStateChange, const FString&, OldState, const FString&, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGameflowStateChangeRequested, const FString&, OldState, const FString&, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGameflowStateChangeInterrupted, const FString&, OldState, const FString&, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGameflowStateChangeDemanded, const FString&, OldState, const FString&, NewState);

/**
 * The app-wide gameflow state machine. Lives on the PlayerController (get-or-add via
 * UGameflowBlueprintLibrary::GetGameflowController) and holds the current state as a string,
 * compared case-insensitively.
 *
 * State changes are a two-phase handshake, not an instant flip:
 *
 *  1. RequestStateChange(To) broadcasts OnStateChangeRequested(Current, To). During that broadcast,
 *     any listener that needs time to leave the current state (exit animations, fades) calls
 *     RegisterExitAuthority(Name) to hold the transition open.
 *  2. Each holder calls ClearExitAuthority(Name) when it finishes. When the last authority clears —
 *     or StateChangeMaxWaitSeconds elapses — the transition is approved: the state flips and
 *     OnStateChange(Old, New) fires.
 *
 * DemandStateChange skips the exit handshake: any pending transition is interrupted
 * (OnStateChangeInterrupted fires, authorities are cleared) and the state flips immediately —
 * unless the target has entry requirements (see below), which gate demands too.
 *
 * Entry requirements are the mirror image of exit authorities: RegisterEntryRequirement /
 * ClearEntryRequirement hold a pending transition's ENTRY open (typically while a bound sublevel
 * streams in — see UGameflowLevelBindingComponent). They apply to requests and demands alike, and
 * are never forced through by the timeout.
 *
 * States may be hierarchical, slash-separated paths ("PMN", "PMN/Step1", "PMN/Step1/Part2").
 * The controller itself treats them as opaque strings; IsStateWithin supplies the subtree test
 * that listeners (CodeAnimation, level bindings) build their enter/exit semantics on.
 *
 * A transition queue (StartTransitionQueue / ContinueToNextState / SnapToNextState) drives the
 * module flow: each "continue" requests the next queued state. The queue is a flat list of
 * strings whose structure is carried by the paths: a reserved "All" or "Any" segment turns a run
 * of sibling entries into a block of branches around an implicit hub, so an unordered "visit
 * every one of these" (or "pick one of these") sits in the same list as the linear steps. The
 * rules live in GameflowQueueRules; the controller only asks it for the next state and tells it
 * which transition was accepted.
 *
 * Native replacement for /Game/HVP/Systems/AnimatedGameflow/GameflowController_AC.
 */
UCLASS(ClassGroup = (HVP), meta = (BlueprintSpawnableComponent), BlueprintType, Blueprintable)
class HVPGAMEFLOW_API UGameflowControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGameflowControllerComponent();

	// --- State (read-only from outside; drive it through the request/demand API) ---

	UPROPERTY(BlueprintReadOnly, Category = "Gameflow")
	FString CurrentState;

	UPROPERTY(BlueprintReadOnly, Category = "Gameflow")
	FString PreviousState;

	/** Non-empty while a transition is awaiting exit authorities. */
	UPROPERTY(BlueprintReadOnly, Category = "Gameflow")
	FString RequestedState;

	/**
	 * Remaining entries of the running transition queue. Writable — the skip button replaces it
	 * directly. The front is the next state only for plain entries; a block's entries are its
	 * remaining branches and the hub is implicit, so read GetNextQueuedState rather than [0].
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Gameflow")
	TArray<FString> TransitionQueue;

	/** How long a requested transition waits for exit authorities before being forced through (BP default 0 = next tick). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow", meta = (ClampMin = "0.0"))
	float StateChangeMaxWaitSeconds = 0.0f;

	// --- Events ---

	/** The state actually flipped. */
	UPROPERTY(BlueprintAssignable, Category = "Gameflow")
	FGameflowStateChange OnStateChange;

	/** A transition was requested; register exit authorities from handlers of this to delay it. */
	UPROPERTY(BlueprintAssignable, Category = "Gameflow")
	FGameflowStateChangeRequested OnStateChangeRequested;

	/** A pending or active transition was cut short by a demand. */
	UPROPERTY(BlueprintAssignable, Category = "Gameflow")
	FGameflowStateChangeInterrupted OnStateChangeInterrupted;

	/**
	 * A demand is about to flip the state. Handlers may register entry requirements (level loads)
	 * to defer the flip; exit authorities registered here are ignored — a demand never waits on
	 * exits. Fires after OnStateChangeInterrupted.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Gameflow")
	FGameflowStateChangeDemanded OnStateChangeDemanded;

	// --- Transitions ---

	/**
	 * Ask to move to a new state, honouring the exit-authority handshake. Ignored (with a log) if
	 * another request is already pending. If nothing registers an authority during the
	 * OnStateChangeRequested broadcast, the state flips before this returns.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void RequestStateChange(const FString& InRequestedState);

	/**
	 * Force a state immediately. Any pending transition is interrupted: OnStateChangeInterrupted
	 * fires, its exit authorities are discarded, and the state flips with no waiting.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void DemandStateChange(const FString& DemandedState);

	/** Replace the transition queue (logging any authoring warnings) and request its first state. */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void StartTransitionQueue(const TArray<FString>& Queue);

	/**
	 * Request the queue's next state (the normal "this state is done" call). The queue is only
	 * peeked here; it advances when the transition is accepted, so a Continue that collides with
	 * a pending transition is dropped harmlessly and the next Continue retries it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void ContinueToNextState();

	/** Demand the queue's next state, skipping exit animations (debug / skip button). */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void SnapToNextState();

	/**
	 * The state Continue/Snap would go to from the current state: the front entry, or for a block
	 * at the front its hub (from outside or from a branch) or its first remaining branch (from the
	 * hub). Empty when the queue is empty.
	 */
	UFUNCTION(BlueprintPure, Category = "Gameflow")
	FString GetNextQueuedState() const;

	/** Remaining branches of the block at the front of the queue, in order; empty otherwise. */
	UFUNCTION(BlueprintPure, Category = "Gameflow")
	TArray<FString> GetRemainingBranches() const;

	// --- Exit authorities ---

	/** Hold the pending transition open until ClearExitAuthority(Authority) is called. */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void RegisterExitAuthority(FName Authority);

	/** Release one authority; the transition executes when the last one clears. */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void ClearExitAuthority(FName Authority);

	/** Discard every authority for every state. Does not approve a pending transition by itself. */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void ClearExits();

	UFUNCTION(BlueprintPure, Category = "Gameflow")
	bool IsExitPending() const;

	/** Execute the pending transition now, regardless of remaining authorities. */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void ApproveStateTransition();

	// --- Entry requirements ---

	/**
	 * Hold the pending transition's ENTRY open (level still streaming in, assets loading). Call
	 * during OnStateChangeRequested or OnStateChangeDemanded. Unlike exit authorities, entry
	 * requirements gate demands too, and the wait timeout never forces through a missing
	 * requirement — it only warns.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void RegisterEntryRequirement(FName Requirement);

	/** Release one entry requirement; the transition executes when nothing else is holding it. */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void ClearEntryRequirement(FName Requirement);

	UFUNCTION(BlueprintPure, Category = "Gameflow")
	bool IsEntryPending() const;

	/**
	 * Hierarchical-state test: is State equal to Parent, or a substate beneath it?
	 * "PMN/Step1" is within "PMN"; "PMNFoo" is not. Case-insensitive. An empty Parent matches
	 * nothing; an empty State matches nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "Gameflow")
	static bool IsStateWithin(const FString& State, const FString& Parent);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Flip the state and broadcast OnStateChange. */
	void ChangeState(const FString& NewState);

	/**
	 * Advance the queue past an accepted transition (GameflowQueueRules::Advance, with logging).
	 *
	 * Normally a plain front-of-queue pop, since Continue transitions to the resolved next state.
	 * A demand that jumps ahead (skip button, menu card) lands further down, and the entries it
	 * vaulted over are exactly the ones it chose to skip. Inside a block only the entered branch
	 * (All) or the whole block (Any) goes, and the hub is never in the queue to begin with.
	 *
	 * bClearIfUnqueued decides what an unqueued State means. A demand jumped somewhere the queue
	 * never planned for, so the rest of the run no longer follows and the queue is dropped; a
	 * request is the flow's own next step and leaves it alone. Guarded and idempotent.
	 */
	void AdvanceQueue(const FString& State, bool bClearIfUnqueued);

	/** Flip a gated demand or approve a gated request once nothing holds it. */
	void TryFinishPendingTransition();

	UExitAuthorityList* FindOrAddAuthorityList(const FString& State);
	UExitAuthorityList* FindAuthorityList(const FString& State) const;

	/** Per-state authority lists, keyed by the state being exited (case-insensitive keys). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UExitAuthorityList>> ExitAuthorities;

	/** Requirements holding the pending transition's entry (one transition pends at a time). */
	TSet<FName> EntryRequirements;

	/** Non-empty while a demand waits on entry requirements; flips as soon as they clear. */
	FString PendingDemandState;

	/** UTC time the pending request was made; drives the timeout. */
	FDateTime StateRequestTime;

	/** Throttles the entry-requirement stall warning. */
	FDateTime LastEntryStallWarning;
};
