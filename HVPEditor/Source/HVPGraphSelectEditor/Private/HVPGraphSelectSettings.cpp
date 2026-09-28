#include "HVPGraphSelectSettings.h"

#include "Input/Events.h"

bool UHVPGraphSelectSettings::IsModifierDown(
	EHVPModifierKey Modifier, const FInputEvent& Event)
{
	return IsModifierDown(Modifier, Event.GetModifierKeys());
}

bool UHVPGraphSelectSettings::IsModifierDown(
	EHVPModifierKey Modifier, const FModifierKeysState& State)
{
	switch (Modifier)
	{
	case EHVPModifierKey::Shift:	return State.IsShiftDown();
	case EHVPModifierKey::Control:	return State.IsControlDown();
	case EHVPModifierKey::Alt:		return State.IsAltDown();
	default:							return false;	// None: unassigned reads as disabled
	}
}
