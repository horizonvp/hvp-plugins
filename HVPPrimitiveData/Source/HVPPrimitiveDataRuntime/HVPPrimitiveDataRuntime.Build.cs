using UnrealBuildTool;

/**
 * The one piece of this plugin that ships: the merge-and-write function Set Named Primitive Data
 * (Multiple) compiles to when the parameters it sets are not side by side. Everything else still
 * compiles away - the single-parameter node, and the multiple node whenever its slots are contiguous,
 * become plain engine calls.
 *
 * Named "...Runtime" rather than plain "HVPPrimitiveData" for the same reason the other two are
 * suffixed: no module directory may be a string prefix of another's (see
 * HVPPrimitiveDataUncooked.Build.cs).
 */
public class HVPPrimitiveDataRuntime : ModuleRules
{
	public HVPPrimitiveDataRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});
	}
}
