#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HVPPackageInstallSettings.generated.h"

/**
 * Per-user settings for Platforms > Package and Install to Headset. Saved in the user's
 * EditorPerProjectUserSettings, not the project: the paths are this machine's.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "HVP Package and Install"))
class HVPPACKAGEINSTALL_API UHVPPackageInstallSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * The Android SDK, given to packaging and to the install script as ANDROID_HOME. Leave empty to
	 * use the machine's own. Set it when the machine-wide ANDROID_HOME points somewhere else: the
	 * install script trusts ANDROID_HOME over everything but an adb already on the PATH.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Environment")
	FDirectoryPath AndroidHome;

	/** A JDK for packaging (Gradle needs 17 or later), given as JAVA_HOME. Leave empty to use the machine's own. */
	UPROPERTY(config, EditAnywhere, Category = "Environment")
	FDirectoryPath JavaHome;

	/** Where the package is archived. Empty: the Platforms menu's last package directory, else <Project>/Packaged. */
	UPROPERTY(config, EditAnywhere, Category = "Package")
	FDirectoryPath ArchiveDirectory;

	/** Install to this device (adb serial). Empty: the only one connected. */
	UPROPERTY(config, EditAnywhere, Category = "Install")
	FString DeviceSerial;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
