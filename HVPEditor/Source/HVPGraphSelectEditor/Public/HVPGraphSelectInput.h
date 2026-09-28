#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "HVPDirectionalSelect.h"
#include "HVPGraphSelect.h"
#include "UObject/WeakObjectPtr.h"

class FScopedTransaction;
class SGraphPanel;
class SHVPCutLine;
class SHVPRadialMenu;
class SWindow;
class UEdGraphNode;

/**
 * Blender-style directional gesture: middle-drag from a selected node to pick a selection mode.
 *
 * Up = Local Branch, Left = Upstream, Right = Downstream, Down = Node + Context.
 *
 * WHY AN INPUT PREPROCESSOR. The graph editor is an engine Slate widget with no hook for adding a
 * gesture, and SGraphEditor::AllInstances is private with an explicit "no-one else should be able
 * to". FSlateApplication's preprocessor chain is the supported way in: it sees pointer events
 * before any widget and can consume them.
 *
 * WHY MIDDLE MOUSE IS SAFE. UGraphEditorSettings::PanningMouseButton defaults to Right, so the
 * middle button is unclaimed in a graph out of the box. It is NOT unclaimed if that setting has
 * been changed, so this stands down entirely when the configured gesture button is the one panning
 * is bound to rather than quietly stealing the pan.
 */
class FHVPGraphSelectInput : public IInputProcessor
{
public:
	/** Registers the processor with Slate. Safe to call once from StartupModule. */
	static void Register();

	/** Removes it again. Call from ShutdownModule, or the processor outlives the module's code. */
	static void Unregister();

	//~ IInputProcessor
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("HVPGraphSelect"); }

private:
	/** Set between a qualifying middle-press and its release. */
	bool bTracking = false;

	/** Screen position of the press, which the release is measured against. */
	FVector2D PressPosition = FVector2D::ZeroVector;

	/** Captured at press time: the graph is not re-read on release, so a stale selection cannot fire. */
	TWeakObjectPtr<UEdGraphNode> PressedNode;

	/** The radial, living in the press window's overlay for the duration of the drag. */
	TSharedPtr<SHVPRadialMenu> Radial;

	/** Whose overlay it went into, so the same window can be asked to take it back out. */
	TWeakPtr<SWindow> RadialWindow;

	/** Which mode the current cursor position resolves to, or unset inside the dead zone. */
	TOptional<EHVPGraphSelectMode> ResolveMode(const FVector2D& CurrentPosition) const;

	/** Tears down the overlay. Safe to call when nothing is showing. */
	void DismissRadial();

	//~ The directional tool - Shift on empty space, plus Ctrl to move rather than select.

	/** Set between a qualifying Shift+middle press and its release. */
	bool bCutting = false;

	/**
	 * Whether Ctrl is moving the far side RIGHT NOW. Not decided at press: Ctrl can be pressed and
	 * released freely mid-drag, each press starting a fresh run of movement from wherever the
	 * cursor was at that moment.
	 */
	bool bCutMoveActive = false;

	/** Movement banked from previous Ctrl runs, so releasing Ctrl freezes rather than snaps back. */
	FVector2D CutBankedOffset = FVector2D::ZeroVector;

	/** Screen position Ctrl was last pressed at; the current run is measured from here. */
	FVector2D CutMoveAnchor = FVector2D::ZeroVector;

	/** Locked on leaving the dead zone and never revisited, so a wobble cannot flip the cut. */
	TOptional<EHVPCardinal> CutCardinal;

	/** The panel the cut was started on; needed for zoom and for node extents. */
	TWeakPtr<SGraphPanel> CutPanel;

	/** Where the cut line sits, in GRAPH coordinates, so panning mid-drag cannot move it. */
	FVector2D CutGraphOrigin = FVector2D::ZeroVector;

	/** Decided once, when the direction locks. Dragging further moves them; it never re-picks them. */
	TArray<TWeakObjectPtr<UEdGraphNode>> CutNodes;

	/** Positions as they were at lock time, so live dragging sets rather than accumulates. */
	TArray<FIntPoint> CutOriginalPositions;

	/** One transaction spanning the whole drag, so undo returns everything in a single step. */
	TUniquePtr<FScopedTransaction> CutTransaction;

	TSharedPtr<SHVPCutLine> CutOverlay;
	TWeakPtr<SWindow> CutWindow;

	bool BeginCut(const FPointerEvent& MouseEvent);
	void UpdateCut(const FVector2D& CurrentPosition, bool bCtrlDown);
	void FinishCut(bool bCommit, bool bCtrlHeld);
};
