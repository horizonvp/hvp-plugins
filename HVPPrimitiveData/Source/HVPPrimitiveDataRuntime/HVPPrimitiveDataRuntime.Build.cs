using UnrealBuildTool;

/**
 * The one piece of this plugin that ships: the merge-and-write functions the multiple nodes compile to -
 * Set Named Primitive Data (Multiple) when the parameters it sets are not side by side, and Set Named
 * Instance Data (Multiple) always. Everything else still compiles away - the single-parameter nodes, and
 * the primitive multiple node whenever its slots are contiguous, become plain engine calls.
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
