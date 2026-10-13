using UnrealBuildTool;

/**
 * HVPText: letters as data, drawn in one batch per frame. A world subsystem owns every letter and
 * letter group and draws each distinct glyph of each font as one instanced component; groups carry
 * the font, technique and size, and may follow a scene component. Fonts come from the import tools
 * in HVPSystems/Tools/HVPText; the plugin ships none.
 */
public class HVPText : ModuleRules
{
	public HVPText(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// UTextRenderSettings is a public header deriving from UDeveloperSettings.
			"DeveloperSettings",
			// UTextGrabBridge is a HandGrab item source, and its header carries FHandGrabbableParams.
			"HVPHandGrab",
		});
	}
}
