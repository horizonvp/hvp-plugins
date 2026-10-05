#include "SHVPCutLine.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HVPGraphSelectSettings.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "HVPCutLine"

namespace HVPCut
{
	/** How far the gradient reaches off the line, at 1.0 scale. */
	static constexpr float BleedDepth = 190.0f;

	static constexpr float LineThickness = 2.0f;

	/** Strength of the tint where it meets the line. Gentle on purpose - this is a hint, not a wash. */
	static constexpr float BleedOpacity = 0.30f;
}

void SHVPCutLine::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);
}

int32 SHVPCutLine::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace HVPCut;

	if (!Cardinal.IsSet())
	{
		return LayerId;	// still inside the dead zone: nothing decided, so nothing drawn
	}

	const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();
	const float Scale = AllottedGeometry.Scale * Settings.UIScale;

	// The origin and graph rect are desktop positions, but AllottedGeometry is in WINDOW space:
	// converting with it is off by the window's position on screen, which only vanishes for a window
	// at the desktop origin (maximised on the main monitor). The tick-space geometry is this same
	// widget in desktop space - Slate stores it just before OnPaint - and local coordinates are the
	// same in both.
	const FGeometry& DesktopGeometry = GetTickSpaceGeometry();
	const FVector2D Origin(DesktopGeometry.AbsoluteToLocal(AbsoluteOrigin));

	// Bounded by the GRAPH, not the window. Clipping would work too, but confining the geometry
	// means nothing is ever generated outside the panel in the first place.
	const FVector2D GraphMin(DesktopGeometry.AbsoluteToLocal(
		FVector2D(AbsoluteGraphRect.Left, AbsoluteGraphRect.Top)));
	const FVector2D GraphMax(DesktopGeometry.AbsoluteToLocal(
		FVector2D(AbsoluteGraphRect.Right, AbsoluteGraphRect.Bottom)));
	const float Depth = BleedDepth * Scale;

	// The gold a selected node is ringed with, so a pending selection and a made one agree. It is
	// not readable as a colour token - Graph.Node.ShadowSelected is a BOX_BRUSH, with the gold baked
	// into the texture - so this is the literal from FCoreStyle SelectionColor_LinearRef, which is
	// where that texture got it. Move mode shifts to the accent blue: the same cut, a different
	// verb, worth telling apart before committing.
	const FLinearColor Accent = bMoveMode
		? FStyleColors::Primary.GetColor(InWidgetStyle)
		: FLinearColor(0.728f, 0.364f, 0.003f);

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));

	const bool bHorizontalCut = (Cardinal.GetValue() == EHVPCardinal::Up
		|| Cardinal.GetValue() == EHVPCardinal::Down);

	// ---- the bleed -----------------------------------------------------------
	// A gradient rect spanning the window on the cut axis and Depth deep on the other, with the
	// stops ordered so the solid end always sits against the line whichever way the cut faces.
	FVector2D BleedPos;
	FVector2D BleedSize;
	EOrientation Orientation;
	bool bReversed = false;

	switch (Cardinal.GetValue())
	{
	case EHVPCardinal::Right:
		BleedPos = FVector2D(Origin.X, GraphMin.Y);
		BleedSize = FVector2D(FMath::Min<double>(Depth, GraphMax.X - Origin.X), GraphMax.Y - GraphMin.Y);
		Orientation = Orient_Vertical;	// stops run left-to-right
		break;
	case EHVPCardinal::Left:
		BleedPos = FVector2D(FMath::Max(Origin.X - Depth, GraphMin.X), GraphMin.Y);
		BleedSize = FVector2D(Origin.X - BleedPos.X, GraphMax.Y - GraphMin.Y);
		Orientation = Orient_Vertical;
		bReversed = true;
		break;
	case EHVPCardinal::Down:
		BleedPos = FVector2D(GraphMin.X, Origin.Y);
		BleedSize = FVector2D(GraphMax.X - GraphMin.X, FMath::Min<double>(Depth, GraphMax.Y - Origin.Y));
		Orientation = Orient_Horizontal;	// stops run top-to-bottom
		break;
	default:	// Up
		BleedPos = FVector2D(GraphMin.X, FMath::Max(Origin.Y - Depth, GraphMin.Y));
		BleedSize = FVector2D(GraphMax.X - GraphMin.X, Origin.Y - BleedPos.Y);
		Orientation = Orient_Horizontal;
		bReversed = true;
		break;
	}

	if (BleedSize.X <= 0.0 || BleedSize.Y <= 0.0)
	{
		return LayerId;	// the cut sits on the panel edge; nothing to bleed into
	}

	FLinearColor Near = Accent;
	Near.A = BleedOpacity;
	FLinearColor Far = Accent;
	Far.A = 0.0f;

	TArray<FSlateGradientStop> Stops;
	Stops.Add(FSlateGradientStop(FVector2D::ZeroVector, bReversed ? Far : Near));
	Stops.Add(FSlateGradientStop(BleedSize, bReversed ? Near : Far));

	FSlateDrawElement::MakeGradient(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(BleedSize, FSlateLayoutTransform(BleedPos)),
		Stops, Orientation);

	// ---- the line itself -----------------------------------------------------
	TArray<FVector2f> Line;
	if (bHorizontalCut)
	{
		Line.Add(FVector2f(static_cast<float>(GraphMin.X), static_cast<float>(Origin.Y)));
		Line.Add(FVector2f(static_cast<float>(GraphMax.X), static_cast<float>(Origin.Y)));
	}
	else
	{
		Line.Add(FVector2f(static_cast<float>(Origin.X), static_cast<float>(GraphMin.Y)));
		Line.Add(FVector2f(static_cast<float>(Origin.X), static_cast<float>(GraphMax.Y)));
	}

	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(),
		Line, ESlateDrawEffect::None, Accent, /*bAntialias*/ true, LineThickness * Scale);

	// ---- the count -----------------------------------------------------------
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(
		"Bold", FMath::Max(7, FMath::RoundToInt(9.0f * Scale)));
	const FText Caption = FText::Format(
		bMoveMode
			? LOCTEXT("MoveCaption", "Move {0} node(s)")
			: LOCTEXT("SelectCaption", "Select {0} node(s)"),
		FText::AsNumber(AffectedCount));
	const FString CaptionText = Caption.ToString();

	const TSharedRef<FSlateFontMeasure> Measure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D TextSize(Measure->Measure(CaptionText, Font));
	const FVector2D Padding(8.0f * Scale, 4.0f * Scale);
	const FVector2D BoxSize = TextSize + Padding * 2.0f;

	// Sat just off the line on the side being taken, so it never covers the graph you are judging.
	const FVector2D Offset = FVector2D(FHVPDirectionalSelect::AxisOf(Cardinal.GetValue()))
		* (16.0 * Scale);
	FVector2D BoxTopLeft = Origin + Offset;
	if (bHorizontalCut)
	{
		BoxTopLeft.X -= BoxSize.X * 0.5;
		if (Cardinal.GetValue() == EHVPCardinal::Up) { BoxTopLeft.Y -= BoxSize.Y; }
	}
	else
	{
		BoxTopLeft.Y -= BoxSize.Y * 0.5;
		if (Cardinal.GetValue() == EHVPCardinal::Left) { BoxTopLeft.X -= BoxSize.X; }
	}

	// Kept inside the panel so a cut near an edge does not push its own caption out over the
	// surrounding editor - the thing this whole change is about.
	BoxTopLeft.X = FMath::Clamp(BoxTopLeft.X, GraphMin.X, FMath::Max(GraphMin.X, GraphMax.X - BoxSize.X));
	BoxTopLeft.Y = FMath::Clamp(BoxTopLeft.Y, GraphMin.Y, FMath::Max(GraphMin.Y, GraphMax.Y - BoxSize.Y));

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
		AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(BoxTopLeft)),
		WhiteBrush, ESlateDrawEffect::None, FStyleColors::Background.GetColor(InWidgetStyle));

	FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3,
		AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(BoxTopLeft + Padding)),
		CaptionText, Font, ESlateDrawEffect::None, Accent);

	return LayerId + 4;
}

#undef LOCTEXT_NAMESPACE
