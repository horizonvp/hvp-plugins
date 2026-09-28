#pragma once

#include "AssetPlacementRules.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class SNotificationItem;
struct FAssetNamingConfig;
struct FAssetData;

/**
 * Shows — and, crucially, retracts — the "this asset is in the wrong folder" warning.
 *
 * The warning is a sticky Slate notification rather than a modal dialog. That is not a
 * cosmetic choice: a modal dialog freezes the editor, so the user could not move the
 * offending asset while it was open, and moving it is the only thing that should dismiss
 * the warning.
 *
 * Offending assets are tracked as weak object pointers, not paths, because moving an asset
 * in Unreal renames the same UObject into a new package — the pointer survives the move,
 * so re-checking after a move is just re-running the rules on the same objects.
 */
class FPlacementWarningPresenter
{
public:
	void Initialise(const FAssetNamingConfig* InConfig);
	void Shutdown();

	/** Add a batch of freshly-found violations to whatever is already outstanding. */
	void Report(const TArray<FPlacementViolation>& NewViolations);

	/** Re-run the rules over everything outstanding; clears the warning if all now pass. */
	void Recheck();

	/** Add a folder that breaks the rules. Folders have no UObject, so they're tracked by path. */
	void ReportFolder(const FString& PackagePath, const TArray<FPlacementViolation>& NewViolations);

	/** True while at least one asset or folder is still misplaced. */
	bool HasOutstanding() const { return Outstanding.Num() > 0 || OutstandingFolders.Num() > 0; }

private:
	const FAssetNamingConfig* Config = nullptr;

	TSharedPtr<SNotificationItem> Notification;
	TArray<TWeakObjectPtr<UObject>> Outstanding;

	/**
	 * Offending folders, by package path. Unlike assets these can't be weak pointers —
	 * a folder isn't a UObject — so they're re-validated against the asset registry, and
	 * dropped when the path stops existing (renamed or deleted).
	 */
	TArray<FString> OutstandingFolders;

	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle PathRemovedHandle;

	/** Re-evaluate and refresh (or dismiss) the notification. */
	void Refresh();

	/** Violations for the still-valid outstanding assets, pruning dead entries. */
	TArray<FPlacementViolation> EvaluateOutstanding();

	void ShowFirstOffender();
	void Dismiss(bool bSuccess);

	void HandleAssetRenamed(const FAssetData& NewData, const FString& OldPath);
	void HandleAssetRemoved(const FAssetData& Data);
	void HandlePathRemoved(const FString& Path);
};
