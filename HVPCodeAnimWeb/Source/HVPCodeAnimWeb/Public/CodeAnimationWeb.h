#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CodeAnimationWebTypes.h"

#include "CodeAnimationWeb.generated.h"

class UCodeAnimationWeb;
struct FCodeAnimWebRuntime;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCodeAnimWebOutputChanged,
	UCodeAnimationWeb*, Web, const TArray<FName>&, ChangedOutputs);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCodeAnimWebStateChanged,
	UCodeAnimationWeb*, Web, uint8, NewState, uint8, PreviousState);

/**
 * A Code Animation Web: a fixed set of states, a set of Animation Outputs, and how each state and each
 * transition drives those outputs.
 *
 * Make one with Add > Blueprint > Code Animation Web (a Blueprint of this class), then:
 *  - add variables and tick "Animation Output" in their Details to make them outputs;
 *  - pick the States enum in Class Defaults, and set what each state does to each output;
 *  - add Transitions for the pairs that need a particular duration or shape.
 *
 * A Web never touches the world. Whoever owns it calls Set State and listens to On Outputs Changed,
 * reading the output variables off the Web and applying them however it likes.
 *
 * Everything is a function of game time since each state or transition began, not of accumulated
 * frame deltas, so pausing and time dilation behave, and a state reads the same however it was reached.
 */
UCLASS(Abstract, Blueprintable, ClassGroup = (HVP), meta = (BlueprintSpawnableComponent),
	HideCategories = (Activation, Collision, Cooking, AssetUserData, Navigation, Replication,
		ComponentReplication, ComponentTick, Tags, Sockets))
class HVPCODEANIMWEB_API UCodeAnimationWeb : public UActorComponent
{
	GENERATED_BODY()

public:
	UCodeAnimationWeb();

	// ---- authoring (Class Defaults) ---------------------------------------------------------------

	/** The states this Web can be in. A Blueprint enum asset, or a C++ enum marked BlueprintType. */
	UPROPERTY(EditDefaultsOnly, Category = "Code Animation Web", meta = (DisplayName = "States"))
	TObjectPtr<UEnum> StateEnum;

	/** Where the Web starts, instantly. Unset means the enum's first entry. */
	UPROPERTY(EditDefaultsOnly, Category = "Code Animation Web")
	FCodeAnimWebStateRef InitialState;

	/** What each state does to each output. Kept in step with the enum and the outputs automatically. */
	UPROPERTY(EditDefaultsOnly, EditFixedSize, Category = "Code Animation Web", meta = (DisplayName = "State Values"))
	TArray<FCodeAnimWebStateEntry> States;

	/** Transitions for particular pairs of states. Any pair not listed uses Default Transition. */
	UPROPERTY(EditDefaultsOnly, Category = "Code Animation Web|Transitions")
	TArray<FCodeAnimWebTransition> Transitions;

	UPROPERTY(EditDefaultsOnly, Category = "Code Animation Web|Transitions")
	FCodeAnimWebTransitionTiming DefaultTransition;

