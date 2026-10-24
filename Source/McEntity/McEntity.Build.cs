using UnrealBuildTool;

public class McEntity : ModuleRules
{
	public McEntity(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McNet", "McAssets" });
	}
}
