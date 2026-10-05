#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UCodeAnimationWeb;
class UEdGraph;

/**
 * Creating and opening a Web's graphs: state graphs, transition graphs and Custom Lerp graphs.
 *
 * Each is an ordinary function graph in the Web's Blueprint, listed under "Code Animation Web" in
 * My Blueprint, with its inputs already in place. The Web holds it by graph GUID, so renaming the
 * function does not unlink it; deleting it does, on the next compile.
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
}
