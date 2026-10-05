using UnrealBuildTool;

/**
 * Everything that needs the editor: creating the asset, binding materials to it, the parameter
 * dropdown and checkboxes on the nodes, and keeping bound materials and open Blueprints in step when
 * a legend changes.
 */
public class HVPPrimitiveDataEditor : ModuleRules
{
	public HVPPrimitiveDataEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// UPrimitiveDataLegend and the node, which the binding and the pin factory both work on.
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
			// Right-click entries on the legend and on materials, and finding every legend in the project.
			"ToolMenus", "ContentBrowser", "AssetTools", "AssetRegistry",
			// UAssetDefinition, the base of the legend's Content Browser name, colour and category.
			"AssetDefinition",
			// UMaterialEditingLibrary: recompiling a material or updating a function after its
			// parameters are rewritten.
			"MaterialEditor",
			// UPrimitiveDataSetLibrary, which the node tests check the gapped case compiles to.
			"HVPPrimitiveDataRuntime",
			// Clickable per-material results rather than lines in the output log.
			"MessageLog",
			// The parameter checkboxes in Set Named Primitive Data (Multiple)'s Details panel.
			"PropertyEditor",
		});
	}
}
