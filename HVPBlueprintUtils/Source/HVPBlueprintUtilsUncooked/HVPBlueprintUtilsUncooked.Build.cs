using UnrealBuildTool;

/**
 * The plugin's Blueprint nodes. UncookedOnly, and it is not a stylistic choice.
 *
 * The blueprint compiler checks the node's package for PKG_UncookedOnly or PKG_Developer and warns
 * "K2 Nodes should only be defined in a Developer or UncookedOnly module" otherwise. An Editor
 * module is absent while content is cooked, so a runtime blueprint holding one of its nodes would
 * lose that node at cook time - the warning is about a packaging failure, not tidiness.
 *
 * UncookedOnly is present in the editor and in commandlets (the cooker included) but not in a
 * packaged game, which is exactly the lifetime a K2Node wants: it expands into engine nodes at
 * compile time, so the shipped build never needs it.
 *
 * A runtime module, if this plugin ever needs one, should be named "HVPBlueprintUtilsRuntime" rather
 * than plain "HVPBlueprintUtils": UHT relativizes generated includes against the first include path
 * that matches, and a module directory that is a string prefix of another's produces broken paths.
 */
public class HVPBlueprintUtilsUncooked : ModuleRules
{
	public HVPBlueprintUtilsUncooked(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// UK2Node itself, plus every intermediate node the expansion spawns: CallFunction,
			// IfThenElse, ExecutionSequence, TemporaryVariable, AssignmentStatement.
			"BlueprintGraph",
			// FKismetCompilerContext, which ExpandNode is handed.
			"KismetCompiler",
			// FSlateIcon / FAppStyle for the palette icon. SlateCore is a runtime module, so it is
			// safe to depend on from here - unlike UnrealEd or PropertyEditor, which are Editor-only
			// and would have to sit behind a Target.bBuildEditor guard.
			"SlateCore",
		});

		// FCompilerResultsLog and FEdGraphToken live in UnrealEd, and ExpandNode cannot avoid them:
		// MessageLog is how a node reports a bad graph, and SpawnIntermediateNode itself calls
		// NotifyIntermediateObjectCreation on the log. Guarded the way the engine's own K2Node
		// modules guard it - UnrealEd is Editor-only, and an UncookedOnly module is compiled for
		// editor and commandlet targets where it exists, but the guard keeps any other target from
		// trying to link against something that is not there.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
