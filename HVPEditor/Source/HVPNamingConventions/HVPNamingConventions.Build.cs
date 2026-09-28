using UnrealBuildTool;

public class HVPNamingConventions : ModuleRules
{
	public HVPNamingConventions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",          // UImportSubsystem, GEditor
			"AssetRegistry",     // OnAssetAdded / OnFilesLoaded
			"AssetTools",        // IAssetTools::RenameAssets, OnAssetPostRename
			"ContentBrowser",    // HVPNaming.FixSelected
			"Json",              // conventions.json
			"Projects",
			"DeveloperSettings", // Project Settings page
			"Slate",
			"SlateCore",
		});

		// Deliberately NO dependency on Niagara / PCG / ControlRig / EnhancedInput /
		// MetaSound / Foliage / LevelSequence / UMGEditor. Those classes are resolved by
		// path string at runtime, so the plugin still loads — and simply skips those
		// suffixes — in a project where the owning plugin is disabled.
	}
}
