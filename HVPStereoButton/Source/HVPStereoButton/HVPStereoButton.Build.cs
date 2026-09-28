using UnrealBuildTool;

public class HVPStereoButton : ModuleRules
{
	public HVPStereoButton(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// "UMG" is public because the actor header exposes UWidgetComponent / UUserWidget.
		// UStereoLayerComponent and IStereoLayers live in Engine — no HMD-plugin dependency.
		// Deliberately NO MetaXR module: it ships without import libs on Win64, so a link
		// dependency would force an unbuildable from-source rebuild (see LenovoXIQ.Build.cs).
		// Hand rigs are recognised by class NAME instead.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore"
		});
	}
}
