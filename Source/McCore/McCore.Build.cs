using UnrealBuildTool;

public class McCore : ModuleRules
{
	public McCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps its helpers in an anonymous namespace; bundled together they collide (some names repeat across files; McSim failed a Shipping build on this).
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Json" });
		PrivateDependencyModuleNames.Add("ImageWrapper");
	}
}
