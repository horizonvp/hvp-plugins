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
};

/**
 * Declarative state -> sublevel streaming, driven by the gameflow.
 *
 * Place one on a persistent-level actor and fill in Bindings ("PMN" -> Scene_PMN). When a
 * transition targets a state inside a bound subtree, the component registers an entry requirement
 * on the gameflow controller and streams the level in; the state is only entered once the level is
 * visible and its actors have begun play. Works for requests and demands alike. Leaving the
 * subtree optionally unloads the level after a grace period; re-entering during the grace period
 * cancels the unload.
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
};
