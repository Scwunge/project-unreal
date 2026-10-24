using UnrealBuildTool;

public class McClient : ModuleRules
{
	public McClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "McNet", "McWorld", "McAssets", "McRender", "McPlayer", "McEntity", "McUI", "McAudio" });
	}
}
