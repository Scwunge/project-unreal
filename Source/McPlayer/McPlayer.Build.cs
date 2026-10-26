using UnrealBuildTool;

public class McPlayer : ModuleRules
{
	public McPlayer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McCore", "McWorld", "McNet", "McEntity", "McItem" });
		PrivateDependencyModuleNames.AddRange(new string[] { "InputCore" });

		// Other players' bodies are drawn by McRender, which the dedicated server doesn't have.
		if (Target.Type != TargetType.Server)
		{
			PrivateDependencyModuleNames.Add("McRender");
			PrivateDefinitions.Add("MC_WITH_RENDER=1");
		}
		else
		{
			PrivateDefinitions.Add("MC_WITH_RENDER=0");
		}
	}
}
