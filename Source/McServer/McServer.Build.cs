using UnrealBuildTool;

public class McServer : ModuleRules
{
	public McServer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McCore", "McWorld", "McGen", "McSim", "McEntity", "McNet", "McItem", "Json" });
	}
}
