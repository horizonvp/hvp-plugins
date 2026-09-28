#pragma once

#include "AssetNamingConfig.h"
#include "AssetSuffixResolver.h"
#include "PlacementWarning.h"
#include "AssetRegistry/AssetData.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

struct FAssetRenameData;
class UFactory;

/**
 * Watches for newly created assets and appends the project's type suffix.
 *
 * Hook choice matters here. The obvious hook — UImportSubsystem::OnAssetPostImport — is
 * useless for the headline case on UE 5.7: textures, meshes, materials and audio all
 * route through Interchange by default (BaseEngine.ini's InterchangeProjectSettings), and
 * Interchange never broadcasts the import subsystem's delegates. So a plugin built on it
 * silently no-ops on a PNG drag-drop.
 *
 * IAssetRegistry::OnAssetAdded is therefore the backbone: it fires for every new asset
 * regardless of whether it came from Interchange, a legacy factory, or the Add menu.
 * OnAssetPostImport is still hooked, purely as a source of "this one was imported" so the
 * imported/created scope toggles can be honoured. OnAssetPostRename closes the loop for
 * Content Browser inline renaming, described in the .cpp.
 */
class FAssetNamingWatcher
{
public:
	void Startup();
	void Shutdown();

	/** Log-only sweep of every /Game asset whose name lacks a recognised suffix. */
	void AuditProject() const;

	/** Apply suffixes to the current Content Browser selection, regardless of age. */
	void FixSelection();

	/** Drain the pending queue now, ignoring the debounce. Used by HVPNaming.Flush and tests. */
	void FlushNow();

	/** Re-run the placement rules over anything still flagged. */
	void RecheckPlacement() { PlacementWarning.Recheck(); }

	/** Placement violations for one asset, for scripting and tests. */
	TArray<FPlacementViolation> CheckPlacement(const UObject* Asset) const;

	/** As above, for an asset that need not be loaded. */
	TArray<FPlacementViolation> CheckPlacementByPath(const FString& PackageName, const FString& AssetName) const;

	/** Placement violations for a folder path. */
	TArray<FPlacementViolation> CheckFolderPlacement(const FString& PackagePath) const;

	bool HasOutstandingPlacementWarnings() const { return PlacementWarning.HasOutstanding(); }

	static FAssetNamingWatcher* Get();

private:
	FAssetNamingConfig Config;
	FAssetSuffixResolver Resolver;
	FPlacementWarningPresenter PlacementWarning;

	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle PathAddedHandle;
	FDelegateHandle FilesLoadedHandle;
	FDelegateHandle PostImportHandle;
	FDelegateHandle PostRenameHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	/** Queued candidates, plus the timestamp of the most recent one (the debounce clock). */
	TSet<FSoftObjectPath> Pending;
	double LastEnqueueTime = 0.0;

	/** Assets the legacy import path told us about, so we can classify them as imported. */
	TSet<FSoftObjectPath> KnownImported;

	/** The asset registry is still doing its startup scan; every asset looks "new". */
	bool bInitialScanComplete = false;

	/** Set while our own RenameAssets call runs, so the resulting events don't re-enqueue. */
	bool bRenameInProgress = false;

	void HandleAssetAdded(const FAssetData& AssetData);
	void HandleAssetRenamed(const FAssetData& NewData, const FString& OldPath);
	void HandlePathAdded(const FString& Path);
	void HandleFilesLoaded();
	void HandleAssetPostImport(UFactory* Factory, UObject* CreatedObject);
	void HandleAssetPostRename(const TArray<FAssetRenameData>& RenameData);

	bool Tick(float DeltaTime);
	void ProcessPending();
	void EvaluatePlacement(const TArray<UObject*>& Assets);

	/**
	 * Enqueue if the asset passes the cheap registry-level filters.
	 *
	 * bAllowExisting waives the "only assets not yet on disk" test. Creation relies on that
	 * test to tell a brand-new asset from one merely being discovered, but a rename or move
	 * is an explicit user action on an asset that is usually already saved — so the rules
	 * have to run for it too.
	 */
	void Enqueue(const FAssetData& AssetData, bool bAllowExisting = false);

	/**
	 * Name after applying the convention, or empty when no change is needed.
	 * Idempotent: a name that already carries a recognised suffix is returned unchanged.
	 */
	FString BuildConventionalName(const FString& CurrentName, const FString& Suffix) const;

	/** Collect renames for these assets into OutRenames, honouring dry-run and scope. */
	void GatherRenames(const TArray<UObject*>& Assets, TArray<FAssetRenameData>& OutRenames,
		bool bIgnoreScopeToggles) const;

	static bool WasImported(const UObject* Asset);
};
