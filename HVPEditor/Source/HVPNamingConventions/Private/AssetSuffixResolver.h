#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

/**
 * Maps an asset's UClass to the project's type suffix.
 *
 * Two design points worth knowing:
 *
 * 1. Classes are resolved from path strings at runtime, not #included. That keeps the
 *    module's dependency list to engine essentials and lets the plugin load cleanly in a
 *    project where Niagara / PCG / Control Rig / Enhanced Input are disabled — those
 *    suffixes are simply skipped instead of failing to link.
 *
 * 2. Ambiguity is resolved by inheritance depth, not declaration order. UTextureRenderTarget2D
 *    and UTexture2D both match a UTexture rule, but RT is deeper, so _RT wins over _T. The
 *    same falls out automatically for _ABP / _WBP / _CS over _BP, and _IA over _DA. No
 *    hand-maintained ordering to get wrong.
 */
class FAssetSuffixResolver
{
public:
	/** Resolve the class-path table. Logs any entries whose owning plugin is disabled. */
	void Initialise();

	/**
	 * Suffix for this asset, or empty when the type isn't covered (a montage, a level, a
	 * plugin asset with no convention). Empty means "leave it alone", never "guess".
	 */
	FString ResolveSuffix(const UObject* Asset) const;

	/** Types the plugin must never touch: worlds, redirectors, built data, external actors. */
	static bool IsNeverRenamed(const UObject* Asset);

private:
	struct FRule
	{
		FString Suffix;
		FString ClassPath;
		TWeakObjectPtr<UClass> Class;
		int32 Depth = 0;
	};

	TArray<FRule> Rules;

	/**
	 * _BP, _AC, _BFL, _BI and _SMC all share the exact class UBlueprint, so depth can't
	 * separate them — they're told apart by BlueprintType and ParentClass instead.
	 */
	static FString ResolveBlueprintSuffix(const UObject* Asset);
};
