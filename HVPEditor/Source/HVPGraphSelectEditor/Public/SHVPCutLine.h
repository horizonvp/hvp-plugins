#pragma once

#include "CoreMinimal.h"
#include "HVPDirectionalSelect.h"
#include "Widgets/SLeafWidget.h"

/**
 * The organisation tool's overlay: a cut line with selection-yellow bleeding off the taken side.
 *
 * The line is fixed at the press point and does NOT follow the cursor. That is deliberate and it is
 * the whole reason the tool is predictable: the set of nodes is decided by where the cut was made,
 * so dragging further can change how far things move but never which things move.
 */
class SHVPCutLine : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SHVPCutLine) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Screen-space point the cut passes through. */
	void SetOrigin(const FVector2D& InAbsoluteOrigin) { AbsoluteOrigin = InAbsoluteOrigin; }

	/**
	 * The graph panel's own screen rect. Everything is drawn inside this rather than across the
	 * whole window, so the cut stays in the graph instead of lying over the Details panel and the
	 * toolbar - it describes a graph operation and should not look like a global one.
	 */
	void SetGraphRect(const FSlateRect& InAbsoluteRect) { AbsoluteGraphRect = InAbsoluteRect; }

	/** Which way the drag locked, or unset while still inside the dead zone. */
	void SetCardinal(const TOptional<EHVPCardinal>& InCardinal) { Cardinal = InCardinal; }

	/** True while Ctrl is held, which shifts the wording and colour from "select" to "move". */
	void SetMoveMode(bool bInMoveMode) { bMoveMode = bInMoveMode; }

	/** How many nodes the cut currently takes, shown so the count is known before committing. */
	void SetAffectedCount(int32 InCount) { AffectedCount = InCount; }

	//~ SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	FVector2D AbsoluteOrigin = FVector2D::ZeroVector;
	FSlateRect AbsoluteGraphRect;
	TOptional<EHVPCardinal> Cardinal;
	bool bMoveMode = false;
	int32 AffectedCount = 0;
};
