#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AssetNamingSettings.generated.h"

/**
 * How loudly to complain when a new asset lands in the wrong folder.
 *
 * There is deliberately no modal-dialog option. A modal dialog freezes the editor, so you
 * could not move the offending asset while it was up — which is the one action that should
 * make the warning go away.
 */
UENUM()
enum class EPlacementWarningStyle : uint8
{
	/** Don't check placement at all. */
	Off          UMETA(DisplayName = "Off"),
	/** Write to the Output Log only. */
	LogOnly      UMETA(DisplayName = "Log only"),
	/** Toast in the corner that fades on its own after a few seconds. */
	Transient    UMETA(DisplayName = "Toast (auto-dismiss)"),
	/**
	 * Sticky popup that will not go away until the asset is somewhere legal. It re-checks
	 * itself whenever an asset is moved, renamed or deleted, and clears the moment the
	 * rules pass.
	 */
	Persistent   UMETA(DisplayName = "Sticky — clears only when fixed"),
};

/**
 * Project Settings -> Editor -> HVP Naming Conventions.
 *
 * Saved to Config/DefaultEditor.ini (defaultconfig), so the settings are shared by the
 * team rather than being per-user.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "HVP Naming Conventions"))
class UAssetNamingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Editor"); }
	virtual FName GetSectionName() const override { return TEXT("HVPNamingConventions"); }

	static const UAssetNamingSettings& Get() { return *GetDefault<UAssetNamingSettings>(); }

	/** Master switch. Turning this off leaves the git hooks as the only enforcement. */
	UPROPERTY(EditAnywhere, config, Category = "General")
	bool bEnabled = true;

	/** Log every rename that would happen, without touching anything. */
	UPROPERTY(EditAnywhere, config, Category = "General")
	bool bDryRun = false;

	/** Suffix assets that arrive from a file import (png -> _T, fbx -> _SM, wav -> _SW). */
	UPROPERTY(EditAnywhere, config, Category = "Scope")
	bool bApplyToImportedAssets = true;

	/** Suffix assets created inside the editor (Add menu, duplicate, Blueprint child, ...). */
	UPROPERTY(EditAnywhere, config, Category = "Scope")
	bool bApplyToCreatedAssets = true;

	/**
	 * Repair the name Ctrl+W leaves behind: Foo_T -> "Foo_T1" puts the suffix mid-name,
	 * which the checker flags. With this on it becomes Foo1_T instead.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Scope")
	bool bRepairDuplicateSuffix = true;

	/**
	 * Fix the case of a suffix that is already there: Foo_bp / Foo_Bp -> Foo_BP, rather
	 * than treating it as unsuffixed and producing Foo_bp_BP.
	 *
	 * Only the CASE is corrected — the type is never changed. A material named Foo_sm
	 * becomes Foo_SM, not Foo_M, for the same reason an exact Foo_SM is left alone: the
	 * plugin doesn't overrule a type the author chose deliberately.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Scope")
	bool bNormaliseSuffixCase = true;

	/**
	 * Also satisfy the checker's asset-case rule by capitalising the first letter of each
	 * underscore-separated segment (my_hand_mesh -> My_Hand_Mesh_SM). Underscores are
	 * preserved, so intentional tags like Hand_L_CD38 survive. Off by default because it
	 * rewrites names the user typed deliberately.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Scope")
	bool bFixPascalCase = false;

	/**
	 * Seconds of quiet before a batch is renamed. A single FBX can produce a mesh, a
	 * skeleton, a physics asset, materials and textures over several frames; waiting for
	 * the import to go quiet means one batched rename instead of a race against the
	 * importer.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Advanced", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float DebounceSeconds = 0.4f;

	/**
	 * Warn when a newly created asset lands somewhere the folder rules disallow — e.g. a
	 * Blueprint dropped straight into Content/ instead of Content/<Project>/Modules/...
	 *
	 * Nothing is ever moved automatically: a move rewrites references, and "should this
	 * live in Library/ or in a module?" depends on how the asset will be used, which isn't
	 * knowable at creation time. The warning tells you; you decide.
	 *
	 * One warning for the whole batch, not one per asset, so a 50-file import can't bury
	 * the editor. Suppressed automatically when the editor runs unattended.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Placement")
	EPlacementWarningStyle PlacementWarning = EPlacementWarningStyle::Persistent;

	/**
	 * The <ProjectName> in Content/<ProjectName>/Modules/ — the folder all game content
	 * must live under.
	 *
	 * Leave empty (the default) to use the .uproject file name, which is also what
	 * check_conventions.py auto-detects. Set it only when the content folder is
	 * deliberately named something other than the .uproject.
	 *
	 * Takes precedence over "projectName" in conventions.json. If the two end up
	 * disagreeing, the plugin logs a warning at startup — a mismatch means the editor and
	 * the git hook are enforcing different layouts.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Project Layout")
	FString ProjectNameOverride;

	/** conventions.json to inherit exemptPaths / suffixes / severity from, relative to the project root. */
	UPROPERTY(EditAnywhere, config, Category = "Project Layout")
	FString ConventionsConfigPath = TEXT("Tools/Conventions/conventions.json");
};
