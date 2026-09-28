#pragma once

#include "CoreMinimal.h"

struct FAssetNamingConfig;

/** One placement problem found on one asset. */
struct FPlacementViolation
{
	FString Rule;          // "content-project-folder", "content-toplevel", ...
	FString PackageName;   // /Game/...
	FString AssetName;
	FString Message;       // what's wrong
	FString Hint;          // what to do about it

	/**
	 * Weak, and deliberately so: moving an asset renames the same UObject into a new
	 * package, so this pointer survives the move and lets the warning re-check itself
	 * without having to track path changes.
	 */
	TWeakObjectPtr<UObject> Asset;

	FString ToLine() const
	{
		return Hint.IsEmpty()
			? FString::Printf(TEXT("%s — %s"), *AssetName, *Message)
			: FString::Printf(TEXT("%s — %s (%s)"), *AssetName, *Message, *Hint);
	}
};

/**
 * The deterministic subset of the folder rules in CLAUDE.md, evaluated for a single asset.
 *
 * These mirror check_conventions.py's check_content_placement / check_module_layout /
 * check_audio_placement. To keep them from drifting, evaluation happens in REPO-PATH space
 * ("Content/BiogenCD38/Modules/AMR/Thing_BP.uasset") rather than package-path space, so the
 * comparisons are the same string operations the Python performs.
 *
 * Deliberately NOT covered: "shared assets used by 2+ modules belong in Library/". That's a
 * judgement about how an asset will be used, which is unknowable when it's created.
 */
namespace AssetPlacementRules
{
	/** Violations for this asset, empty when its location is fine (or the rules are off). */
	TArray<FPlacementViolation> Check(const UObject* Asset, const FAssetNamingConfig& Config);

	/** As above, for an asset that may not be loaded. */
	TArray<FPlacementViolation> CheckPath(const FString& PackageName, const FString& AssetName,
		const FAssetNamingConfig& Config);

	/**
	 * Rules that apply to a FOLDER rather than an asset, e.g. "/Game/BiogenCD38/Modules/AMR/Textures".
	 *
	 * Needed because an empty folder contains no asset, so the asset-level checks never
	 * see it — creating a folder called Textures would otherwise go unflagged until
	 * something was put in it.
	 */
	TArray<FPlacementViolation> CheckFolder(const FString& PackagePath, const FAssetNamingConfig& Config);
}
