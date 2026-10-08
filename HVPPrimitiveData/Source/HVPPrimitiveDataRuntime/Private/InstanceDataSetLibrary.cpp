#include "InstanceDataSetLibrary.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/ObjectKey.h"

DEFINE_LOG_CATEGORY_STATIC(LogHVPInstanceData, Log, All);

namespace InstanceDataSetLibrary
{
	/** Once per mesh: Num Custom Data Floats is usually set once in its Details panel, and the write made every frame. */
	void WarnIfShort(const UInstancedStaticMeshComponent& Target, int32 Needed)
	{
		if (Target.NumCustomDataFloats >= Needed)
		{
			return;
		}
		static TSet<FObjectKey> Warned;
		if (!Warned.Contains(&Target))
		{
			Warned.Add(&Target);
			UE_LOG(LogHVPInstanceData, Warning,
				TEXT("%s has %d custom data floats per instance, but Set Named Instance Data writes up to float %d. Raise its Num Custom Data Floats to at least the legend's Floats Per Instance."),
				*Target.GetPathName(), Target.NumCustomDataFloats, Needed - 1);
		}
	}

	/** One past the last float Mask owns, counted from DataIndex. */
	int32 MaskEnd(uint64 Bits)
	{
		return Bits == 0 ? 0 : 64 - static_cast<int32>(FMath::CountLeadingZeros64(Bits));
	}
}

void UInstanceDataSetLibrary::SetInstanceCustomDataMasked(const TArray<UInstancedStaticMeshComponent*>& Targets, int32 InstanceIndex,
	int32 DataIndex, const TArray<float>& Values, int64 Mask, bool bMarkRenderStateDirty)
{
	const uint64 Bits = static_cast<uint64>(Mask);
	if (DataIndex < 0 || Values.Num() == 0 || Bits == 0)
	{
		return;
	}
	// One past the last float this write owns.
	const int32 Needed = DataIndex + InstanceDataSetLibrary::MaskEnd(Bits);

	TArray<float, TInlineAllocator<64>> Row;
	for (UInstancedStaticMeshComponent* Target : Targets)
	{
		if (!IsValid(Target) || !Target->IsValidInstance(InstanceIndex))
		{
			continue;
		}

		const int32 NumFloats = Target->NumCustomDataFloats;
		InstanceDataSetLibrary::WarnIfShort(*Target, Needed);
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

void UInstanceDataSetLibrary::SetInstanceCustomDataBatch(const TArray<UInstancedStaticMeshComponent*>& Targets,
	const TArray<int32>& InstanceIndices, int32 DataIndex, const TArray<float>& Uniform, int64 Mask,
	const TArray<FInstanceDataColumn>& Columns, bool bMarkRenderStateDirty)
{
	const uint64 Bits = static_cast<uint64>(Mask);
	if (DataIndex < 0 || InstanceIndices.Num() == 0 || (Bits == 0 && Columns.Num() == 0))
	{
		return;
	}

	int32 Needed = DataIndex + InstanceDataSetLibrary::MaskEnd(Bits);
	for (const FInstanceDataColumn& Column : Columns)
	{
		Needed = FMath::Max(Needed, DataIndex + Column.Offset + Column.Width);
	}

	// Consecutive ascending indices are one block of the custom data array, so one ranged write.
	bool bConsecutive = true;
	for (int32 k = 1; k < InstanceIndices.Num() && bConsecutive; ++k)
	{
		bConsecutive = InstanceIndices[k] == InstanceIndices[0] + k;
	}

	TArray<float> Block;
	for (UInstancedStaticMeshComponent* Target : Targets)
	{
		if (!IsValid(Target))
		{
			continue;
		}
		const int32 NumFloats = Target->NumCustomDataFloats;
		InstanceDataSetLibrary::WarnIfShort(*Target, Needed);
		if (NumFloats == 0)
		{
			continue;
		}

		// The k-th instance of the batch, its row at Row: uniform values, then each column's k-th value.
		// Floats past the mesh's Num Custom Data Floats are dropped.
		auto Fill = [&](float* Row, int32 k)
		{
			const int32 Count = FMath::Min(Uniform.Num(), FMath::Min(64, NumFloats - DataIndex));
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (Bits & (uint64(1) << Index))
				{
					Row[DataIndex + Index] = Uniform[Index];
				}
			}
			for (const FInstanceDataColumn& Column : Columns)
			{
				const int32 First = DataIndex + Column.Offset;
				if (Column.Width == 1)
				{
					if (Column.Scalars.IsValidIndex(k) && First >= 0 && First < NumFloats)
					{
						Row[First] = Column.Scalars[k];
					}
				}
				else if (Column.Colors.IsValidIndex(k))
				{
					const FLinearColor& Color = Column.Colors[k];
					const float Channels[] = { Color.R, Color.G, Color.B, Color.A };
					for (int32 Channel = 0; Channel < FMath::Min(Column.Width, 4); ++Channel)
					{
						if (First + Channel >= 0 && First + Channel < NumFloats)
						{
							Row[First + Channel] = Channels[Channel];
						}
					}
				}
			}
		};

		const int32 Start = InstanceIndices[0];
		const int32 End = InstanceIndices.Last();
		if (bConsecutive && Target->IsValidInstance(Start) && Target->IsValidInstance(End)
			&& Target->PerInstanceSMCustomData.Num() >= (End + 1) * NumFloats)
		{
			// The engine's ranged SetCustomData writes whole rows, so start from what is there.
			Block.Reset();
			Block.Append(&Target->PerInstanceSMCustomData[Start * NumFloats], (End - Start + 1) * NumFloats);
			for (int32 k = 0; k < InstanceIndices.Num(); ++k)
			{
				Fill(&Block[k * NumFloats], k);
			}
			Target->SetCustomData(Start, End, TConstArrayView<float>(Block), bMarkRenderStateDirty);
			continue;
		}

		// Any other order: row by row, marking the render state dirty (if asked) only on the last write.
		int32 Last = INDEX_NONE;
		for (int32 k = InstanceIndices.Num() - 1; k >= 0 && Last == INDEX_NONE; --k)
		{
			Last = Target->IsValidInstance(InstanceIndices[k]) ? k : INDEX_NONE;
		}
		for (int32 k = 0; k <= Last; ++k)
		{
			const int32 Instance = InstanceIndices[k];
			if (!Target->IsValidInstance(Instance) || Target->PerInstanceSMCustomData.Num() < (Instance + 1) * NumFloats)
			{
				continue;
			}
			Block.Reset();
			Block.Append(&Target->PerInstanceSMCustomData[Instance * NumFloats], NumFloats);
			Fill(Block.GetData(), k);
			Target->SetCustomData(Instance, TArrayView<const float>(Block), bMarkRenderStateDirty && k == Last);
		}
	}
}
