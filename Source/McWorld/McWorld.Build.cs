using UnrealBuildTool;

public class McWorld : ModuleRules
{
	public McWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "McCore" });
	}
}
