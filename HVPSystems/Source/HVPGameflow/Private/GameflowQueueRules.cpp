#include "GameflowQueueRules.h"

#include "GameflowControllerComponent.h"

namespace GameflowQueueRules
{
	const TCHAR* const MarkerAll = TEXT("All");
	const TCHAR* const MarkerAny = TEXT("Any");

	namespace
	{
		bool Within(const FString& State, const FString& Parent)
		{
			return UGameflowControllerComponent::IsStateWithin(State, Parent);
		}

		bool SameState(const FString& A, const FString& B)
		{
			return A.Equals(B, ESearchCase::IgnoreCase);
		}

		EBlockKind MarkerKind(const FString& Segment)
		{
			if (Segment.Equals(MarkerAll, ESearchCase::IgnoreCase))
			{
				return EBlockKind::All;
			}
			if (Segment.Equals(MarkerAny, ESearchCase::IgnoreCase))
			{
				return EBlockKind::Any;
			}
			return EBlockKind::None;
		}

		/** Is Queue[Index] a branch of the block whose hub is Hub? */
		bool IsBranchOf(const FString& Entry, const FString& Hub)
		{
			const FBranchInfo Info = Parse(Entry);
			return Info.IsBranch() && SameState(Info.Hub, Hub);
		}

		/** Index one past the run of Hub's branches that starts at Start. */
		int32 BlockEnd(const TArray<FString>& Queue, int32 Start, const FString& Hub)
		{
			int32 End = Start;
			while (End < Queue.Num() && IsBranchOf(Queue[End], Hub))
			{
				++End;
			}
			return End;
		}
	}

	FBranchInfo Parse(const FString& State)
	{
		FBranchInfo Info;
		TArray<FString> Segments;
		State.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty=*/false);

		// The marker needs a hub before it and a branch name after it.
		for (int32 i = 1; i + 1 < Segments.Num(); ++i)
		{
			const EBlockKind Kind = MarkerKind(Segments[i]);
			if (Kind == EBlockKind::None)
			{
				continue;
			}
			Info.Kind = Kind;
			Info.Hub = FString::Join(TArrayView<const FString>(Segments.GetData(), i), TEXT("/"));
			break;
		}
		return Info;
	}

	FBranchInfo FrontBlock(const TArray<FString>& Queue)
	{
		return Queue.IsEmpty() ? FBranchInfo() : Parse(Queue[0]);
	}

	FString NextState(const TArray<FString>& Queue, const FString& Current)
	{
		if (Queue.IsEmpty())
		{
			return FString();
		}
		const FBranchInfo Front = FrontBlock(Queue);
		if (!Front.IsBranch())
		{
			return Queue[0];
		}

		// Arriving from outside the hub: enter the hub first, whether or not it was listed.
		if (!Within(Current, Front.Hub))
		{
			return Front.Hub;
		}

		// In a branch of an All block: back to the hub. (An Any block is gone the moment one of its
		// branches is entered, so a front Any block means we are not in one of its branches.)
		if (Front.Kind == EBlockKind::All && IsBranchOf(Current, Front.Hub))
		{
			return Front.Hub;
		}

		// At the hub: the first remaining branch, so a skip-walk still visits every branch in order.
		return Queue[0];
	}

	void Advance(TArray<FString>& Queue, const FString& Accepted, bool bClearIfUnqueued)
	{
		if (Queue.IsEmpty())
		{
			return;
		}

		// The first entry Accepted lands on: an exact match, or a block whose hub subtree
		// contains it. Everything before it was vaulted over.
		int32 Index = INDEX_NONE;
		for (int32 i = 0; i < Queue.Num(); ++i)
		{
			if (SameState(Queue[i], Accepted))
			{
				Index = i;
				break;
			}
			const FBranchInfo Entry = Parse(Queue[i]);
			if (Entry.IsBranch() && Within(Accepted, Entry.Hub))
			{
				Index = i;
				break;
			}
		}

		if (Index == INDEX_NONE)
		{
			if (bClearIfUnqueued)
			{
				Queue.Empty();
			}
			return;
		}
		if (Index > 0)
		{
			Queue.RemoveAt(0, Index);
		}

		const FBranchInfo Front = Parse(Queue[0]);
		if (!Front.IsBranch() || SameState(Queue[0], Accepted))
		{
			// A plain entry (an explicitly listed hub included) is simply popped.
			Queue.RemoveAt(0);
			return;
		}

		if (!IsBranchOf(Accepted, Front.Hub))
		{
			// The hub itself, or a substate of it that is not a branch: the block stays whole.
			return;
		}

		const int32 End = BlockEnd(Queue, 0, Front.Hub);
		if (Front.Kind == EBlockKind::Any)
		{
			Queue.RemoveAt(0, End);
			return;
		}
		for (int32 i = End - 1; i >= 0; --i)
		{
			if (Within(Accepted, Queue[i]))
			{
				Queue.RemoveAt(i);
			}
		}
	}

	TArray<FString> RemainingBranches(const TArray<FString>& Queue)
	{
		TArray<FString> Out;
		const FBranchInfo Front = FrontBlock(Queue);
		if (Front.IsBranch())
		{
			const int32 End = BlockEnd(Queue, 0, Front.Hub);
			Out.Append(Queue.GetData(), End);
		}
		return Out;
	}

	bool IsInsideBlockOf(const FString& State, const FString& Hub)
	{
		const FBranchInfo Info = Parse(State);
		return Info.IsBranch() && Within(Info.Hub, Hub);
	}

	TArray<FString> Validate(const TArray<FString>& Queue)
	{
		TArray<FString> Warnings;
		TSet<FString> ClosedHubs; // hubs whose block has ended (case-folded)
		FString OpenHub;

		for (const FString& Entry : Queue)
		{
			TArray<FString> Segments;
			Entry.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty=*/false);
			int32 Markers = 0;
			for (int32 i = 0; i < Segments.Num(); ++i)
			{
				if (MarkerKind(Segments[i]) == EBlockKind::None)
				{
					continue;
				}
				++Markers;
				if (i == 0)
				{
					Warnings.Add(FString::Printf(TEXT("'%s': marker '%s' has no hub before it"), *Entry, *Segments[i]));
				}
				else if (i + 1 == Segments.Num())
				{
					Warnings.Add(FString::Printf(TEXT("'%s': marker '%s' has no branch after it"), *Entry, *Segments[i]));
				}
			}
			if (Markers > 1)
			{
				Warnings.Add(FString::Printf(TEXT("'%s': nested blocks are not supported (only the first marker counts)"), *Entry));
			}

			const FBranchInfo Info = Parse(Entry);
			const FString Hub = Info.IsBranch() ? Info.Hub.ToLower() : FString();
			if (Hub != OpenHub)
			{
				if (!OpenHub.IsEmpty())
				{
					ClosedHubs.Add(OpenHub);
				}
				if (!Hub.IsEmpty() && ClosedHubs.Contains(Hub))
				{
					Warnings.Add(FString::Printf(TEXT("'%s': branches of '%s' are not contiguous"), *Entry, *Info.Hub));
				}
				OpenHub = Hub;
			}
		}
		return Warnings;
	}
}
