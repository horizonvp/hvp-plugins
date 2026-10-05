#pragma once

#include "CoreMinimal.h"

class UBlueprint;

/**
 * Each Animation Output's own event dispatcher: On <Output> Changed (Web, New <Output>), a real
 * dispatcher on the Web Blueprint, so it works everywhere dispatchers do - the "+" events in a
 * component's Details panel, Bind Event to / Assign nodes with the red delegate pin, and Call. Plus one
 * for all of them, On Outputs Changed (Web, Changed Outputs), fired once per update.
 *
 * Real dispatchers rather than delegates declared in C++ because only they can carry the Web as its own
 * Blueprint class, so a listener can read its outputs without a cast.
 *
 * The Web makes and maintains these itself. A dispatcher is tied to its output by the output's
 * variable GUID (stamped on the dispatcher as metadata), so renaming the output renames the dispatcher,
 * changing its type retypes the value parameter, and un-ticking Animation Output removes it.
 */
namespace CodeAnimWebEvents
{
	/** Brings a Web Blueprint's dispatchers in line with its outputs. True if anything changed (it then needs compiling). */
	bool Reconcile(UBlueprint* Blueprint);

	/** The dispatcher name an output would get: On<Output>Changed. */
	FName EventNameFor(FName OutputName);

	/** The name the all-outputs dispatcher gets, if free. */
	inline const FName OutputsChangedEventName(TEXT("OnOutputsChanged"));
}
