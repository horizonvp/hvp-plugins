#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TextRenderTypes.h"
#include "TextRenderSettings.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * Project Settings > Plugins > HVP Text. What a group starts with, and what every letter carries.
 * The plugin ships no fonts and no materials: Tools/HVPText/import_fonts.py makes both in the
 * project, and these point at them.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "HVP Text"))
class HVPTEXT_API UTextRenderSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTextRenderSettings();

	/** A group's font until its style names one. */
	UPROPERTY(config, EditAnywhere, Category = "Defaults", meta = (AllowedClasses = "/Script/HVPText.LetterFontAsset"))
	FSoftObjectPath DefaultFont;

	/** A new group's technique. */
	UPROPERTY(config, EditAnywhere, Category = "Defaults")
	ETextRenderTechnique DefaultTechnique = ETextRenderTechnique::MSDF;

	/** A new group's capital height, cm. */
	UPROPERTY(config, EditAnywhere, Category = "Defaults", meta = (ClampMin = "0.1"))
	float DefaultCapHeightCm = 8.f;

	/** Per instance custom data floats every letter carries: at least what the letter materials
	    read. The stock materials read a colour from floats 0-2. */
	UPROPERTY(config, EditAnywhere, Category = "Custom Data", meta = (ClampMin = "0", ClampMax = "64"))
	int32 NumCustomDataFloats = 3;

	/** What a letter's custom data floats start as, where its own CustomData does not say. White for
	    the stock materials. */
	UPROPERTY(config, EditAnywhere, Category = "Custom Data")
	TArray<float> DefaultCustomData = { 1.f, 1.f, 1.f };

	/** The material glyph meshes are drawn with: M_LetterMesh, from import_fonts.py. MSDF quads use
	    their font's own material instance. */
	UPROPERTY(config, EditAnywhere, Category = "Techniques", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath GlyphMeshMaterial;

	/** A 100 cm plane, normal +Z, that MSDF quads are drawn on. */
	UPROPERTY(config, EditAnywhere, Category = "Techniques", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
	FSoftObjectPath QuadMesh;

	/** MSDF letters draw after the scene's other translucency, wherever their bounds sit: a shared
	    component's bounds span every letter of its glyph, so sorting by distance misplaces them. */
	UPROPERTY(config, EditAnywhere, Category = "Techniques")
	int32 TranslucentSortPriority = 10;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
