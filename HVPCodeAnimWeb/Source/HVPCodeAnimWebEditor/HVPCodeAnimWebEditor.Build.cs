using UnrealBuildTool;

/**
 * Authoring a Code Animation Web: the Animation Output checkbox on variables, the State Values table
 * with its override ticks, state pickers, keeping Webs in step with their enums, and the New Asset entry.
 */
public class HVPCodeAnimWebEditor : ModuleRules
{
	public HVPCodeAnimWebEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			"HVPCodeAnimWeb",
			// The Web's Blueprint nodes, for the Output pin's dropdown.
			"HVPCodeAnimWebUncooked",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore", "UnrealEd",
			// Slate templates instantiated here (combo buttons, key handling) link against InputCore.
			"InputCore",
			// FBlueprintEditorModule's variable customizations, and FBlueprintEditor to find the Blueprint.
			"Kismet",
			// Pin types, and the nodes the tests build graphs from.
			"BlueprintGraph",
			// SGraphPin and the visual pin factory registry.
			"GraphEditor",
			// Details customizations.
			"PropertyEditor",
			// FPropertyBagInstanceDataDetails: the override ticks on each state's values.
			"StructUtilsEditor",
			// EAssetTypeCategories, for where the New Asset entry goes.
			"AssetTools",
			// Adding and removing the outputs' event dispatchers.
			"BlueprintEditorLibrary",
			// The Web Graph: its toolbar button, and the Blueprint editor's document tabs.
			"ToolMenus", "EditorFramework",
		});
	}
}
