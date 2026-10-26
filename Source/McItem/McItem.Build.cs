using UnrealBuildTool;

public class McItem : ModuleRules
{
	public McItem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps its helpers in an anonymous namespace; bundled together they could collide.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Json", "McCore" });
	}
}
