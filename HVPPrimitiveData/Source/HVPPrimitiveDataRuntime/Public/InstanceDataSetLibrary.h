#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "InstanceDataSetLibrary.generated.h"

class UInstancedStaticMeshComponent;

/** Runtime support for Set Named Instance Data (Multiple). Not meant to be placed by hand. */
UCLASS()
class HVPPRIMITIVEDATARUNTIME_API UInstanceDataSetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Writes the floats of Values whose bit is set in Mask into one instance's custom data from DataIndex
	 * on, as ONE write per target rather than one SetCustomDataValue per float. Floats whose bit is clear
	 * keep the instance's current value.
	 *
	 * Bit i of Mask is Values[i]. A target with too few Num Custom Data Floats for the write has what fits
	 * written, and a warning logged once.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|InstancedStaticMesh", meta = (BlueprintInternalUseOnly = "true"))
	static void SetInstanceCustomDataMasked(const TArray<UInstancedStaticMeshComponent*>& Targets, int32 InstanceIndex,
		int32 DataIndex, const TArray<float>& Values, int64 Mask, bool bMarkRenderStateDirty);
};
