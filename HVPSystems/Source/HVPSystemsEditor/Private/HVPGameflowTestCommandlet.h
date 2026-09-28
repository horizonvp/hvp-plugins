#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "HVPGameflowTestCommandlet.generated.h"

class UGameflowControllerComponent;

/**
 * Headless smoke test for the gameflow controller's hierarchical states and entry-requirement
 * gating. Exercises request/demand transitions with exit authorities and entry requirements and
 * fails (non-zero exit) on any violated expectation.
 *
 * Run: UnrealEditor-Cmd.exe <project> -run=HVPGameflowTest
 */
UCLASS()
class UHVPGameflowTestCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;

private:
	// Test-listener handlers bound to the controller under test.
	UFUNCTION()
	void OnRequested_GateEntry(const FString& OldState, const FString& NewState);

	UFUNCTION()
	void OnDemanded_GateEntry(const FString& OldState, const FString& NewState);

	UFUNCTION()
	void OnRequested_HoldExit(const FString& OldState, const FString& NewState);

	UPROPERTY(Transient)
	TObjectPtr<UGameflowControllerComponent> Controller;

	/** When set, the matching handler registers this entry requirement / exit authority. */
	FName PendingEntryGate;
	FName PendingExitHold;
};
