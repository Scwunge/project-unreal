using UnrealBuildTool;

public class McUI : ModuleRules
{
	public McUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McNet", "McAssets" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore" });
	}
}
