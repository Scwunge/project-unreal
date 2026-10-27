using UnrealBuildTool;

public class McClient : ModuleRules
{
	public McClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "McCore", "McWorld", "McGen", "McSim", "McEntity", "McServer", "McNet", "McPlayer", "McItem" });

		if (Target.Type != TargetType.Server)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "McAssets", "McRender", "McUI", "McAudio" });
			PrivateDefinitions.Add("MC_WITH_RENDER=1");
		}
		else
		{
			PrivateDefinitions.Add("MC_WITH_RENDER=0");
		}
	}
}
