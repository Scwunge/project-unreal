using UnrealBuildTool;

public class McUI : ModuleRules
{
	public McUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each file keeps its helpers in an anonymous namespace; bundled together they collide (some names repeat across files; McSim failed a Shipping build on this).
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McNet", "McAssets", "McCore", "McPlayer", "McItem" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore", "InputCore", "Json", "ImageWrapper", "McRender", "McAudio", "McServer", "McWorld" });
		// FreeType reads PNG and gzip bitmap fonts too, so its own libraries come along.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "FreeType2", "zlib", "UElibPNG");
	}
}
