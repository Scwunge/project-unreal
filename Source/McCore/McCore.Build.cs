using UnrealBuildTool;

public class McCore : ModuleRules
{
	public McCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Json" });
		PrivateDependencyModuleNames.Add("ImageWrapper");
	}
}
