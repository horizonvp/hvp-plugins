#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "InstanceDataSetLibrary.generated.h"

class UInstancedStaticMeshComponent;

/**
 * One parameter of Set Named Instance Data (Batch) that has a value per instance: its floats' offset
 * within the write, its width, and the values - Scalars for a width of 1, Colors (red, green, blue) for
 * 3. Built by the node's expansion; a function cannot take a variable number of arrays any other way.
 */
USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct HVPPRIMITIVEDATARUNTIME_API FInstanceDataColumn
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Instance Data")
	int32 Offset = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Instance Data")
	int32 Width = 1;

	UPROPERTY(BlueprintReadWrite, Category = "Instance Data")
	TArray<float> Scalars;

	UPROPERTY(BlueprintReadWrite, Category = "Instance Data")
	TArray<FLinearColor> Colors;
};

/** Runtime support for the Set Named Instance Data nodes. Not meant to be placed by hand. */
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

	/**
	 * The batch form: the same floats for every instance in InstanceIndices, in one call per target.
	 *
	 * Uniform holds the values shared by every instance, bit i of Mask owning Uniform[i] as above. Each
	 * column then lays its own value per instance over that - the k-th instance in InstanceIndices takes
	 * the column's k-th value; an instance past the end of a column's array keeps that column's floats as
	 * they are. Floats neither owns keep their values.
	 *
	 * Consecutive ascending indices are written as one range (the engine's ranged SetCustomData); any
	 * other order is written instance by instance, still without leaving C++. Invalid indices are skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Components|InstancedStaticMesh", meta = (BlueprintInternalUseOnly = "true", AutoCreateRefTerm = "Uniform,Columns"))
	static void SetInstanceCustomDataBatch(const TArray<UInstancedStaticMeshComponent*>& Targets, const TArray<int32>& InstanceIndices,
		int32 DataIndex, const TArray<float>& Uniform, int64 Mask, const TArray<FInstanceDataColumn>& Columns, bool bMarkRenderStateDirty);
};
