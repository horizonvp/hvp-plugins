#pragma once

#include "CoreMinimal.h"
#include "TextRenderTypes.generated.h"

class ULetterFontAsset;

/** How a group's letters are drawn. Both draw each distinct glyph as one instanced component. */
UENUM(BlueprintType)
enum class ETextRenderTechnique : uint8
{
	/** Flat distance-field quads. */
	MSDF      UMETA(DisplayName = "MSDF quads"),
	/** Extruded glyph meshes. */
	GlyphMesh UMETA(DisplayName = "Glyph meshes"),
};

/** Which point of a block of text SetGroupText puts at its Location. */
UENUM(BlueprintType)
enum class ETextAnchor : uint8
{
	/** The top left corner of the ink: the first line's capital height, its first letter's left edge.
	    Lines are left-aligned. */
	TopLeft,
	/** The middle of the block, from the first line's capital height to the last line's baseline.
	    Lines are centred. A single line's letters sit exactly at Location's height. */
	Center,
};

/** One letter as the text subsystem holds it. */
USTRUCT(BlueprintType)
struct HVPTEXT_API FTextLetter
{
	GENERATED_BODY()

	/** The character drawn. Only the first character counts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Letter")
	FString Character = TEXT("A");

	/** Letter frame, relative to its group's frame (the world when the group has none): origin at the
	    letter's visual centre, +X toward the reader, +Z up. Scale is uniform; 1 draws capitals at the
	    group's cap height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Letter")
	FTransform Transform;

	/** Per instance custom data, as the letter's material reads it (PerInstanceCustomData nodes).
	    Kept exactly NumCustomDataFloats long: missing floats come from DefaultCustomData, extra ones
	    are dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Letter")
	TArray<float> CustomData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Letter")
	bool bVisible = true;

	TCHAR GetChar() const { return Character.IsEmpty() ? TEXT(' ') : Character[0]; }
};

/** How a letter group looks. Letters of every group that share a font and technique share
    instanced components, whatever their cap height. */
USTRUCT(BlueprintType)
struct HVPTEXT_API FTextGroupStyle
{
	GENERATED_BODY()

	/** Empty: the project's Default Font (Project Settings > Plugins > HVP Text). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Style")
	TObjectPtr<ULetterFontAsset> Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Style")
	ETextRenderTechnique Technique = ETextRenderTechnique::MSDF;

	/** Capital height of a letter at scale 1, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Style", meta = (ClampMin = "0.1"))
	float CapHeightCm = 8.f;
};
