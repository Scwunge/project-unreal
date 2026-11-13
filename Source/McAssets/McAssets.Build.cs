using UnrealBuildTool;

public class McAssets : ModuleRules
{
	public McAssets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps its helpers in an anonymous namespace; bundled together they collide (some names repeat across files; McSim failed a Shipping build on this).
		bUseUnity = false;
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
