using UnrealBuildTool;

public class FantasyShooter : ModuleRules
{
	public FantasyShooter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AudioMixer",
			"SlateCore",
			"Slate",
			"ApplicationCore"
		});
	}
}
