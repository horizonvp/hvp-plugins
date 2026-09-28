using UnrealBuildTool;

/**
 * Enforces one-way content dependencies for the project's own plugins: a plugin's content may
 * reference engine content, its own content, and content or modules of plugins it declares in its
 * .uplugin — never /Game, never a project module, never an undeclared plugin.
 *
 * Editor-only. Runs on save as a sticky notification, on demand via HVPReferences.Audit, and
 * headless via -run=HVPReferenceCheck for git hooks and CI.
 */
public class HVPReferenceCheck : ModuleRules
{
	public HVPReferenceCheck(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",          // UCommandlet, GIsEditor
			"AssetRegistry",     // dependency graph
			"Projects",          // IPluginManager, IProjectManager
			"DeveloperSettings", // Project Settings page
			"Slate",
			"SlateCore",
		});
	}
}
