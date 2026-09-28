using UnrealBuildTool;

public class HVPPaletteEditor : ModuleRules
{
	public HVPPaletteEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// UHVPPaletteSettings is a public header deriving from UDeveloperSettings.
			"DeveloperSettings",
		});

		// Editor-only, and it stays that way: this is the plugin's ONLY module and it is Type "Editor"
		// in the .uplugin, so nothing here reaches a packaged build. There is deliberately no runtime
		// module - the assets this generates reference Engine and the collection alone, so there is
		// nothing left for a shipping game to link against.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore", "UnrealEd", "AssetRegistry",
			// The right-click entry and the asset selection behind it.
			"ToolMenus", "ContentBrowser", "AssetTools",
			// Programmatic authoring: UMaterialEditingLibrary for the material function, the K2Node
			// classes for the Blueprint graph.
			"MaterialEditor", "BlueprintGraph", "KismetCompiler",
			// FImage / FImageView: reading a texture's source mip and decoding it to linear,
			// gamma handled by the image layer rather than by hand.
			"ImageCore",
		});
	}
}
