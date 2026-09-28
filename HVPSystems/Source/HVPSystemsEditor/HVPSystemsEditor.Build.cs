using UnrealBuildTool;

public class HVPSystemsEditor : ModuleRules
{
	public HVPSystemsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"UnrealEd",
			"BlueprintGraph",
			"BlueprintEditorLibrary",
			"HVPGameflow",
			"HVPCodeAnimation"
		});
	}
}
