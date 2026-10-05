#include "SHVPRadialMenu.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HVPGraphSelectSettings.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "HVPRadialMenu"

namespace HVPRadial
{
	/** Ring geometry, in Slate units at 1.0 DPI scale and 1.0 user scale. */
	static constexpr float InnerRadius = 40.0f;
	static constexpr float OuterRadius = 54.0f;
	static constexpr float OutlineWidth = 1.5f;
	static constexpr float LabelGap = 14.0f;

	/**
	 * Gap between segments, as a WIDTH rather than an angle.
	 *
	 * A constant angular padding produces a wedge: the same few degrees is a longer arc at the
	 * outer edge than the inner one, so the gaps splay outward. Converting a width to an angle
	 * separately at each radius - Angle = atan(Gap/2 / Radius) - keeps the slot parallel-sided.
	 */
	static constexpr float GapWidth = 7.0f;

	static constexpr int32 ArcSteps = 20;

	/**
	 * Segment centre angles, in SCREEN degrees: 0 is +X (right) and angles increase CLOCKWISE
	 * because Slate's Y grows downward. So 270 is up, which is why Local Branch sits there.
	 */
	struct FSegment
	{
		EHVPGraphSelectMode Mode;
		float CentreDegrees;
		const TCHAR* Label;
	};

	static const FSegment Segments[] =
	{
		{ EHVPGraphSelectMode::Downstream,     0.0f,   TEXT("Downstream")     },
		{ EHVPGraphSelectMode::NodeAndContext, 90.0f,  TEXT("Node + Context") },
		{ EHVPGraphSelectMode::Upstream,       180.0f, TEXT("Upstream")       },
		{ EHVPGraphSelectMode::LocalBranch,    270.0f, TEXT("Local Branch")   },
	};

	static FVector2f PointOnCircle(const FVector2D& Centre, float Radius, float Degrees)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return FVector2f(
			static_cast<float>(Centre.X) + Radius * FMath::Cos(Radians),
			static_cast<float>(Centre.Y) + Radius * FMath::Sin(Radians));
	}

	/**
	 * One filled annulus sector, triangulated as a strip zipped between its two arcs.
	 *
	 * The OUTLINE is drawn by calling this a second time, slightly larger, in the outline colour and
	 * on a lower layer - not by stroking the boundary. A thick polyline has to join its own corners,
	 * and at the near-right-angle where an arc meets a radial edge that join steps rather than
	 * meeting at a point. Two filled shapes have no joins at all, so the corners are exact.
	 */
	static void EmitSector(FSlateWindowElementList& OutDrawElements, int32 Layer,
		const FSlateResourceHandle& Handle, const FSlateRenderTransform& Transform,
		const FVector2D& Centre, float Inner, float Outer, float Gap, float CentreDegrees,
		const FLinearColor& Colour)
	{
		const float OuterPad = FMath::RadiansToDegrees(FMath::Atan2(Gap * 0.5f, Outer));
		const float InnerPad = FMath::RadiansToDegrees(FMath::Atan2(Gap * 0.5f, Inner));

		// Slate packs element tints as ToFColor(bSRGBVertexColor); MakeBox therefore lands in a
		// different colour space than a hand-packed ToFColor(false) would, which is what made the
		// ring read darker than its label box despite both being handed the same FLinearColor.
		const FColor Packed = Colour.ToFColor(true);

		TArray<FSlateVertex> Verts;
		TArray<SlateIndex> Indices;
		Verts.Reserve((ArcSteps + 1) * 2);
		Indices.Reserve(ArcSteps * 6);

		for (int32 Step = 0; Step <= ArcSteps; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / ArcSteps;
			const FVector2f OuterPoint = PointOnCircle(Centre, Outer,
				FMath::Lerp(CentreDegrees - 45.0f + OuterPad, CentreDegrees + 45.0f - OuterPad, Alpha));
			const FVector2f InnerPoint = PointOnCircle(Centre, Inner,
				FMath::Lerp(CentreDegrees - 45.0f + InnerPad, CentreDegrees + 45.0f - InnerPad, Alpha));

			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				Transform, OuterPoint, FVector2f(0.5f, 0.5f), Packed));
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				Transform, InnerPoint, FVector2f(0.5f, 0.5f), Packed));
		}

		for (int32 Step = 0; Step < ArcSteps; ++Step)
		{
			const SlateIndex Base = static_cast<SlateIndex>(Step * 2);
			Indices.Append({ Base, SlateIndex(Base + 1), SlateIndex(Base + 2) });
			Indices.Append({ SlateIndex(Base + 1), SlateIndex(Base + 3), SlateIndex(Base + 2) });
		}

		FSlateDrawElement::MakeCustomVerts(
			OutDrawElements, Layer, Handle, Verts, Indices, nullptr, 0, 0);
	}
}

