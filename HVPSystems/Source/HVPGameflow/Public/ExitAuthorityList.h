#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ExitAuthorityList.generated.h"

/**
 * A set of named "exit authorities" holding one gameflow state open.
 *
 * The GameflowController keeps one of these per state that has a pending transition. Anything that
 * needs time before the state may be left (an exit animation, a fade, a save) registers itself here
 * by name and removes itself when done; the transition executes once the list runs dry.
 *
 * Native replacement for /Game/HVP/Systems/AnimatedGameflow/ExitAuthorityList.
 */
UCLASS(BlueprintType)
class HVPGAMEFLOW_API UExitAuthorityList : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Gameflow")
	TSet<FName> Authorities;

	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void AddAuthority(FName Authority);

	UFUNCTION(BlueprintCallable, Category = "Gameflow")
	void RemoveAuthority(FName Authority);

	UFUNCTION(BlueprintPure, Category = "Gameflow")
	bool IsEmpty() const { return Authorities.IsEmpty(); }
};
