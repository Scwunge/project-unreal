using UnrealBuildTool;

public class McSim : ModuleRules
{
	public McSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "McCore", "McWorld", "McItem" });
	}
}
