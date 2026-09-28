#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"

#include "HVPGraphSelectSettings.generated.h"

struct FInputEvent;
class FModifierKeysState;

/**
 * A held modifier, rather than a specific key.
 *
 * Deliberately not an FKey: the question being asked is "is shift down", which both shift keys
 * answer, and an FKey would force a choice between LeftShift and RightShift that nobody wants to
 * make. Slate reports modifier STATE, so this matches what can actually be tested.
 */
UENUM()
enum class EHVPModifierKey : uint8
{
	None	UMETA(DisplayName = "None"),
	Shift	UMETA(DisplayName = "Shift"),
	Control	UMETA(DisplayName = "Ctrl"),
	Alt		UMETA(DisplayName = "Alt"),
};

/**
 * Project Settings -> Plugins -> HVP Graph Select.
 *
 * Two tools share one gesture. TREE SELECT follows wires from the node under the cursor; SELECTION
 * CUT slices the graph along a line and takes everything past it. What they share - the button a
 * drag starts with, how far it must travel, how big the overlay is - lives under Gesture.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "HVP Graph Select"))
class UHVPGraphSelectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	//~=========================================================================
	//~ Tree Select
	//~=========================================================================

	/**
	 * The radial gesture: drag from a selected node to pick a traversal.
	 *
	 * Only the gesture. The same four modes stay on the node's right-click menu either way, so
	 * turning this off loses the shortcut rather than the feature.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Tree Select",
		meta = (DisplayName = "Enable Radial Menu"))
	bool bEnableRadialMenu = true;

	/**
	 * Pull in the pure nodes feeding each selected node, and the pure nodes feeding those, and so on.
	 * This is what makes a selection movable - the maths hanging off a node travels with it.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Tree Select")
	bool bIncludePureInputs = true;

	/**
	 * Drop any pure node that also feeds something outside the selection, so what you end up with
	 * is self-contained.
	 *
	 * A single Get can serve half a graph. Without this, selecting one run drags that Get along and
	 * the selection is no longer a thing you can collapse or copy without consequences elsewhere.
	 *
	 * "Feeds something outside" follows data wires forward THROUGH other pure nodes and asks about
	 * the first non-pure node it reaches: a chain of maths ending at an unselected call is shared,
	 * however many pure hops are in between.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Tree Select")
	bool bExcludeSharedPureInputs = false;

	/** Hard stop, so a traversal of a pathological graph cannot hang the editor. */
	UPROPERTY(EditAnywhere, config, Category = "Tree Select",
		meta = (ClampMin = "1", UIMin = "32"))
	int32 MaxNodes = 512;

	//~=========================================================================
	//~ Tree Select | Local Branch
	//~=========================================================================

	/**
	 * Local Branch stops going forward at a node with more outgoing exec WIRES than this - a real
	 * fork. Counting wires rather than pins is the point: a Branch with only one output connected
	 * is a pass-through, and ending a run there would chop it in half for no reason.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Tree Select|Local Branch",
		meta = (ClampMin = "1", UIMin = "1", UIMax = "8"))
	int32 ExecFanOutLimit = 1;

	/** The same, backwards: stop at a node more exec wires than this feed INTO - a merge point. */
	UPROPERTY(EditAnywhere, config, Category = "Tree Select|Local Branch",
		meta = (ClampMin = "1", UIMin = "1", UIMax = "8"))
	int32 ExecMergeLimit = 1;

	/**
	 * Whether the fork or merge that ended the walk is itself selected.
	 *
	 * Off, because the common job is grabbing one arm of a switch to drag it clear - and a selected
	 * switch comes with it, which is exactly what you were avoiding. The node you started from is
	 * always selected regardless; this only governs nodes the walk ran into.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Tree Select|Local Branch")
	bool bIncludeBoundaryNodes = false;

	//~=========================================================================
	//~ Selection Cut
	//~=========================================================================

	/** Cut the graph along a cardinal line and take everything entirely past it. */
	UPROPERTY(EditAnywhere, config, Category = "Selection Cut",
		meta = (DisplayName = "Enable Selection Cut"))
	bool bEnableSelectionCut = true;

	/**
	 * Held with the gesture button to cut instead of opening the radial.
	 *
	 * None disables the cut gesture rather than making it unconditional: without a modifier the two
	 * tools are indistinguishable at the press, and one of them has to lose. The radial keeps it.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Selection Cut")
	EHVPModifierKey CutModifier = EHVPModifierKey::Shift;

	/**
	 * Held during a cut to move the far side rather than select it. Can be pressed and released
	 * freely mid-drag; whether it is down at release decides whether a selection happens.
	 *
	 * None leaves the cut as a pure selection tool.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Selection Cut")
	EHVPModifierKey MoveModifier = EHVPModifierKey::Control;

	//~=========================================================================
	//~ Gesture
	//~=========================================================================

	/**
	 * The button a gesture is dragged with, for both tools.
	 *
	 * Must be a MOUSE button - the interaction is press, drag, release, which a keyboard key cannot
	 * express. Middle by default because the graph editor pans with right out of the box; if
	 * PanningMouseButton is set to Middle, both tools stand down rather than fight it.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Gesture")
	FKey GestureStartKey = EKeys::MiddleMouseButton;

	/** Pixels of travel before a drag counts as a gesture rather than a stray click. */
	UPROPERTY(EditAnywhere, config, Category = "Gesture",
		meta = (ClampMin = "1", UIMin = "8", UIMax = "160"))
	float GestureDeadZone = 24.0f;

	/**
	 * Size of both tools' overlays, multiplied on top of the editor's DPI scale.
	 *
	 * Purely cosmetic - the dead zone is separate, so a bigger overlay does not change how far you
	 * have to pull to commit.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Gesture",
		meta = (ClampMin = "0.4", ClampMax = "3.0", UIMin = "0.5", UIMax = "2.0"))
	float UIScale = 1.0f;

	//~=========================================================================

	/**
	 * True when the given modifier is held. None is never down: an unassigned modifier reads as a
	 * disabled one, which is what both settings above want it to mean.
	 */
	static bool IsModifierDown(EHVPModifierKey Modifier, const FInputEvent& Event);
	static bool IsModifierDown(EHVPModifierKey Modifier, const FModifierKeysState& State);
};