	/**
	 * Used instead of any authored transition when the state changes mid-transition. Authored transitions
	 * are only played from a state that was actually reached, so they can count on their starting values;
	 * from anywhere in between, the Web blends from exactly where it is, still moving, using this.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Code Animation Web|Transitions")
	FCodeAnimWebTransitionTiming InterruptTransition;

	/**
	 * How many transitions may be blending on top of each other before the oldest is frozen in place.
	 * Only reached by changing state several times faster than the transitions finish.
	 */
	UPROPERTY(EditDefaultsOnly, AdvancedDisplay, Category = "Code Animation Web|Transitions", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxBlendDepth = 4;

	/**
	 * Only ever move along authored transitions (any row in Transitions, graph or not - never the default
	 * lerp). Set State finds a route of transitions to the target and plays them one after another;
	 * a state no route reaches is refused, and the Web stays where it is. Setting a state mid-route
	 * lets the transition in progress finish, then heads for the new target from there.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Code Animation Web|Transitions")
	bool bTransitionTraversalOnly = false;

	/** Which route Transition Traversal Only takes when there is more than one. */
	UPROPERTY(EditDefaultsOnly, Category = "Code Animation Web|Transitions", meta = (EditCondition = "bTransitionTraversalOnly"))
	ECodeAnimWebPathCost PathCost = ECodeAnimWebPathCost::FewestTransitions;

	/** Custom Lerp graphs, one per output set to the Custom lerp. Managed from the variables' Details. */
	UPROPERTY()
	TArray<FCodeAnimCustomLerp> CustomLerps;

	// ---- runtime ----------------------------------------------------------------------------------

	/**
	 * For C++ listeners: fired at most once per update, after every output has been written, naming the
	 * outputs whose values changed.
	 *
	 * Blueprints use the Web Blueprint's own dispatchers instead, which the Web makes and keeps up to
	 * date, and which carry the Web as its own class rather than as a plain Code Animation Web: On
	 * Outputs Changed (the same, once per update) and one On <Output> Changed per output (the new
	 * value typed).
	 */
	UPROPERTY()
	FOnCodeAnimWebOutputChanged OnOutputChanged;

	UPROPERTY(BlueprintAssignable, Category = "Code Animation Web")
	FOnCodeAnimWebStateChanged OnStateChanged;

	/**
	 * Head for a state. Setting the state already being headed for does nothing. The Set Web State node
	 * is this with a pin of the Web's own States enum.
	 * @param bInstant	Jump there with no transition.
	 */
	UFUNCTION(BlueprintCallable, Category = "Code Animation Web", meta = (BlueprintInternalUseOnly = "true"))
	bool SetState(uint8 NewState, bool bInstant = false);

	/** The state being headed for (or sat in). The Get Web State node is this, as the Web's States enum. */
	UFUNCTION(BlueprintPure, Category = "Code Animation Web", meta = (BlueprintInternalUseOnly = "true"))
	uint8 GetState() const;

	UFUNCTION(BlueprintPure, Category = "Code Animation Web")
	bool IsTransitioning() const;

	/**
	 * Freeze the Web: its clock stops, so a transition holds where it is and a state graph stops
	 * animating; it stops ticking, so nothing is evaluated or fired; and Set State is refused (Set Web
	 * State's Success comes out false). Unpausing carries on from exactly where it stopped - the time
	 * spent paused is skipped, not caught up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Code Animation Web")
	void SetPaused(bool bPaused);

	UFUNCTION(BlueprintPure, Category = "Code Animation Web")
	bool IsPaused() const { return bIsPaused; }

	/**
	 * Head for a state and pause there: from now on Set State is refused, and once the Web arrives it
	 * freezes, exactly as Set Paused does - on the values of the moment it arrived. Already there, it
	 * freezes at once. Set Paused (false) releases it, before or after it arrives. The Pause Web In State
	 * node is this with a pin of the Web's own States enum.
	 * @return False if refused: already paused or headed for a pause, or the state itself was refused.
	 */
	UFUNCTION(BlueprintCallable, Category = "Code Animation Web", meta = (BlueprintInternalUseOnly = "true"))
	bool PauseInState(uint8 NewState, bool bInstant = false);

	/** Whether Set State is refused right now: paused, or headed for a pause by Pause In State. */
	UFUNCTION(BlueprintPure, Category = "Code Animation Web")
	bool IsStateLocked() const { return bIsPaused || bPauseOnArrival; }

	/**
	 * Fire every output's events, changed or not - On Outputs Changed once, naming them all. Call after
	 * binding at runtime, so the listener starts from the current values rather than waiting for the
	 * next change. (Events bound in the Details panel are bound before play, and get this automatically
	 * at Begin Play.)
	 */
	UFUNCTION(BlueprintCallable, Category = "Code Animation Web")
	void RebroadcastOutputs();

	/** Name of every Animation Output, in variable order. */
	UFUNCTION(BlueprintPure, Category = "Code Animation Web")
	TArray<FName> GetOutputNames() const;

	/**
	 * Evaluate now and publish any changes. Outputs follow states on their own; call this after changing
	 * a variable that a settled Web's graphs read, so the change shows without waiting for a state change.
	 */
	UFUNCTION(BlueprintCallable, Category = "Code Animation Web")
	void Refresh();

	// ---- backing for the Web's Blueprint nodes ----------------------------------------------------

	/**
	 * Get From Output / Get To Output: an output's value at one end of the transition being evaluated.
	 * Outside a transition or Custom Lerp graph, both ends are the output's default.
	 */
	UFUNCTION(BlueprintPure, CustomThunk, meta = (BlueprintInternalUseOnly = "true", CustomStructureParam = "Value"))
	void GetTransitionEndpoint(FName Output, bool bTo, int32& Value) const;
	DECLARE_FUNCTION(execGetTransitionEndpoint);

	// ---- manual time (C++) ------------------------------------------------------------------------

	/**
	 * Set State at an explicit game time. Set State and Tick use the world's time; tests and tools use this.
	 * @return False if the state was refused: the Web is paused, it is not a state of the Web, or
	 *         (Transition Traversal Only) no route of transitions reaches it.
	 */
	bool SetStateAtTime(uint8 NewState, double Now, bool bInstant = false);

	/** Evaluate and publish at an explicit game time. Does nothing while paused. */
	void UpdateAtTime(double Now);

	/** Pause or unpause at an explicit game time. */
	void SetPausedAtTime(bool bPaused, double Now);

	/** Pause In State at an explicit game time. */
	bool PauseInStateAtTime(uint8 NewState, double Now, bool bInstant = false);

	// ---- editor -----------------------------------------------------------------------------------

#if WITH_EDITOR
	/**
	 * Rebuild the output list and the State Values from the variables' metadata and the enum, keeping
	 * every per-state override whose output still exists. Runs on compile and on edits; safe to call anytime.
	 */
	void SyncDefinition();

	/** Delete the State Values of enumerators that no longer exist. */
	UFUNCTION(CallInEditor, Category = "Code Animation Web")
	void RemoveOrphanedStates();

	/**
	 * Default values of the outputs, laid out like each state's Values - what an unticked output shows.
	 * Rebuilt from the output list and the class defaults if it has not been built this session.
	 */
	const FInstancedPropertyBag& GetOutputDefaults();

	/** The Blueprint this Web class was compiled from, or null for a native class. */
	UBlueprint* GetWebBlueprint() const;

	virtual void PostCDOCompiled(const FPostCDOCompiledContext& Context) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Every enumerator of StateEnum as (key, display name), in enum order, hidden ones and _MAX left out. */
	void GetStateList(TArray<TPair<FName, FText>>& OutStates) const;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Baked on compile from the variables' metadata; see FCodeAnimOutputSpec. */
	UPROPERTY()
	TArray<FCodeAnimOutputSpec> OutputSpecs;

	/** The Web Blueprint's On Outputs Changed dispatcher, baked on compile like the outputs' own. */
	UPROPERTY()
	FName OutputsChangedEventName;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	FInstancedPropertyBag OutputDefaults;

public:
	/** Where the Web Graph's Entry node sits. */
	UPROPERTY()
	FVector2D WebGraphEntryPosition = FVector2D::ZeroVector;

	UPROPERTY()
	bool bHasWebGraphEntryPosition = false;

private:
#endif

	/** Built on first use from the class defaults. Shared rather than unique: its type is private. */
	TSharedPtr<FCodeAnimWebRuntime> Runtime;

	bool bIsPaused = false;
	/** Pause In State: headed for a state, refusing others, and to freeze on arrival. */
	bool bPauseOnArrival = false;
	/** Game time the current pause began. */
	double PausedAt = 0.0;
	/** Game time spent paused, all told: the Web's clock is game time less this. */
	double PausedTotal = 0.0;

#if WITH_EDITOR
	void RebuildOutputDefaults();
#endif

	FCodeAnimWebRuntime& EnsureRuntime(double Now);
	double GetNow() const;

	/**
	 * Game time to the Web's own clock, which stands still while paused. Everything below the public
	 * entry points runs on the Web's clock.
	 */
	double ToWebTime(double Now) const { return (bIsPaused ? PausedAt : Now) - PausedTotal; }
	bool SetStateWebTime(uint8 NewState, double Now, bool bInstant);
	void UpdateWebTime(double Now);

	/** Stop the clock at a time on the Web's own clock. */
	void FreezeAtWebTime(double Now);
	void Evaluate(double Now);
	void Publish(bool bBroadcastAll);
	void UpdateTickEnabled();
	void ApplyPendingState(double Now);

	/** Copies one output's value out of a pose (or the live variable) into memory of a matching property. */
	void ReadOutput(FName Output, int32 Source, const FProperty* ValueProperty, void* ValueAddress) const;
};
