using UnrealBuildTool;

public class SinkSubsystemRaceFix : ModuleRules
{
	public SinkSubsystemRaceFix(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicIncludePaths.AddRange(new string[] {});
		PrivateIncludePaths.AddRange(new string[] {});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AbstractInstance",
			"FactoryGame",
			"ReplicationGraph",
			"SML"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "MoviePlayer", "UMG", "Slate", "SlateCore" });
		DynamicallyLoadedModuleNames.AddRange(new string[] {});
	}
}
