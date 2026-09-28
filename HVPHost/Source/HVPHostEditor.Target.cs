using UnrealBuildTool;
using System.Collections.Generic;

public class HVPHostEditorTarget : TargetRules
{
	public HVPHostEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("HVPHost");
	}
}
