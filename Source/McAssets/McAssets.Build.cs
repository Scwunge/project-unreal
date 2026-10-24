using UnrealBuildTool;

public class McAssets : ModuleRules
{
	public McAssets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "McCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
	}
}
