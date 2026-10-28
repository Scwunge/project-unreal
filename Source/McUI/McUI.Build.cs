using UnrealBuildTool;

public class McUI : ModuleRules
{
	public McUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McNet", "McAssets", "McCore", "McPlayer", "McItem" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore", "InputCore", "Json", "ImageWrapper", "McRender", "McServer" });
	}
}
