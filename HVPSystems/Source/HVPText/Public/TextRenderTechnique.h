#pragma once

#include "CoreMinimal.h"
#include "TextRenderTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectKey.h"
#include "TextRenderTechnique.generated.h"

class UInstancedStaticMeshComponent;
class ULetterFontAsset;
class UMaterialInterface;
class UStaticMesh;

/**
 * What a technique is told about the letters it may draw, by letter index. The text subsystem
 * answers it for one pool: the letters' fonts, and their frames in the pool's root space.
 */
class ITextLetterSource
{
public:
	virtual ~ITextLetterSource() = default;
	virtual int32 Num() const = 0;
	virtual TCHAR GetChar(int32 Letter) const = 0;
	/** Null when the letter's group has no font. */
	virtual const ULetterFontAsset* GetFont(int32 Letter) const = 0;
	/** The letter's frame in the pool root's space with its em scale in: a glyph laid out at 1 em =
	    1 cm, transformed by this, lands where the letter is. Zero scale when hidden. */
	virtual FTransform GetDrawFrame(int32 Letter) const = 0;
	virtual const TArray<float>& GetCustomData(int32 Letter) const = 0;
};

/**
 * One way of drawing letters, for one pool (a root component the instances hang under). Every
 * distinct glyph of every font is one instanced component and every letter an instance of it, so
 * moving any number of letters is one batched update per glyph, and the per instance custom data
 * is entirely the letters' (UTextRenderSubsystem::GetNumCustomDataFloats).
 *
 * A technique draws the letters it is told are its own (Mine). Build lays everything out from
 * scratch; UpdateGlyphs moves single letters to another glyph (or to or from this technique) without
 * touching the rest: the letter gives its instance back to the old glyph's component, hidden and
 * free for the next letter, and takes one in the new glyph's.
 */
UCLASS(Abstract)
class HVPTEXT_API UTextRenderTechnique : public UObject
{
	GENERATED_BODY()

public:
	void Build(USceneComponent* InRoot, int32 InNumCustomDataFloats, const ITextLetterSource& Letters, TConstArrayView<bool> Mine);
	/** Re-homes the given letters. Letters may have grown since the last build. */
	void UpdateGlyphs(const ITextLetterSource& Letters, TConstArrayView<bool> Mine, TConstArrayView<int32> Changed);
	/** Pushes the letters' frames: every component, or with Dirty only those holding a dirty letter
	    (and any that just gave an instance back). */
	void ApplyTransforms(const ITextLetterSource& Letters, const TBitArray<>* Dirty = nullptr);
	void ApplyCustomData(const ITextLetterSource& Letters, const TBitArray<>* Dirty = nullptr);
	void Clear();
	bool IsBuilt() const { return Root.IsValid(); }
	int32 GetComponentCount() const { return Components.Num(); }
	/** Why some of this technique's letters are not drawn; empty when all are. */
	FString GetNote() const;

protected:
	/** How one glyph is drawn. */
	struct FGlyphDraw
	{
		UStaticMesh* Mesh = nullptr;
		UMaterialInterface* Material = nullptr;
		/** The instance relative to its letter frame, at 1 em = 1 cm. */
		FTransform InLetter;
		/** Custom primitive data 0-3 of the glyph's component, when bHasPrimitiveData. */
		FVector4f PrimitiveData = FVector4f::Zero();
		bool bHasPrimitiveData = false;
	};

	/** Why this font cannot be drawn at all; empty when it can. */
	virtual FString CheckFont(const ULetterFontAsset& Font) const PURE_VIRTUAL(UTextRenderTechnique::CheckFont, return FString(););
	/** False when the font has nothing to draw this character with. */
	virtual bool DescribeGlyph(const ULetterFontAsset& Font, TCHAR Char, FGlyphDraw& Out) const PURE_VIRTUAL(UTextRenderTechnique::DescribeGlyph, return false;);
	virtual void ConfigureComponent(UInstancedStaticMeshComponent* Component) const {}

private:
	/** The component that draws Char in Font, made on first use; INDEX_NONE when the font cannot. */
	int32 GetGlyphComponent(const ULetterFontAsset* Font, TCHAR Char);
	/** Gives a letter an instance in the right component (or none), releasing its old one. */
	void AssignLetter(int32 Letter, const ULetterFontAsset* Font, TCHAR Char, bool bMine);
	void SizeLetterArrays(int32 Num);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;

	TWeakObjectPtr<USceneComponent> Root;
	int32 NumCustomDataFloats = 0;

	/** Per font: why it cannot be drawn this way, empty when it can. */
	TMap<TObjectKey<ULetterFontAsset>, FString> FontNotes;
	/** Per font and character: its component, or INDEX_NONE once found undrawable. */
	TMap<TPair<TObjectKey<ULetterFontAsset>, TCHAR>, int32> GlyphComponent;
	/** Per component: its glyph's instance-in-letter transform, which letter owns each instance
	    (INDEX_NONE = free, drawn at zero scale) and the free instances. */
	TArray<FTransform> ComponentInLetter;
	TArray<TArray<int32>> InstanceOwner;
	TArray<TArray<int32>> FreeInstances;
	/** Components that gave an instance back since the last transform update: it must be hidden. */
	TSet<int32> ReleasedIn;

	/** Per letter: its component and instance (INDEX_NONE when not drawn by this technique), and
	    whether it is this technique's but has no glyph to draw (and why, when its font is the cause). */
	TArray<int32> LetterComponent;
	TArray<int32> LetterInstance;
	TArray<bool> LetterMissing;
	TArray<TObjectKey<ULetterFontAsset>> LetterFont;

	TArray<TArray<FTransform>> Scratch;
};

/**
 * Distance-field quads. Each glyph's component carries its atlas rect (left, top, right, bottom)
 * in custom primitive data 0-3, which M_LetterMSDF reads; the material is the font's own instance.
 */
UCLASS()
class HVPTEXT_API UTextRenderTechnique_MSDF : public UTextRenderTechnique
{
	GENERATED_BODY()

public:
	/** A 100 cm plane, normal +Z. Settings' Quad Mesh. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> QuadMesh;

	int32 TranslucentSortPriority = 10;

protected:
	virtual FString CheckFont(const ULetterFontAsset& Font) const override;
	virtual bool DescribeGlyph(const ULetterFontAsset& Font, TCHAR Char, FGlyphDraw& Out) const override;
	virtual void ConfigureComponent(UInstancedStaticMeshComponent* Component) const override;
};

/** Extruded glyph meshes with LODs, baked by the font tools. */
UCLASS()
class HVPTEXT_API UTextRenderTechnique_GlyphMesh : public UTextRenderTechnique
{
	GENERATED_BODY()

public:
	/** Settings' Glyph Mesh Material. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Material;

protected:
	virtual FString CheckFont(const ULetterFontAsset& Font) const override;
	virtual bool DescribeGlyph(const ULetterFontAsset& Font, TCHAR Char, FGlyphDraw& Out) const override;
};
