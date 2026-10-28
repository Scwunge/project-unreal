using UnrealBuildTool;

public class McAudio : ModuleRules
{
	public McAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "McCore", "McWorld", "McAssets" });
		// sounds.json, and the OGG files decoded to PCM for the engine to play.
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "VorbisAudioDecoder", "McRender" });
		// FSoundQualityInfo, which the decoder fills in, is defined in a header of this module; nothing is linked from it.
		PrivateIncludePathModuleNames.Add("TargetPlatform");
	}
}
