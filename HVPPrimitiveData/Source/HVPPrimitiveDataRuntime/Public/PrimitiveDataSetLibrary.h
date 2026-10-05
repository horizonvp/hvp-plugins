#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PrimitiveDataSetLibrary.generated.h"

class UPrimitiveComponent;

/** Runtime support for Set Named Primitive Data (Multiple). Not meant to be placed by hand. */
UCLASS()
class HVPPRIMITIVEDATARUNTIME_API UPrimitiveDataSetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Writes the floats of Values whose bit is set in Mask into custom primitive data from DataIndex
	 * on, in ONE update per target. Floats whose bit is clear keep the target's current value: they are
	 * read back and written over themselves, which is what lets parameters with other parameters
	 * between them share a single write.
	 *
	 * Bit i of Mask is Values[i]. 36 floats of custom primitive data fit in 64 bits.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rendering|Material", meta = (BlueprintInternalUseOnly = "true"))
	static void SetCustomPrimitiveDataMasked(const TArray<UPrimitiveComponent*>& Targets, int32 DataIndex,
		const TArray<float>& Values, int64 Mask);

	/**
	 * The same, into each target's Custom Primitive Data Defaults: the values that are saved with the
	 * component and that its runtime data resets to. What a construction script wants - runtime data
	 * set there does not survive the level loading. Each call rebuilds the target's render state, so
	 * it is not for every frame.
	 */
	UFUNCTION(BlueprintCallable, Category = "Rendering|Material", meta = (BlueprintInternalUseOnly = "true"))
	static void SetDefaultCustomPrimitiveDataMasked(const TArray<UPrimitiveComponent*>& Targets, int32 DataIndex,
		const TArray<float>& Values, int64 Mask);
};