void SHVPRadialMenu::Construct(const FArguments& InArgs)
{
	// Never eat a click. The input processor owns the gesture; this only draws it.
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);
}

int32 SHVPRadialMenu::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace HVPRadial;

	const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();

	// The centre is a desktop position, but AllottedGeometry is in WINDOW space: converting with it is
	// off by the window's position on screen, which only vanishes for a window sitting at the desktop
	// origin (maximised on the main monitor). The tick-space geometry is this same widget in desktop
	// space - Slate stores it just before OnPaint - and local coordinates are the same in both.
	const FVector2D Centre(GetTickSpaceGeometry().AbsoluteToLocal(AbsoluteCenter));
	// DPI scale and the user's preference are one multiplier: everything below is in final pixels.
	const float Scale = AllottedGeometry.Scale * Settings.UIScale;

	const float Inner = InnerRadius * Scale;
	const float Outer = OuterRadius * Scale;
	const float Border = OutlineWidth * Scale;
	const float Gap = GapWidth * Scale;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(
		"Regular", FMath::Max(6, FMath::RoundToInt(8.0f * Scale)));
	const TSharedRef<FSlateFontMeasure> Measure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateResourceHandle Handle =
		FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);

	// Resolved through the widget style rather than GetSpecifiedColor, so a user's editor theme is
	// honoured instead of whatever the table happened to hold at static-init time.
	const FLinearColor Grey = FStyleColors::Panel.GetColor(InWidgetStyle);
	const FLinearColor Outline = FStyleColors::Background.GetColor(InWidgetStyle);
	const FLinearColor Highlight = FStyleColors::Primary.GetColor(InWidgetStyle);
	const FLinearColor TextIdle = FStyleColors::Foreground.GetColor(InWidgetStyle);

	const int32 OutlineLayer = LayerId;
	const int32 FillLayer = LayerId + 1;
	const int32 LabelLayer = LayerId + 2;
	const int32 TextLayer = LayerId + 4;

	const FPaintGeometry Geometry = AllottedGeometry.ToPaintGeometry();
	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();

	for (const FSegment& Segment : Segments)
	{
		const bool bActive = ActiveMode.IsSet() && ActiveMode.GetValue() == Segment.Mode;
		const FLinearColor Fill = bActive ? Highlight : Grey;

		// Outline first, grown by Border on every side. Its gap is the one the eye reads, so the
		// FILL's gap is widened by the same amount to leave a uniform border rather than a fat one
		// on the flat edges and none between segments.
		EmitSector(OutDrawElements, OutlineLayer, Handle, Transform, Centre,
			Inner - Border, Outer + Border, Gap, Segment.CentreDegrees, Outline);

		EmitSector(OutDrawElements, FillLayer, Handle, Transform, Centre,
			Inner, Outer, Gap + Border * 2.0f, Segment.CentreDegrees, Fill);

		// ---- the floating label ----------------------------------------------
		const FString Label(Segment.Label);
		const FVector2D TextSize(Measure->Measure(Label, Font));
		const FVector2D BoxPadding(7.0f * Scale, 3.0f * Scale);
		const FVector2D BoxSize = TextSize + BoxPadding * 2.0f;

		// Pushed out along the segment's own axis by half the box, so the box clears the ring by
		// LabelGap on the side facing it rather than overlapping on the diagonals.
		const float Radians = FMath::DegreesToRadians(Segment.CentreDegrees);
		const FVector2f Anchor = PointOnCircle(Centre, Outer + Border + LabelGap * Scale,
			Segment.CentreDegrees);
		const FVector2D BoxTopLeft(
			Anchor.X - BoxSize.X * 0.5f + FMath::Cos(Radians) * BoxSize.X * 0.5f,
			Anchor.Y - BoxSize.Y * 0.5f + FMath::Sin(Radians) * BoxSize.Y * 0.5f);

		FSlateDrawElement::MakeBox(OutDrawElements, LabelLayer,
			AllottedGeometry.ToPaintGeometry(BoxSize + FVector2D(Border * 2.0f),
				FSlateLayoutTransform(BoxTopLeft - FVector2D(Border))),
			WhiteBrush, ESlateDrawEffect::None, Outline);

		FSlateDrawElement::MakeBox(OutDrawElements, LabelLayer + 1,
			AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(BoxTopLeft)),
			WhiteBrush, ESlateDrawEffect::None, Fill);

		FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(BoxTopLeft + BoxPadding)),
			Label, Font, ESlateDrawEffect::None, bActive ? FLinearColor::White : TextIdle);
	}

	return TextLayer;
}

#undef LOCTEXT_NAMESPACE
