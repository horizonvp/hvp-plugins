using UnrealBuildTool;

/**
 * Editor type, not UncookedOnly. The K2Node rule - "K2 Nodes should only be defined in a Developer
 * or UncookedOnly module" - is about node CLASSES, whose paths get serialised into every graph that
 * places one. This module defines no node types and writes nothing into an asset; it only reads a
 * graph and changes the editor's selection. Nothing here has to exist at cook time.
 */
public class HVPGraphSelectEditor : ModuleRules
{
	public HVPGraphSelectEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// UHVPGraphSelectSettings is a public header deriving from UDeveloperSettings.
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore",
			// The context-menu entries themselves.
			"ToolMenus",
			// SGraphEditor - which lives in UnrealEd's GraphEditor.h, NOT in the GraphEditor module,
			// despite the name. FindGraphEditorForGraph and the selection calls come from there.
			"UnrealEd",
			// UEdGraphSchema_K2::PC_Exec, for telling an exec pin from a data pin. UGraphNodeContext-
			// MenuContext and UEdGraphNode are Engine, so they need nothing extra.
			"BlueprintGraph",
			// SGraphPanel, for finding the graph under the cursor from the widget path, and
			// UGraphEditorSettings, for standing down when middle-click is configured to pan.
			"GraphEditor",
			// EKeys::MiddleMouseButton. FPointerEvent comes from SlateCore, but the key constants
			// it is compared against are InputCore's.
			"InputCore",
		});
	}
}
