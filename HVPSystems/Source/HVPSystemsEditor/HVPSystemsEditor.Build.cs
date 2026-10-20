using UnrealBuildTool;

public class HVPSystemsEditor : ModuleRules
{
	public HVPSystemsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"UnrealEd",
			"BlueprintGraph",
			"BlueprintEditorLibrary",
			"HVPGameflow",
			"HVPCodeAnimation",
			// The HVP.Text automation tests, and Make HVPText Font.
			"HVPText",
			// Make HVPText Font: a Font Face context menu entry, its dialog, and the font tools it runs.
			"ContentBrowser",
			"Json",
			"Projects",
			"PythonScriptPlugin",
			"Slate",
			"SlateCore",
			"ToolMenus"
		});
	}
}
