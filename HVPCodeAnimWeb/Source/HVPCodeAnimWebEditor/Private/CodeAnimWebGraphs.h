#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UCodeAnimationWeb;
class UEdGraph;

/**
 * Creating, opening and removing a Web's graphs: state graphs, transition graphs and Custom Lerp graphs.
 *
 * Each is an ordinary function graph in the Web's Blueprint, listed under "Code Animation Web" in
 * My Blueprint, with its inputs already in place. The Web holds it by graph GUID, so renaming the
 * function does not unlink it; deleting the function unlinks it, on the next compile. The other way
 * round, deleting a transition, or taking an output off Custom, deletes its graph.
 */
namespace CodeAnimWebGraphs
{
	UEdGraph* FindGraph(const UBlueprint* Blueprint, const FGuid& Guid);

	/** "Open Graph" or "+ Graph", for a button that does one or the other. */
	FText ButtonLabel(const UBlueprint* Blueprint, const FGuid& Guid);

	/** State Values row -> its state graph. */
	UEdGraph* OpenOrCreateStateGraph(UCodeAnimationWeb* Defaults, FName StateKey, bool bOpen = true);

	/** Transitions row -> its transition graph. */
	UEdGraph* OpenOrCreateTransitionGraph(UCodeAnimationWeb* Defaults, int32 TransitionIndex, bool bOpen = true);

	/** A Custom-lerp output -> its Custom Lerp graph. */
	UEdGraph* OpenOrCreateCustomLerpGraph(UBlueprint* Blueprint, FName OutputName, bool bOpen = true);

	/** The Custom Lerp graph's GUID for an output, or an invalid GUID. */
	FGuid FindCustomLerpGraph(UBlueprint* Blueprint, FName OutputName);

	/**
	 * Delete every graph this Web made whose owner is gone: a transition row removed, an output no
	 * longer Custom (its Custom Lerp entry goes too), or a state row removed. Only graphs the Web made
	 * (UCodeAnimationWeb::OwnedGraphs) are candidates. Graphs still in use but made before the Web kept
	 * that record are adopted into it first. Call inside a transaction so the deletion undoes with
	 * whatever caused it. True if anything was removed.
	 */
	bool RemoveUnusedGraphs(UBlueprint* Blueprint);

	/**
	 * Rename the Web's graphs to match their owners as they are now: a transition's ends and direction
	 * (Transition_A_To_B / Transition_A_And_B), a state's display name, a Custom Lerp's output. Only
	 * graphs the Web made, and only while they still carry its Transition_ / State_ / Lerp_ prefix -
	 * one renamed by hand to something else is left alone. Call inside a transaction. True if any
	 * were renamed.
	 */
	bool RenameGraphsToMatch(UBlueprint* Blueprint);
}
