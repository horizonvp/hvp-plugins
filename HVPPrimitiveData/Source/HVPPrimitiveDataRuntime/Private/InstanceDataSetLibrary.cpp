#include "InstanceDataSetLibrary.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/ObjectKey.h"

DEFINE_LOG_CATEGORY_STATIC(LogHVPInstanceData, Log, All);

void UInstanceDataSetLibrary::SetInstanceCustomDataMasked(const TArray<UInstancedStaticMeshComponent*>& Targets, int32 InstanceIndex,
	int32 DataIndex, const TArray<float>& Values, int64 Mask, bool bMarkRenderStateDirty)
{
	const uint64 Bits = static_cast<uint64>(Mask);
	if (DataIndex < 0 || Values.Num() == 0 || Bits == 0)
	{
		return;
	}
	// One past the last float this write owns.
	const int32 Needed = DataIndex + (64 - static_cast<int32>(FMath::CountLeadingZeros64(Bits)));

	TArray<float, TInlineAllocator<64>> Row;
	for (UInstancedStaticMeshComponent* Target : Targets)
	{
		if (!IsValid(Target) || !Target->IsValidInstance(InstanceIndex))
		{
			continue;
		}

		const int32 NumFloats = Target->NumCustomDataFloats;
		if (NumFloats < Needed)
		{
			// Once per mesh: this is usually set once in its Details panel and then called every frame.
			static TSet<FObjectKey> Warned;
			if (!Warned.Contains(Target))
			{
				Warned.Add(Target);
				UE_LOG(LogHVPInstanceData, Warning,
					TEXT("%s has %d custom data floats per instance, but Set Named Instance Data writes up to float %d. Raise its Num Custom Data Floats to at least the legend's Floats Per Instance."),
					*Target->GetPathName(), NumFloats, Needed - 1);
			}
		}
		const int32 Base = InstanceIndex * NumFloats;
		if (NumFloats == 0 || !Target->PerInstanceSMCustomData.IsValidIndex(Base + NumFloats - 1))
		{
			continue;
		}

		// The engine's SetCustomData writes a whole instance from float 0, so start from what is there.
		Row.Reset();
		Row.Append(&Target->PerInstanceSMCustomData[Base], NumFloats);
		const int32 Count = FMath::Min(Values.Num(), FMath::Min(64, NumFloats - DataIndex));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Bits & (uint64(1) << Index))
			{
				Row[DataIndex + Index] = Values[Index];
			}
		}
		Target->SetCustomData(InstanceIndex, TArrayView<const float>(Row), bMarkRenderStateDirty);
	}
}
