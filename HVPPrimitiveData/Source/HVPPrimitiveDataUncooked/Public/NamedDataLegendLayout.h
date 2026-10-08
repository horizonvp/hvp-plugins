#pragma once

#include "CoreMinimal.h"

/**
 * What the Primitive Data Legend and the Instance Data Legend have in common: entries with a name, an
 * identity that survives a rename, and a slot allocated for them that never moves unless the entry
 * itself changed. Written once here, as templates over the entry type, so the two legends cannot drift
 * apart in how they hand out slots.
 *
 * An entry type needs: FName Name, FGuid Id, a uint8-backed enum Type, int32 Slot, int32 AllocatedWidth,
 * GetWidth() and HasSlot().
 */
namespace NamedDataLegendLayout
{
	/**
	 * Ids present and unique, names present. True if anything changed.
	 *
	 * "Duplicate" on an array element copies the Id along with everything else, and it is the COPY - the
	 * later one - that must get a fresh identity, or the original's nodes would start following it.
	 */
	template <typename TEntry>
	bool NormalizeIdentities(TArray<TEntry>& Entries)
	{
		bool bChanged = false;

		TSet<FGuid> SeenIds;
		TSet<FName> SeenNames;
		for (TEntry& Entry : Entries)
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
		for (TEntry& Entry : Entries)
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

		return bChanged;
	}

	/**
	 * Give every entry a slot within Capacity floats. The rule is that only the entry that changed may
	 * move. Three passes:
	 *   1. entries whose width is what their slot was allocated for keep it - they did not change, so a
	 *      neighbour growing into them must not evict them;
	 *   2. entries that changed width keep their slot if the new width still fits there;
	 *   3. everything left is allocated first-fit.
	 * No alignment: the material translator reads every component individually, so a vector starting
	 * mid-float4 costs nothing extra. An entry left at INDEX_NONE did not fit. True if anything changed.
	 */
	template <typename TEntry>
	bool AllocateSlots(TArray<TEntry>& Entries, int32 Capacity)
	{
		bool bChanged = false;
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
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			TEntry& Entry = Entries[i];
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
			TEntry& Entry = Entries[i];
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
			TEntry& Entry = Entries[i];
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
			bChanged |= (Entry.Slot != Previous);
		}

		return bChanged;
	}

	/** Floats claimed by entries that have a slot. */
	template <typename TEntry>
	int32 UsedFloats(const TArray<TEntry>& Entries)
	{
		int32 Used = 0;
		for (const TEntry& Entry : Entries)
		{
			if (Entry.HasSlot())
			{
				Used += Entry.GetWidth();
			}
		}
		return Used;
	}

	/** Hash of what nodes and bound materials depend on: identity, name, type and slot. */
	template <typename TEntry>
	uint32 HashLayout(const TArray<TEntry>& Entries)
	{
		uint32 Hash = 0;
		for (const TEntry& Entry : Entries)
		{
			Hash = HashCombine(Hash, GetTypeHash(Entry.Id));
			Hash = HashCombine(Hash, GetTypeHash(Entry.Name));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Entry.Type)));
			Hash = HashCombine(Hash, GetTypeHash(Entry.Slot));
		}
		return Hash;
	}

	/** Each entry's current name by identity: the baseline the next edit's renames are found against. */
	template <typename TEntry>
	void SnapshotNames(const TArray<TEntry>& Entries, TMap<FGuid, FName>& OutNames)
	{
		OutNames.Reset();
		for (const TEntry& Entry : Entries)
		{
			OutNames.Add(Entry.Id, Entry.Name);
		}
	}

	/**
	 * Renames by identity: same Id, different name. Handed to listeners so bound materials can have the
	 * matching parameter relabelled rather than reported as one missing and one stray.
	 */
	template <typename TEntry>
	TMap<FName, FName> FindRenames(const TArray<TEntry>& Entries, const TMap<FGuid, FName>& Before)
	{
		TMap<FName, FName> Renames;
		for (const TEntry& Entry : Entries)
		{
			if (const FName* Previous = Before.Find(Entry.Id))
			{
				if (*Previous != Entry.Name && !Previous->IsNone())
				{
					Renames.Add(*Previous, Entry.Name);
				}
			}
		}
		return Renames;
	}
}
