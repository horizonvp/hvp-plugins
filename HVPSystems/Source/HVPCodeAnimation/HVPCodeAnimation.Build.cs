using UnrealBuildTool;

public class HVPCodeAnimation : ModuleRules
{
	public HVPCodeAnimation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MediaAssets",
			"HVPGameflow"
		});
	}
}
