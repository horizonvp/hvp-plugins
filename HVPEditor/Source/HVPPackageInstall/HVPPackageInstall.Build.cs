using UnrealBuildTool;

/**
 * Package and Install to Headset: one Platforms-menu entry that packages for Android (ASTC), runs
 * the build's install script, and reports the outcome in a window. Editor only; nothing here is
 * serialised into an asset.
 */
public class HVPPackageInstall : ModuleRules
{
	public HVPPackageInstall(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// UHVPPackageInstallSettings is a public header deriving from UDeveloperSettings.
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore",
			// The menu entry, added to the level editor's Platforms menu.
			"ToolMenus",
			// The Package and Install window's place under Window > Tools.
			"WorkspaceMenuStructure",
			// FEditorFileUtils (save before packaging) and FUnrealEdMisc (-unrealexe for UAT).
			"UnrealEd",
			// IUATHelperModule: runs UAT with the editor's own progress toast, cancel button and log.
			"UATHelper",
			// UPlatformsMenuSettings and UProjectPackagingSettings: what the Platforms menu has chosen.
			"DeveloperToolSettings",
			// PlatformInfo::FindPlatformInfo, for the flavour's UAT arguments (-cookflavor=ASTC).
			"DesktopPlatform",
		});
	}
}
