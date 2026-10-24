using UnrealBuildTool;

public class McPlayer : ModuleRules
{
	public McPlayer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McCore", "McWorld", "McNet" });
		PrivateDependencyModuleNames.AddRange(new string[] { "InputCore" });
	}
}
