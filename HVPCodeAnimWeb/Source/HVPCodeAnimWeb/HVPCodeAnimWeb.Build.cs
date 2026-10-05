using UnrealBuildTool;

/**
 * The Code Animation Web component: evaluating states and transitions, and publishing output changes.
 * Ships in packaged builds, so nothing here may depend on the editor.
 */
public class HVPCodeAnimWeb : ModuleRules
{
	public HVPCodeAnimWeb(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			// Engine: UActorComponent, UCurveFloat, and EEasingFunc from the Kismet math library.
			"Core", "CoreUObject", "Engine",
		});
	}
}
