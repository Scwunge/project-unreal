using UnrealBuildTool;

public class McRender : ModuleRules
{
	public McRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McWorld", "McAssets" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore" });
	}
}
