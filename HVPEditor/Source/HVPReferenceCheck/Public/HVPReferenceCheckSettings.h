#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HVPReferenceCheckSettings.generated.h"

/**
 * Project Settings -> Editor -> HVP Reference Check.
 *
 * Saved to Config/DefaultEditor.ini (defaultconfig) so the whole team enforces the same rules.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "HVP Reference Check"))
class HVPREFERENCECHECK_API UHVPReferenceCheckSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Editor"); }
	virtual FName GetSectionName() const override { return TEXT("HVPReferenceCheck"); }

	static const UHVPReferenceCheckSettings& Get() { return *GetDefault<UHVPReferenceCheckSettings>(); }

	/** Master switch for the on-save check and the console command. The commandlet ignores this. */
	UPROPERTY(EditAnywhere, config, Category = "General")
	bool bEnabled = true;

	/** Check a plugin package every time it is saved and show a notification listing any leaks. */
	UPROPERTY(EditAnywhere, config, Category = "General")
	bool bCheckOnSave = true;

	/**
	 * Treat a reference to an ENGINE plugin's content or module as a violation unless that plugin is
	 * declared in the referencing plugin's .uplugin. Off by default: Epic's own plugins are always
	 * present, so the leak is harmless, but declaring them is still the tidy thing to do.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rules")
	bool bRequireDeclaredEnginePlugins = false;

	/**
	 * Project plugins that are not checked at all. Use this for third-party plugins copied into
	 * Plugins/ that you do not maintain. HVP plugins should never be listed here.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rules")
	TArray<FString> ExemptPlugins;

	/**
	 * Package roots a plugin may reference even though the rules would forbid them, e.g.
	 * "/Game/Shared/Fonts". Every entry is a deliberate exception; keep the list short.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rules")
	TArray<FString> AllowedRoots;
};
