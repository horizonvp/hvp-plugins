using UnrealBuildTool;

/** Deliberately empty. The host has no game code; it only enables the plugins. */
public class HVPHost : ModuleRules
{
	public HVPHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
