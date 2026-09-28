#pragma once

#include "CoreMinimal.h"

/**
 * The subset of Tools/Conventions/conventions.json that this plugin needs, resolved the
 * same way check_conventions.py resolves it: a built-in default table, with the JSON
 * file merged on top.
 *
 * That mirrored-defaults arrangement is deliberate. conventions.json ships with
 * "suffixes": {} because the Python checker treats that key as an *overlay* on its own
 * built-in table rather than a replacement. Reading the JSON alone would therefore give
 * us an empty table. So the frozen baseline lives in both places, and every future
 * addition goes in conventions.json's overlay — where both tools pick it up and can't
 * drift apart.
 */
struct FAssetNamingConfig
{
	/** Suffix -> human description, e.g. "_T" -> "Texture". Mirrors check_conventions.py DEFAULTS. */
	TMap<FString, FString> Suffixes;

	/** Tags that must never be the final segment: _REF, _WIP, _TEMP, _OLD. */
	TArray<FString> MidNameTags;

	/** exemptPaths from the JSON, translated into /Game-relative wildcards. */
	TArray<FString> ExemptPackageWildcards;

	/** Raw severity map; a rule set to "off" is skipped, matching the project's own config. */
	TMap<FString, FString> Severities;

	// --- placement rules ---------------------------------------------------
	// Repo-relative, because that's the space check_conventions.py works in. Package
	// paths are converted to it before evaluation so the two can't drift.

	FString ProjectName;
	FString ContentDir = TEXT("Content");
	FString ContentRoot;                       // "Content/BiogenCD38"

	/**
	 * What the project name would have been without the plugin's override — i.e. what
	 * check_conventions.py sees. Kept so a divergence between the editor and the hook can
	 * be reported rather than silently enforced.
	 */
	FString ProjectNameWithoutOverride;
	bool bRequireProjectContentFolder = true;
	TArray<FString> ContentTopLevelFolders;    // App, Library, Systems, Modules
	FString ModulesDir = TEXT("Modules");

	/**
	 * Assets must live inside a module folder, not directly in Modules/. Depth below that is
	 * up to the module: Modules/MainMenu/Thing_BP is as valid as Modules/MainMenu/UI/Thing_BP,
	 * so "requireSubfolders" from the JSON is intentionally not honoured.
	 */
	bool bAllowLooseFilesInModulesDir = false;

	/**
	 * Library/ must organise its content into subfolders — a loose asset at Library/ root
	 * is flagged. Plugin-only: check_conventions.py has no equivalent rule.
	 */
	bool bRequireLibrarySubfolders = true;
	FString LibraryDir = TEXT("Library");

	/**
	 * Folder names that group by asset TYPE rather than by what the asset is for.
	 * Matched whole and case-insensitively, so Library/MasterMaterials/ is unaffected.
	 * Plugin-only, like the Library rule.
	 */
	TArray<FString> DiscouragedFolderNames;   // "Textures", "Materials"

	bool bAudioCheckEnabled = true;
	FString AudioDir = TEXT("Audio");
	FString VoDir = TEXT("Audio/VO");
	TArray<FString> VoPrefixes;                // "VO_"
	TArray<FString> AudioSuffixes;             // "_SW", "_SC"

	/** Where the config was actually read from (empty when the built-in defaults were used). */
	FString LoadedFrom;

	/**
	 * Load from <ProjectDir>/RelativeJsonPath, falling back to built-in defaults.
	 *
	 * Project name precedence, highest first:
	 *   1. ProjectNameOverride  — the plugin's Project Settings entry
	 *   2. "projectName"        — conventions.json
	 *   3. FApp::GetProjectName() — the .uproject file name (the default)
	 */
	static FAssetNamingConfig Load(const FString& RelativeJsonPath,
		const FString& ProjectNameOverride = FString());

	/**
	 * Longest-match suffix lookup, matching split_suffix() in check_conventions.py.
	 * Longest match is load-bearing: "Plasma_ATT" must resolve to _ATT, not _T, and
	 * "Foo_MS" to _MS, not _S.
	 *
	 * Always returns the CANONICAL suffix from the table, so a case-insensitive lookup on
	 * "Foo_bp" returns "_BP" and the caller can normalise with it. The checker itself is
	 * case-sensitive, which is exactly why "Foo_bp" needs fixing rather than accepting.
	 */
	FString FindTrailingSuffix(const FString& Stem, bool bCaseSensitive = true) const;

	/** True when the stem ends with a mid-name tag (Foo_REF), which the checker treats as an error. */
	bool EndsWithMidNameTag(const FString& Stem) const;

	/** True when this /Game/... package path matches an exemptPaths entry. */
	bool IsExemptPackage(const FString& PackagePath) const;

	/** severity[Rule] != "off". Unknown rules default to enabled. */
	bool IsRuleEnabled(const FString& Rule) const;

	/**
	 * "/Game/BiogenCD38/Modules/AMR/Thing" -> "Content/BiogenCD38/Modules/AMR/Thing.uasset".
	 * Placement rules are evaluated in repo-path space so they can mirror
	 * check_conventions.py line for line instead of being re-derived.
	 */
	FString PackageToRepoPath(const FString& PackageName) const;

private:
	void ApplyBuiltInDefaults();
	void MergeJson(const FString& JsonText, const FString& SourcePath);
};

/** Strip // and block comments from JSONC so FJsonSerializer can parse conventions.json. */
FString StripJsonComments(const FString& In);
