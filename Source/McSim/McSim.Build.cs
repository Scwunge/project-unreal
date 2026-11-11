using UnrealBuildTool;

public class McSim : ModuleRules
{
	public McSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps its helpers in an anonymous namespace; bundled together they collide (a Shipping build failed on NameOf, Up, Down, Around, StateOf, NumberProperty, MaxValue between McPortal and McRandomTicks).
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "McCore", "McWorld", "McItem" });
		PrivateDependencyModuleNames.AddRange(new string[] { "McGen", "Json" });
	}
}
