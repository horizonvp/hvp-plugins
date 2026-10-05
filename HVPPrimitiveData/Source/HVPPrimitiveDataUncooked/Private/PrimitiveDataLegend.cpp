#include "PrimitiveDataLegend.h"

#include "Misc/DataValidation.h"
#include "SceneTypes.h"

#define LOCTEXT_NAMESPACE "PrimitiveDataLegend"

FOnPrimitiveDataLegendChanged UPrimitiveDataLegend::OnChanged;

int32 UPrimitiveDataLegend::GetCapacity()
{
	return FCustomPrimitiveData::NumCustomPrimitiveDataFloats;
}

const FPrimitiveDataLegendEntry* UPrimitiveDataLegend::FindParameter(const FGuid& Id) const
{
	if (!Id.IsValid())
	{
		return nullptr;
	}
	return Parameters.FindByPredicate([&Id](const FPrimitiveDataLegendEntry& E) { return E.Id == Id; });
}

const FPrimitiveDataLegendEntry* UPrimitiveDataLegend::FindParameter(FName Name) const
{
	if (Name.IsNone())
	{
		return nullptr;
	}
	return Parameters.FindByPredicate([Name](const FPrimitiveDataLegendEntry& E) { return E.Name == Name; });
}

int32 UPrimitiveDataLegend::GetUsedFloats() const
{
	int32 Used = 0;
	for (const FPrimitiveDataLegendEntry& Entry : Parameters)
	{
		if (Entry.HasSlot())
		{
			Used += Entry.GetWidth();
		}
	}
	return Used;
}

void UPrimitiveDataLegend::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		TakeSnapshot();
	}
}

void UPrimitiveDataLegend::PostLoad()
{
	Super::PostLoad();
	Normalize();
	TakeSnapshot();
}

bool UPrimitiveDataLegend::Normalize()
{
	bool bChanged = false;

	// Ids: present and unique. "Duplicate" on an array element copies the Id along with everything
	// else, and it is the COPY - the later one - that must get a fresh identity, or the original's
	// nodes would start following it.
	TSet<FGuid> SeenIds;
	TSet<FName> SeenNames;
	for (FPrimitiveDataLegendEntry& Entry : Parameters)
	{
		if (!Entry.Id.IsValid() || SeenIds.Contains(Entry.Id))
		{
			Entry.Id = FGuid::NewGuid();
			bChanged = true;
		}
		SeenIds.Add(Entry.Id);
		SeenNames.Add(Entry.Name);
	}

	// A fresh "+" element arrives nameless; give it something a node's dropdown can show.
	for (FPrimitiveDataLegendEntry& Entry : Parameters)
	{
		if (Entry.Name.IsNone())
		{
			int32 Suffix = 0;
			FName Candidate(TEXT("Parameter"));
			while (SeenNames.Contains(Candidate))
			{
				Candidate = FName(*FString::Printf(TEXT("Parameter_%d"), ++Suffix));
			}
			Entry.Name = Candidate;
			SeenNames.Add(Candidate);
			bChanged = true;
		}
	}

	// Slots. The rule is that only the entry that changed may move. Three passes:
	//   1. entries whose width is what their slot was allocated for keep it - they did not change, so
	//      a neighbour growing into them must not evict them;
	//   2. entries that changed width keep their slot if the new width still fits there;
	//   3. everything left is allocated first-fit.
	// No alignment: the material translator reads every component individually
	// (CustomPrimitiveData[i / 4][i % 4]), so a vector starting mid-float4 costs nothing extra.
	const int32 Capacity = GetCapacity();
	TBitArray<> Used(false, Capacity);

	auto Fits = [&Used, Capacity](int32 Start, int32 Width)
	{
		if (Start < 0 || Start + Width > Capacity)
		{
			return false;
		}
		for (int32 i = Start; i < Start + Width; ++i)
		{
			if (Used[i])
			{
				return false;
			}
		}
		return true;
	};
	auto Claim = [&Used](int32 Start, int32 Width)
	{
		for (int32 i = Start; i < Start + Width; ++i)
		{
			Used[i] = true;
		}
	};

	TArray<int32> Pending;
	TArray<int32> Resized;
	for (int32 i = 0; i < Parameters.Num(); ++i)
	{
		FPrimitiveDataLegendEntry& Entry = Parameters[i];
		const int32 Width = Entry.GetWidth();
		if (Entry.HasSlot() && Entry.AllocatedWidth == Width && Fits(Entry.Slot, Width))
		{
			Claim(Entry.Slot, Width);
		}
		else if (Entry.HasSlot() && Entry.AllocatedWidth != Width)
		{
			Resized.Add(i);
		}
		else
		{
			Pending.Add(i);
		}
	}

	for (int32 i : Resized)
	{
		FPrimitiveDataLegendEntry& Entry = Parameters[i];
		const int32 Width = Entry.GetWidth();
		if (Fits(Entry.Slot, Width))
		{
			Claim(Entry.Slot, Width);
			Entry.AllocatedWidth = Width;
			bChanged = true;
		}
		else
		{
			Pending.Add(i);
		}
	}

	for (int32 i : Pending)
	{
		FPrimitiveDataLegendEntry& Entry = Parameters[i];
		const int32 Width = Entry.GetWidth();
		const int32 Previous = Entry.Slot;

		Entry.Slot = INDEX_NONE;
		for (int32 Start = 0; Start + Width <= Capacity; ++Start)
		{
			if (Fits(Start, Width))
			{
				Entry.Slot = Start;
				Entry.AllocatedWidth = Width;
				Claim(Start, Width);
				break;
			}
		}
		// Still INDEX_NONE means the legend is full; IsDataValid and the node both report it.
		bChanged |= (Entry.Slot != Previous);
	}

	return bChanged;
}

