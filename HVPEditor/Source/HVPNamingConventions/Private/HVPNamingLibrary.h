#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HVPNamingLibrary.generated.h"

/**
 * Script access to the convention rules, so the folder rules can be queried from Python
 * or an editor utility widget rather than only observed through a notification.
 */
UCLASS()
class UHVPNamingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Human-readable placement problems for this asset; empty means its folder is fine. */
	UFUNCTION(BlueprintCallable, Category = "HVP Naming")
	static TArray<FString> CheckAssetPlacement(UObject* Asset);

	/** As above, without needing the asset loaded. AssetName is the name without the path. */
	UFUNCTION(BlueprintCallable, Category = "HVP Naming")
	static TArray<FString> CheckPlacementByPath(const FString& PackageName, const FString& AssetName);

	/** Problems with a FOLDER path, e.g. "/Game/BiogenCD38/Modules/AMR/Textures". */
	UFUNCTION(BlueprintCallable, Category = "HVP Naming")
	static TArray<FString> CheckFolderPlacement(const FString& PackagePath);

	/** Re-run the rules over anything currently flagged, clearing the warning if all pass. */
	UFUNCTION(BlueprintCallable, Category = "HVP Naming")
	static void RecheckPlacement();

	/** True while at least one asset is still sitting in an invalid folder. */
	UFUNCTION(BlueprintCallable, Category = "HVP Naming")
	static bool HasOutstandingPlacementWarnings();
};
