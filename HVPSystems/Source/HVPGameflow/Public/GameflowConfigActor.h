#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameflowLevelBindingComponent.h"
#include "GameflowConfigActor.generated.h"

class UGameflowControllerComponent;

/**
 * Level-authored startup configuration for the gameflow — entirely optional.
 *
 * The dependency runs one way: nothing in the gameflow system knows this class exists. The
 * controller and the level-binding component behave identically whether or not one of these is in
 * the map. Drop one into a level and it primes the pump on BeginPlay:
 *
 *  1. Installs LevelBindings onto the level's UGameflowLevelBindingComponent — the one already in
 *     the map if there is one, otherwise one lazily added to the world settings (see
 *     UGameflowBlueprintLibrary::GetGameflowLevelBinding). Existing entries win unless
 *     bOverrideExistingBindings is set, so hand-authored bindings are never clobbered.
 *  2. Runs InitialStates as a transition queue: the first state is requested, and each
 *     ContinueToNextState from there walks the rest.
 *
 * Startup is deferred until the player controller (and therefore the gameflow controller) exists,
 * and every level-binding component in the world is bound to the controller before the first state
 * is requested — otherwise the first state's sublevel would miss its own entry gate.
 *
 * Placing two of these in one map is fine as long as only one carries InitialStates:
 * bSkipIfFlowAlreadyStarted keeps a config actor in a streamed-in sublevel from yanking a running
 * flow back to the beginning.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = (HVP), meta = (DisplayName = "Gameflow Config"))
class HVPGAMEFLOW_API AGameflowConfigActor : public AInfo
{
	GENERATED_BODY()

public:
	AGameflowConfigActor();

	// --- Level bindings ---

	/** State (subtree root) -> sublevel, copied onto the level's binding component at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow|Levels")
	TMap<FString, FGameflowLevelBinding> LevelBindings;

	/** Replace bindings the target component already has for the same key instead of keeping them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow|Levels")
	bool bOverrideExistingBindings = false;

	// --- Startup sequence ---

	/**
	 * States to walk through on startup, in order. The first is requested immediately; the rest sit
	 * in the controller's transition queue for ContinueToNextState. Leave empty to configure
	 * bindings only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow|Startup")
	TArray<FString> InitialStates;

	/** Uncheck to configure everything by hand and call Apply() yourself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow|Startup")
	bool bApplyOnBeginPlay = true;

	/** Grace period before the first state is requested — for splash fades, late-spawning rigs, XR handshakes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow|Startup",
		meta = (ClampMin = "0.0", EditCondition = "bApplyOnBeginPlay"))
	float StartDelaySeconds = 0.0f;

	/** Don't start the sequence if the gameflow has already left its initial state or has a queue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gameflow|Startup")
	bool bSkipIfFlowAlreadyStarted = true;

	// --- Manual driving ---

	/**
	 * Install the bindings and start the sequence, retrying each frame until the gameflow controller
	 * exists. One-shot: both halves are guarded, so calling this again does nothing. Drive the
	 * controller directly to re-run a flow.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void Apply();

	/** Copy LevelBindings onto the resolved binding component. Returns the component written to. */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	UGameflowLevelBindingComponent* InstallLevelBindings();

	/**
	 * Start InitialStates as the controller's transition queue. Returns false only while the gameflow
	 * controller doesn't exist yet (caller should retry); a skip under bSkipIfFlowAlreadyStarted
	 * counts as done and returns true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	bool StartInitialSequence();

	/** The binding component this actor writes to — resolved at BeginPlay, null before that. */
	UFUNCTION(BlueprintPure, Category = "Gameflow")
	UGameflowLevelBindingComponent* GetTargetBindingComponent() const { return TargetBindingComponent; }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** A component on this actor if a subclass added one, else the world's (see FindOrAddLevelBinding). */
	UGameflowLevelBindingComponent* ResolveBindingComponent();

	UPROPERTY(Transient)
	TObjectPtr<UGameflowLevelBindingComponent> TargetBindingComponent;

	bool bBindingsInstalled = false;
	bool bSequenceStarted = false;

	FTimerHandle StartDelayTimer;
};
