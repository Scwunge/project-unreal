using UnrealBuildTool;

public class McRender : ModuleRules
{
	public McRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps its helpers in an anonymous namespace; bundled together they collide (some names repeat across files; McSim failed a Shipping build on this).
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McCore", "McWorld", "McItem", "McAssets", "ProceduralMeshComponent" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "ImageWrapper", "Json", "InputCore", "McSim" });
	}
}
