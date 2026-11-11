using UnrealBuildTool;

public class McServer : ModuleRules
{
	public McServer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps its helpers in an anonymous namespace; bundled together they collide (some names repeat across files; McSim failed a Shipping build on this).
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McCore", "McWorld", "McGen", "McSim", "McEntity", "McNet", "McItem", "Json" });
		AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
	}
}
