#include "HVPNamingLibrary.h"

#include "AssetNamingWatcher.h"

namespace
{
	TArray<FString> ToLines(const TArray<FPlacementViolation>& Violations)
	{
		TArray<FString> Lines;
		Lines.Reserve(Violations.Num());
		for (const FPlacementViolation& Violation : Violations)
		{
			Lines.Add(FString::Printf(TEXT("[%s] %s"), *Violation.Rule, *Violation.ToLine()));
		}
		return Lines;
	}
}

TArray<FString> UHVPNamingLibrary::CheckAssetPlacement(UObject* Asset)
{
	const FAssetNamingWatcher* Watcher = FAssetNamingWatcher::Get();
	if (!Watcher || !Asset)
	{
		return {};
	}
	return ToLines(Watcher->CheckPlacement(Asset));
}

TArray<FString> UHVPNamingLibrary::CheckPlacementByPath(const FString& PackageName, const FString& AssetName)
{
	const FAssetNamingWatcher* Watcher = FAssetNamingWatcher::Get();
	if (!Watcher)
	{
		return {};
	}
	return ToLines(Watcher->CheckPlacementByPath(PackageName, AssetName));
}

TArray<FString> UHVPNamingLibrary::CheckFolderPlacement(const FString& PackagePath)
{
	const FAssetNamingWatcher* Watcher = FAssetNamingWatcher::Get();
	if (!Watcher)
	{
		return {};
	}
	return ToLines(Watcher->CheckFolderPlacement(PackagePath));
}

void UHVPNamingLibrary::RecheckPlacement()
{
	if (FAssetNamingWatcher* Watcher = FAssetNamingWatcher::Get())
	{
		Watcher->RecheckPlacement();
	}
}

bool UHVPNamingLibrary::HasOutstandingPlacementWarnings()
{
	const FAssetNamingWatcher* Watcher = FAssetNamingWatcher::Get();
	return Watcher && Watcher->HasOutstandingPlacementWarnings();
}
