using UnrealBuildTool;

/**
 * The index asset and the Blueprint node. UncookedOnly, for the same reason LenovoXIQUncooked is: a
 * K2Node belongs in a module the editor and the cooker have but a packaged game does not, and the
 * blueprint compiler warns about any other placement.
 *
 * The asset lives here too, and that is deliberate rather than convenient. Nothing at runtime ever
 * reads an index - the node resolves its slot while the Blueprint compiles and bakes the number into
 * an engine call - so UPrimitiveDataIndex reports IsEditorOnly() and the cooker leaves it out.
 * There is no runtime module in this plugin at all.
 *
 * Named "...Uncooked" rather than plain "HVPPrimitiveData" so neither module's directory is a
 * string prefix of the other's: UHT relativizes generated includes against the first include path
 * that matches, and a prefix produces broken paths (see LenovoXIQUncooked.Build.cs).
 */
public class HVPPrimitiveDataUncooked : ModuleRules
{
	public HVPPrimitiveDataUncooked(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// UK2Node is the public base of UK2Node_SetIndexedPrimitiveData, which the editor module
			// includes for its pin factory.
			"BlueprintGraph",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// FKismetCompilerContext, handed to ExpandNode.
			"KismetCompiler",
			// FSlateIcon for the node's palette icon.
			"SlateCore",
		});

		// FCompilerResultsLog and FBlueprintEditorUtils. Editor-only, guarded as the engine's own K2Node
		// modules guard it.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
