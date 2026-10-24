using UnrealBuildTool;

public class McGen : ModuleRules
{
	public McGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "McCore", "McWorld" });
	}
}
