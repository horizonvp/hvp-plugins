#pragma once

#include "CoreMinimal.h"

class FKismetCompilerContext;
class UEdGraph;
class UEdGraphPin;
class UK2Node;

/**
 * Expansion steps the Set Named ... nodes share, so the primitive and instance versions wire their
 * intermediate graphs the same way.
 */
namespace NamedDataNodeExpansion
{
	/**
	 * Unwired Target means self, as on any engine node: fine when the Blueprint being compiled IS a
	 * TargetClass, an error anywhere else. Checked by the node rather than left to the intermediate call
	 * so the message names the node the user placed. False after reporting.
	 */
	bool CheckSelfTarget(FKismetCompilerContext& CompilerContext, UK2Node* Node, UEdGraphPin* Target, const UClass* TargetClass,
		const FText& Message);

	/**
	 * Hand Target's links to a static function's array-of-targets input: an array straight through, any
	 * number of single wires (or self, unwired) gathered with a Make Array. False, after reporting it, when
	 * an array is mixed with singles; a connection that failed clears bWired instead.
	 */
	bool GatherTargets(FKismetCompilerContext& CompilerContext, UK2Node* Node, UEdGraph* SourceGraph, UEdGraphPin* Target,
		UEdGraphPin* TargetsInput, bool& bWired);

	/** One parameter's value going into the float array: Width 1 is a float pin, 3 or 4 a Linear Color pin. */
	struct FValueWrite
	{
		UEdGraphPin* Pin = nullptr;
		int32 Offset = 0;
		int32 Width = 1;
	};

	/**
	 * A Make Array of Count floats into ValuesInput, each write landing at its offset - colours broken into
	 * their channels with the engine's BreakColor, so the cooked graph needs nothing else. Floats no write
	 * covers stay zero. False if anything failed to wire.
	 */
	bool WireValues(FKismetCompilerContext& CompilerContext, UK2Node* Node, UEdGraph* SourceGraph, UEdGraphPin* ValuesInput,
		int32 Count, TConstArrayView<FValueWrite> Writes);
}
