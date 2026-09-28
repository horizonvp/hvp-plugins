// HVPPalette

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "HVPPaletteSettings.generated.h"

/**
 * Which outputs "Generate Palette Accessors" produces. Project Settings -> Plugins -> HVP
 * Palette.
 *
 * Config lives in DefaultEditor.ini rather than a per-user file: which assets a palette generates
 * is a property of the PROJECT, not of whoever happens to right-click it. Two people regenerating
 * the same collection have to get the same set of assets out, or one of them silently stops
 * refreshing something the other is still using.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "HVP Palette"))
class UHVPPaletteSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHVPPaletteSettings();

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/**
	 * The material function - one named output pin per parameter, each reading the collection live.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Generated Assets",
		meta = (DisplayName = "Material Function (_MF)"))
	bool bGenerateMaterialFunction;

	/**
	 * The Blueprint function library - the same parameters as pins on a pure node, usable from a
	 * construction script.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Generated Assets",
		meta = (DisplayName = "Blueprint Function Library (_BFL)"))
	bool bGenerateBlueprintLibrary;

	/**
	 * The 8x8 swatch texture, addressing the same entries by a single 0-63 index.
	 *
	 * Off by default, and deliberately: an index is a bare number with no name attached, so
	 * inserting a colour in the middle of the collection shifts every index after it and silently
	 * changes what each material samples. The pins above have no such failure mode. Turn this on
	 * only where the index itself is computed rather than picked.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Generated Assets",
		meta = (DisplayName = "Palette Swatch Texture (_T)"))
	bool bGeneratePaletteTexture;
};