uint32 UPrimitiveDataLegend::ComputeLayoutHash() const
{
	uint32 Hash = 0;
	for (const FPrimitiveDataLegendEntry& Entry : Parameters)
	{
		Hash = HashCombine(Hash, GetTypeHash(Entry.Id));
		Hash = HashCombine(Hash, GetTypeHash(Entry.Name));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Entry.Type)));
		Hash = HashCombine(Hash, GetTypeHash(Entry.Slot));
	}
	return Hash;
}

uint32 UPrimitiveDataLegend::ComputeBindingHash() const
{
	uint32 Hash = GetTypeHash(bRequireAllParameters);
	for (const TSoftObjectPtr<UObject>& Bound : BoundMaterials)
	{
		Hash = HashCombine(Hash, GetTypeHash(Bound.ToSoftObjectPath()));
	}
	return Hash;
}

void UPrimitiveDataLegend::TakeSnapshot()
{
	LastLayoutHash = ComputeLayoutHash();
	LastBindingHash = ComputeBindingHash();
	LastNames.Reset();
	for (const FPrimitiveDataLegendEntry& Entry : Parameters)
	{
		LastNames.Add(Entry.Id, Entry.Name);
	}
}

#if WITH_EDITOR

void UPrimitiveDataLegend::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);

	// Interactive edits (dragging a value) settle later with a ValueSet; nothing here is dragged, but
	// there is no reason to re-sync materials mid-gesture if something ever is.
	if (Event.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	Normalize();

	const bool bLayoutChanged = ComputeLayoutHash() != LastLayoutHash;
	const bool bBindingChanged = ComputeBindingHash() != LastBindingHash;
	if (!bLayoutChanged && !bBindingChanged)
	{
		return;
	}

	// Renames by identity: same Id, different name. Handed to listeners so bound materials can have
	// the matching parameter relabelled rather than reported as one missing and one stray.
	TMap<FName, FName> Renames;
	for (const FPrimitiveDataLegendEntry& Entry : Parameters)
	{
		if (const FName* Before = LastNames.Find(Entry.Id))
		{
			if (*Before != Entry.Name && !Before->IsNone())
			{
				Renames.Add(*Before, Entry.Name);
			}
		}
	}

	TakeSnapshot();
	OnChanged.Broadcast(this, Renames, bLayoutChanged);
}

EDataValidationResult UPrimitiveDataLegend::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FName> Names;
	for (const FPrimitiveDataLegendEntry& Entry : Parameters)
	{
		if (Names.Contains(Entry.Name))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateName", "'{0}' is named twice. Materials match parameters by name, so each must be unique."),
				FText::FromName(Entry.Name)));
			Result = EDataValidationResult::Invalid;
		}
		Names.Add(Entry.Name);

		if (!Entry.HasSlot())
		{
			Context.AddError(FText::Format(
				LOCTEXT("NoRoom", "'{0}' has no slot: a primitive has {1} floats of custom data and the other parameters use {2}. A vector needs four in a row."),
				FText::FromName(Entry.Name), FText::AsNumber(GetCapacity()), FText::AsNumber(GetUsedFloats())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
