#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameflowBlueprintLibrary.generated.h"

class UGameflowControllerComponent;
class UGameflowLevelBindingComponent;

/**
 * Native replacement for /Game/HVP/Systems/AnimatedGameflow/Gameflow_BFL.
 */
UCLASS()
class HVPGAMEFLOW_API UGameflowBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The one gameflow controller for the running app: found on (or lazily added to) the local
	 * player controller. Null before the player controller exists.
	 *
	 * Pure with an out-pin named "Controller", exactly like the Blueprint original — consumer
	 * graphs wire that pin straight into gameflow calls.
	 */
	UFUNCTION(BlueprintPure, Category = "Gameflow", meta = (WorldContext = "WorldContextObject"))
	static void GetGameflowController(const UObject* WorldContextObject, UGameflowControllerComponent*& Controller);

	/** C++ convenience for the same lookup. */
	static UGameflowControllerComponent* FindOrAddController(const UObject* WorldContextObject);

	// --- Level bindings ---

	/**
	 * The level's state -> sublevel binding component: the one already placed in the world, or one
	 * lazily added to the persistent level's world settings. The mirror of GetGameflowController,
	 * with an out-pin named "Level Binding" — wire it straight into Bindings edits.
	 *
	 * If several are placed (one per streamed sublevel, say), this returns the first found and logs
	 * a warning; use GetAllGameflowLevelBindings when you mean all of them.
	 */
	UFUNCTION(BlueprintPure, Category = "Gameflow", meta = (WorldContext = "WorldContextObject"))
	static void GetGameflowLevelBinding(const UObject* WorldContextObject, UGameflowLevelBindingComponent*& LevelBinding);

	/** C++ convenience for the same lookup. */
	static UGameflowLevelBindingComponent* FindOrAddLevelBinding(const UObject* WorldContextObject);

	/** Every level-binding component currently in the world; empty if none are placed. Adds nothing. */
	UFUNCTION(BlueprintPure, Category = "Gameflow", meta = (WorldContext = "WorldContextObject"))
	static TArray<UGameflowLevelBindingComponent*> GetAllGameflowLevelBindings(const UObject* WorldContextObject);

	// --- Hierarchical states ---

	/** Is State equal to Parent or a substate beneath it? ("PMN/Step1" is within "PMN".) */
	UFUNCTION(BlueprintPure, Category = "Gameflow")
	static bool IsStateWithin(const FString& State, const FString& Parent);

	/** Parent path of a hierarchical state ("PMN/Step1/Part2" -> "PMN/Step1"); empty at the root. */
	UFUNCTION(BlueprintPure, Category = "Gameflow")
	static FString GetParentState(const FString& State);

	/** Is the app's current gameflow state within Query? False before the controller exists. */
	UFUNCTION(BlueprintPure, Category = "Gameflow", meta = (WorldContext = "WorldContextObject"))
	static bool IsControllerInState(const UObject* WorldContextObject, const FString& Query);
};
