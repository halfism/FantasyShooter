using UnrealBuildTool;
using System.Collections.Generic;

public class FantasyShooterEditorTarget : TargetRules
{
	public FantasyShooterEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("FantasyShooter");
	}
}
