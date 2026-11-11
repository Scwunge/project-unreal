using UnrealBuildTool;

public class McAssets : ModuleRules
{
	public McAssets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "RenderCore", "RHI", "ImageWrapper" });
		if (Target.Type != TargetType.Server)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "ApplicationCore", "Slate", "SlateCore", "MoviePlayer" });
			PrivateDefinitions.Add("MC_LOAD_SCREEN=1");
		}
		else
		{
			PrivateDefinitions.Add("MC_LOAD_SCREEN=0");
		}
	}
}
