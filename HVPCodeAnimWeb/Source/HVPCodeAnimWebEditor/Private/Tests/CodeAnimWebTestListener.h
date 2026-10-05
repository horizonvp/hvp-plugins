#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "CodeAnimWebTestListener.generated.h"

class UCodeAnimationWeb;

/**
 * Something for the tests to bind the Web's dynamic delegates to, which only take UFUNCTIONs. Records
 * every call. Lives in the editor module with the tests; nothing else uses it.
 */
UCLASS(Transient)
class UCodeAnimWebTestListener : public UObject
{
	GENERATED_BODY()

public:
	/** The Changed Outputs of each call received, in order. */
	TArray<TArray<FName>> OutputChangedCalls;

	UFUNCTION()
	void HandleOutputChanged(UCodeAnimationWeb* Web, const TArray<FName>& ChangedOutputs)
	{
		OutputChangedCalls.Add(ChangedOutputs);
	}
};
