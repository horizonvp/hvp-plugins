using UnrealBuildTool;

/**
 * Everything that needs the editor: creating the asset, binding materials to it, the parameter
 * dropdown on the node, and keeping bound materials and open Blueprints in step when an index changes.
 */
public class HVPPrimitiveDataEditor : ModuleRules
{
	public HVPPrimitiveDataEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// UPrimitiveDataIndex and the node, which the binding and the pin factory both work on.
			"HVPPrimitiveDataUncooked",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore", "UnrealEd",
			// SComboBox is a template, so its key handling (EKeys::Enter, ::PageUp, ...) is instantiated
			// in this module and has to link against InputCore here.
			"InputCore",
			// SGraphPin, and FEdGraphUtilities' visual pin factory registry.
			"GraphEditor", "BlueprintGraph",
			// Right-click entries on the index and on materials, and finding every index in the project.
			"ToolMenus", "ContentBrowser", "AssetTools", "AssetRegistry",
			// UAssetDefinition, the base of the index's Content Browser name, colour and category.
			"AssetDefinition",
			// UMaterialEditingLibrary: recompiling a material or updating a function after its
			// parameters are rewritten.
			"MaterialEditor",
			// Clickable per-material results rather than lines in the output log.
			"MessageLog",
		});
	}
}
