using UnrealBuildTool;

/**
 * The Web's Blueprint nodes: Set / Get Web State, and Get From / To Output.
 * UncookedOnly, as K2Nodes must be: they expand into ordinary calls on
 * UCodeAnimationWeb at compile time, so a packaged game needs only the runtime module.
 */
public class HVPCodeAnimWebUncooked : ModuleRules
{
	public HVPCodeAnimWebUncooked(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// UK2Node, the public base of every node here; the editor module includes them for its pin widget.
			"BlueprintGraph",
			"HVPCodeAnimWeb",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// FKismetCompilerContext, handed to ExpandNode.
			"KismetCompiler",
			// FSlateIcon for the nodes' icons.
			"SlateCore",
		});

		// FCompilerResultsLog and FBlueprintEditorUtils, guarded as the engine's own K2Node modules guard them.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
