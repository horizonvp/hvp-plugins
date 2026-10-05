#include "PrimitiveDataSetLibrary.h"

#include "Components/PrimitiveComponent.h"
#include "SceneTypes.h"

namespace PrimitiveDataSetLibrary
{
	/**
	 * Merge Values over the floats Read returns for each target, keeping the ones Mask does not own,
	 * and hand the run to Write - one write per target.
	 */
	template <typename TRead, typename TWrite>
	void SetMasked(const TArray<UPrimitiveComponent*>& Targets, int32 DataIndex, const TArray<float>& Values, int64 Mask,
		TRead Read, TWrite Write)
	{
		if (DataIndex < 0 || Values.Num() == 0)
		{
			return;
		}
		// The engine ignores anything past the end; so does the mask, which only has room for that many.
		const int32 Count = FMath::Min(Values.Num(), FCustomPrimitiveData::NumCustomPrimitiveDataFloats - DataIndex);
		if (Count <= 0)
		{
			return;
		}

		TArray<float, TInlineAllocator<FCustomPrimitiveData::NumCustomPrimitiveDataFloats>> Merged;
		Merged.SetNumUninitialized(Count);
		for (UPrimitiveComponent* Target : Targets)
		{
			if (!IsValid(Target))
			{
				continue;
			}
			// Floats never set read as zero, which is also what the engine fills them with.
			const TArray<float>& Current = Read(*Target);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const bool bOwned = (Mask & (int64(1) << Index)) != 0;
				Merged[Index] = bOwned ? Values[Index]
					: (Current.IsValidIndex(DataIndex + Index) ? Current[DataIndex + Index] : 0.f);
			}
			Write(*Target, TConstArrayView<float>(Merged));
		}
	}
}

void UPrimitiveDataSetLibrary::SetCustomPrimitiveDataMasked(const TArray<UPrimitiveComponent*>& Targets, int32 DataIndex,
	const TArray<float>& Values, int64 Mask)
{
	PrimitiveDataSetLibrary::SetMasked(Targets, DataIndex, Values, Mask,
		[](const UPrimitiveComponent& Target) -> const TArray<float>& { return Target.GetCustomPrimitiveData().Data; },
		// Skips the render update itself when nothing actually changed.
		[DataIndex](UPrimitiveComponent& Target, TConstArrayView<float> Run) { Target.SetCustomPrimitiveDataFloatArray(DataIndex, Run); });
}

void UPrimitiveDataSetLibrary::SetDefaultCustomPrimitiveDataMasked(const TArray<UPrimitiveComponent*>& Targets, int32 DataIndex,
	const TArray<float>& Values, int64 Mask)
{
	PrimitiveDataSetLibrary::SetMasked(Targets, DataIndex, Values, Mask,
		[](const UPrimitiveComponent& Target) -> const TArray<float>& { return Target.GetDefaultCustomPrimitiveData().Data; },
		[DataIndex](UPrimitiveComponent& Target, TConstArrayView<float> Run) { Target.SetDefaultCustomPrimitiveDataFloatArray(DataIndex, Run); });
}
