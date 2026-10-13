#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LetterFontAsset.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UTexture2D;

/** One glyph of an MSDF atlas. Produced by Tools/HVPText/build_fonts.py. */
USTRUCT(BlueprintType)
struct FLetterGlyph
{
	GENERATED_BODY()

	/** Atlas UVs: (left, top, right, bottom), 0..1. All zero for a glyph known only from its mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyph")
	FVector4f UVRect = FVector4f::Zero();

	/** Quad bounds around the origin on the baseline, in em, y down: (left, top, right, bottom).
	    Includes the distance-field padding, so it is larger than the ink. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyph")
	FVector4f Quad = FVector4f::Zero();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyph")
	float Advance = 0.f;
};

/**
 * Everything the text techniques need to draw one font. One asset per font, made by
 * Tools/HVPText/import_fonts.py from the font tools' output, so every font enters a project the
 * same way.
 */
UCLASS(BlueprintType)
class HVPTEXT_API ULetterFontAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font")
	FString DisplayName;

	/** Capital height in em. A letter "H cm tall" is drawn at an em size of H / CapHeight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Font")
	float CapHeight = 0.7f;

	// --- Layout (em) ----------------------------------------------------------------------

	/** Baseline to baseline, em. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float LineHeight = 1.2f;

	/** How far the font reaches above the baseline, em. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float Ascender = 0.9f;

	/** How far the font reaches below the baseline, em (positive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float Descender = 0.25f;

	/** The distance-field padding around every glyph quad, em: a quad shrunk by this on each side is
	    the glyph's ink box. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	float QuadPadding = 0.f;

	/** Pair kerning, em, added to the first glyph's advance when the second follows it. Keyed by
	    KerningKey(First, Second); pairs without an entry kern by 0. From the font's GPOS table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	TMap<int32, float> Kerning;

	// --- MSDF ----------------------------------------------------------------------------

	/** MTSDF atlas: RGB multi-channel distance, A true distance. Linear, uncompressed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MSDF")
	TObjectPtr<UTexture2D> MSDFAtlas;

	/** Instance of M_LetterMSDF bound to MSDFAtlas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MSDF")
	TObjectPtr<UMaterialInterface> MSDFMaterial;

	/** Keyed by character code. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MSDF")
	TMap<int32, FLetterGlyph> Glyphs;

	// --- Glyph meshes ----------------------------------------------------------------------

	/** Extruded glyphs with LODs from build_glyph_meshes.py, keyed by character code. Built in the
	    letter frame at 1 em = 100 units, centred on the capital's middle; empty for fonts that were
	    not extruded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TMap<int32, TObjectPtr<UStaticMesh>> GlyphMeshes;

	UStaticMesh* FindGlyphMesh(TCHAR Char) const
	{
		const TObjectPtr<UStaticMesh>* Found = GlyphMeshes.Find(static_cast<int32>(Char));
		return Found ? Found->Get() : nullptr;
	}
	const FLetterGlyph* FindGlyph(TCHAR Char) const { return Glyphs.Find(static_cast<int32>(Char)); }

	/** Both characters in one int: the first in the high 16 bits. (An FString key would not do:
	    TMap compares FStrings without case, so "To" and "TO" would be one pair.) */
	static int32 KerningKey(TCHAR First, TCHAR Second)
	{
		return static_cast<int32>((static_cast<uint32>(First) & 0xFFFF) << 16 | (static_cast<uint32>(Second) & 0xFFFF));
	}
	float GetKerning(TCHAR First, TCHAR Second) const
	{
		const float* Found = Kerning.Find(KerningKey(First, Second));
		return Found ? *Found : 0.f;
	}
	/** The pen advance, em. A character the font lacks advances like a space would, half an em. */
	float GetAdvance(TCHAR Char) const
	{
		const FLetterGlyph* Glyph = FindGlyph(Char);
		return Glyph ? Glyph->Advance : 0.5f;
	}
	/** The glyph's ink box along the line, em from its pen position. False for whitespace and
	    characters the font lacks, which have no ink (both then come back as the advance's middle). */
	bool GetInkExtent(TCHAR Char, float& OutLeft, float& OutRight) const
	{
		const FLetterGlyph* Glyph = FindGlyph(Char);
		const bool bInk = Glyph && Glyph->Quad.Z > Glyph->Quad.X;
		if (!bInk)
		{
			OutLeft = OutRight = GetAdvance(Char) * 0.5f;
			return false;
		}
		OutLeft = Glyph->Quad.X + QuadPadding;
		OutRight = Glyph->Quad.Z - QuadPadding;
		return true;
	}
	/** Where a letter's origin sits along the line, em from its pen position: every technique centres
	    a letter on its ink box (the MSDF quad's padding is even on both sides). */
	float GetInkCentre(TCHAR Char) const
	{
		float Left, Right;
		GetInkExtent(Char, Left, Right);
		return (Left + Right) * 0.5f;
	}
};
