#pragma once

#include "CoreMinimal.h"

/**
 * The rules of the gameflow transition queue, as pure functions over the caller's string array.
 *
 * The queue is a flat list of state strings, and its structure is carried by the strings
 * themselves. There are two operators:
 *
 *  - '/'  containment. "PMN/Step1" is inside "PMN" (UGameflowControllerComponent::IsStateWithin).
 *  - a reserved MARKER segment, "All" or "Any". Entries that share the prefix before the marker
 *    form a BLOCK of branches around an implicit HUB, which is that prefix:
 *
 *        AILibrary/Curated/Intro
 *        AILibrary/Curated/Pick                    <- optional explicit hub entry, popped like any other
 *        AILibrary/Curated/Pick/All/PlayA_Video    <- block, hub = AILibrary/Curated/Pick
 *        AILibrary/Curated/Pick/All/PlayB_Video
 *        AILibrary/Curated/Overview
 *
 *    All  entering a branch removes only that entry. From a branch, the next state is the hub
 *         while siblings remain, so the flow rests at the hub until something chooses the next
 *         branch. The block is done when it is empty: the join.
 *    Any  entering a branch removes the whole block. The next state is whatever follows. One choice.
 *
 * The hub is never stored. It is derived from the front entry every time it is needed, which is
 * why returning to it costs nothing and why nothing has to be cleared when a block finishes. From
 * the hub, the next state is the first remaining branch, so a Snap or bare Continue still walks a
 * block in authored order when nothing else chooses.
 *
 * The controller calls exactly two of these: NextState from Continue/Snap and Advance once a
 * transition is accepted. Everything here is stateless so the rules can be unit tested with no
 * controller, no delegates and no world (see HVPGameflowTestCommandlet).
 */
namespace GameflowQueueRules
{
	enum class EBlockKind : uint8
	{
		None,
		All,
		Any,
	};

	/** What one state string says about block membership. */
	struct FBranchInfo
	{
		EBlockKind Kind = EBlockKind::None;
		/** The prefix before the marker segment: the implicit hub. Empty for a plain state. */
		FString Hub;

		bool IsBranch() const { return Kind != EBlockKind::None; }
	};

	/** The marker segments, compared case-insensitively. */
	HVPGAMEFLOW_API extern const TCHAR* const MarkerAll;
	HVPGAMEFLOW_API extern const TCHAR* const MarkerAny;

	/**
	 * Parse one state. A marker that is the first or last segment does not make a branch (there is
	 * no hub, or no branch name); Validate reports those. A branch's substate ("Hub/All/X/Part2")
	 * parses as a branch of the same hub.
	 */
	HVPGAMEFLOW_API FBranchInfo Parse(const FString& State);

	/** Block membership of the queue's front entry; Kind None when the front is a plain entry. */
	HVPGAMEFLOW_API FBranchInfo FrontBlock(const TArray<FString>& Queue);

	/**
	 * The state the queue wants next from Current. The resolver behind ContinueToNextState and
	 * SnapToNextState:
	 *   - plain front entry            -> that entry
	 *   - block at front, from outside  -> the hub
	 *   - block at front, at the hub    -> the first remaining branch
	 *   - All block, from a branch      -> the hub (siblings remain by construction)
	 * Empty when the queue is empty.
	 */
	HVPGAMEFLOW_API FString NextState(const TArray<FString>& Queue, const FString& Current);

	/**
	 * Advance the queue past an accepted transition to Accepted.
	 *
	 * The queue is searched for the first entry Accepted lands on: an exact match, or a branch
	 * entry whose hub subtree contains Accepted. Entries before it were vaulted over and are
	 * dropped. Then:
	 *   - a plain entry is popped;
	 *   - the hub of a block (or a substate of it that is not a branch) leaves the block untouched;
	 *   - a branch of an All block removes the entries it lands on (normally exactly one);
	 *   - a branch of an Any block removes the whole block.
	 * An Accepted that lands nowhere clears the queue when bClearIfUnqueued (a demand went
	 * somewhere the queue never planned for) and leaves it alone otherwise (a request is the
	 * flow's own step).
	 */
	HVPGAMEFLOW_API void Advance(TArray<FString>& Queue, const FString& Accepted, bool bClearIfUnqueued);

	/** The remaining branches of the block at the front, in order; empty when the front is plain. */
	HVPGAMEFLOW_API TArray<FString> RemainingBranches(const TArray<FString>& Queue);

	/**
	 * Is State inside a branch block somewhere beneath Hub? True for "Pick/All/PlayA" against
	 * "Pick" and against "Curated"; false for "Pick" itself or "Pick/Foo". Consumers use this to
	 * mean "at the hub, not in a branch" when they gate on a hub subtree.
	 */
	HVPGAMEFLOW_API bool IsInsideBlockOf(const FString& State, const FString& Hub);

	/**
	 * Authoring problems in a queue, as human-readable warnings; empty when it is well formed.
	 * Catches a marker as first or last segment, nested markers, and a block whose branches are
	 * not contiguous.
	 */
	HVPGAMEFLOW_API TArray<FString> Validate(const TArray<FString>& Queue);
}
