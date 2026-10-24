using UnrealBuildTool;

public class McNet : ModuleRules
{
	public McNet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McCore", "McWorld" });
		PrivateDependencyModuleNames.AddRange(new string[] { "NetCore" });
	}
}
