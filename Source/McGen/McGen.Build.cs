using UnrealBuildTool;

public class McGen : ModuleRules
{
	public McGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps its helpers in an anonymous namespace; bundled together they collide (TypeOf, LavaLevel).
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "McCore", "McWorld", "McItem" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
	}
}
