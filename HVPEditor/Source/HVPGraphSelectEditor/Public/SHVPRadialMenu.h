#pragma once

#include "CoreMinimal.h"
#include "HVPGraphSelect.h"
#include "Widgets/SLeafWidget.h"

/**
 * The gesture's radial: four segments of a ring, each labelled, the pointed-at one highlighted.
 *
 * A LEAF widget that fills its window's overlay and is hit-test invisible. It paints at an ABSOLUTE
 * position handed to it rather than at its own geometry, which is what lets one widget spanning the
 * whole window draw a ring wherever the press happened without any layout work.
 *
 * It does not decide which segment is active. The input processor already owns the dead zone and
 * the direction maths for firing the action, and having the visual re-derive that from the cursor
 * would be two sources of truth that could disagree - the highlight showing one thing while the
 * release did another.
 */
class SHVPRadialMenu : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SHVPRadialMenu) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Screen-space point the gesture started from; the ring is drawn centred here. */
	void SetCenter(const FVector2D& InAbsoluteCenter) { AbsoluteCenter = InAbsoluteCenter; Invalidate(EInvalidateWidgetReason::Paint); }

	/** Which segment to light up, or unset while inside the dead zone. */
	void SetActiveMode(const TOptional<EHVPGraphSelectMode>& InMode) { ActiveMode = InMode; Invalidate(EInvalidateWidgetReason::Paint); }

	//~ SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	FVector2D AbsoluteCenter = FVector2D::ZeroVector;
	TOptional<EHVPGraphSelectMode> ActiveMode;
};
