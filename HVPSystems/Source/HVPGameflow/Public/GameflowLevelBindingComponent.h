#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameflowLevelBindingComponent.generated.h"

class UGameflowControllerComponent;
class ULevelStreaming;
class UWorld;

/** How one gameflow state (subtree) maps onto its streaming sublevel(s). */
USTRUCT(BlueprintType)
struct FGameflowLevelBinding
{
	GENERATED_BODY()

	/** The streaming sublevel this state needs. Must be in the persistent map's levels list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow")
	TSoftObjectPtr<UWorld> Level;

	/**
	 * Further sublevels this state ALSO needs — the one-to-many case (e.g. a shared scene
	 * plus section-specific dressing). Each is loaded, entry-gated, and unloaded exactly
	 * like Level: the state is only entered once EVERY listed level is visible, and
	 * leaving the subtree releases them all (each still protected by the shared-level
	 * book-end guard).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow")
	TArray<TSoftObjectPtr<UWorld>> AdditionalLevels;

	/** Unload the level when the gameflow leaves this state's subtree. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow")
	bool bUnloadOnLeave = true;

	/** Grace period before the unload, for media/animations that linger past the state change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow", meta = (ClampMin = "0.0"))
	double UnloadDelaySeconds = 0.0;

	/**
	 * Hide the levels (actors EndPlay, components unregister) as soon as the subtree is left, and use
	 * UnloadDelaySeconds only to defer the unload itself. Unloading forces a full GC
	 * (s.ForceGCAfterLevelStreamedOut), so this keeps that hitch off the transition frames without
	 * leaving the old level on screen. Ignored when UnloadDelaySeconds is 0. A level another binding
	 * still needs for the current state stays visible.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow")
	bool bHideDuringUnloadDelay = false;

	/**
	 * Optional earlier state (subtree) during which to stream the levels into memory without showing
	 * them, so entering the bound state only has to make them visible. Their actors do not BeginPlay
	 * until then. Waits for any previous copy to finish unloading first, so each entry still gets a
	 * fresh level. Empty = load on entry as usual.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow")
	FString PreloadDuringState;
};

/**
 * Declarative state -> sublevel streaming, driven by the gameflow.
 *
 * Place one on a persistent-level actor and fill in Bindings ("PMN" -> Scene_PMN). When a
 * transition targets a state inside a bound subtree, the component registers an entry requirement
 * on the gameflow controller and streams the level in; the state is only entered once the level is
 * visible and its actors have begun play. Works for requests and demands alike. Leaving the
 * subtree optionally unloads the level after a grace period; re-entering during the grace period
 * cancels the unload. A binding can also preload its levels (loaded, not visible) during an
 * earlier state, which leaves only the make-visible step for entry.
 *
 * Keys are hierarchical: a binding for "PMN" also covers "PMN/Step1/..." — moving around inside
 * the subtree neither reloads nor unloads.
 */
UCLASS(ClassGroup = (HVP), meta = (BlueprintSpawnableComponent), Blueprintable, BlueprintType)
class HVPGAMEFLOW_API UGameflowLevelBindingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGameflowLevelBindingComponent();

	/** State (subtree root) -> sublevel to stream for it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow|Levels")
	TMap<FString, FGameflowLevelBinding> Bindings;

	/**
	 * Subscribe to the gameflow controller now, if it exists yet. BeginPlay and (failing that) tick
	 * call this automatically once the player controller is up; it is public so that code driving the
	 * very first transition of the app can guarantee this component is listening beforehand, rather
	 * than trusting tick order. Idempotent; returns true once bound.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameflow|Levels")
	bool EnsureBoundToController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	UFUNCTION()
	void HandleStateChangeRequested(const FString& OldState, const FString& NewState);

	UFUNCTION()
	void HandleStateChangeDemanded(const FString& OldState, const FString& NewState);

	UFUNCTION()
	void HandleStateChange(const FString& OldState, const FString& NewState);

	/** OnLevelShown gives no payload; sweep every pending load and clear the visible ones. */
	UFUNCTION()
	void HandleAnyLevelShown();

	/** Load (and gate entry on) every binding whose subtree contains TargetState. */
	void EnsureLevelsForState(const FString& TargetState);

	void ClearRequirementNextTick(const FString& BindingKey);
	void UnloadBinding(const FString& BindingKey);

	/** Hide a binding's levels now, ahead of its delayed unload (bHideDuringUnloadDelay). */
	void HideBinding(const FString& BindingKey);

	/**
	 * Book-end guard: whether a binding other than BindingKey covers the current state and needs
	 * Level, so leaving BindingKey's section must not hide or unload it.
	 */
	bool IsLevelNeededElsewhere(const FString& BindingKey, const TSoftObjectPtr<UWorld>& Level) const;

	/** Start a preload for every binding whose PreloadDuringState contains State. */
	void PreloadForState(const FString& State);

	/** Load-not-visible each level whose previous copy is gone; keep retrying the rest while still in the window. */
	void TryPreload(const FString& BindingKey);
	void StopPreloadRetry(const FString& BindingKey);

	/** Every level a binding needs: Level plus AdditionalLevels, nulls skipped. */
	static void AppendBoundLevels(const FGameflowLevelBinding& Binding,
		TArray<TSoftObjectPtr<UWorld>>& OutLevels);

	/** One binding can gate several levels; each (state, level) pair gets its own
	 *  requirement/pending-load identity. */
	static FString MakeInstanceKey(const FString& BindingKey, const TSoftObjectPtr<UWorld>& Level);

	/** Resolve a soft level reference against the world's streaming-levels list. */
	ULevelStreaming* FindStreamingLevel(const TSoftObjectPtr<UWorld>& Level) const;

	static FName RequirementName(const FString& BindingKey);

	UPROPERTY(Transient)
	TObjectPtr<UGameflowControllerComponent> CachedController;

	/** Instance keys (state|level) streaming in -> the level; each requirement clears when
	 *  its level becomes visible, so a state with several levels waits for them all. */
	TMap<FString, TSoftObjectPtr<UWorld>> PendingLoads;

	/** Pending delayed unloads, cancellable on re-entry. */
	TMap<FString, FTimerHandle> PendingUnloads;

	/** Preloads (by binding key) waiting on the previous copy of a level to finish unloading. */
	TMap<FString, FTimerHandle> PendingPreloads;
};
