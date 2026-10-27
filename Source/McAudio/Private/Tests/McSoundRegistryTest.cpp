#include "McSoundRegistry.h"
#include "McSound.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSoundRegistryTest, "McClient.McAudio.SoundSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcSoundRegistryTest::RunTest(const FString& Parameters)
{
	FMcSoundRegistry Registry;
	const bool bParsed = Registry.Parse(TEXT(R"({
		"block.stone.break": { "sounds": ["dig/stone1", "dig/stone2", "dig/stone3", "dig/stone4"], "subtitle": "x" },
		"weighted": { "sounds": [ { "name": "a", "weight": 3, "volume": 0.5 }, { "name": "b", "pitch": 2.0 } ] },
		"far": { "sounds": [ { "name": "resonate1", "attenuation_distance": 48 } ] },
		"ref": { "sounds": [ { "name": "far", "type": "event", "pitch": 0.5, "volume": 0.5 } ] },
		"loop1": { "sounds": [ { "name": "loop2", "type": "event" } ] },
		"loop2": { "sounds": [ { "name": "loop1", "type": "event" } ] },
		"empty": { "sounds": [] }
	})"));
	TestTrue(TEXT("sounds.json parses"), bParsed);
	TestEqual(TEXT("events"), Registry.NumEvents(), 7);
	TestFalse(TEXT("text that isn't an object fails"), FMcSoundRegistry().Parse(TEXT("[1, 2")));

	// Plain list: the draw picks an equal share each.
	FMcSoundVariant Variant;
	TestTrue(TEXT("stone break"), Registry.Pick(FName("block.stone.break"), 0.0f, Variant));
	TestEqual(TEXT("first"), Variant.Name, FString(TEXT("dig/stone1")));
	TestTrue(TEXT("second draw"), Registry.Pick(FName("block.stone.break"), 0.26f, Variant));
	TestEqual(TEXT("second"), Variant.Name, FString(TEXT("dig/stone2")));
	TestTrue(TEXT("last draw"), Registry.Pick(FName("block.stone.break"), 0.99f, Variant));
	TestEqual(TEXT("last"), Variant.Name, FString(TEXT("dig/stone4")));
	TestTrue(TEXT("draw of 1"), Registry.Pick(FName("block.stone.break"), 1.0f, Variant));
	TestEqual(TEXT("a draw of 1 still lands on the last"), Variant.Name, FString(TEXT("dig/stone4")));

	// Weights: a has 3 of the 4 shares.
	TestTrue(TEXT("weighted 0"), Registry.Pick(FName("weighted"), 0.0f, Variant));
	TestEqual(TEXT("weight 3, first draw"), Variant.Name, FString(TEXT("a")));
	TestEqual(TEXT("its volume"), Variant.Volume, 0.5f);
	TestTrue(TEXT("weighted 0.74"), Registry.Pick(FName("weighted"), 0.74f, Variant));
	TestEqual(TEXT("weight 3, last of its share"), Variant.Name, FString(TEXT("a")));
	TestTrue(TEXT("weighted 0.76"), Registry.Pick(FName("weighted"), 0.76f, Variant));
	TestEqual(TEXT("then b"), Variant.Name, FString(TEXT("b")));
	TestEqual(TEXT("its pitch"), Variant.Pitch, 2.0f);

	// A reference to another event plays that event's file with both volumes and pitches multiplied in.
	TestTrue(TEXT("ref"), Registry.Pick(FName("ref"), 0.0f, Variant));
	TestEqual(TEXT("follows the reference"), Variant.Name, FString(TEXT("resonate1")));
	TestEqual(TEXT("volume multiplied"), Variant.Volume, 0.5f);
	TestEqual(TEXT("pitch multiplied"), Variant.Pitch, 0.5f);
	TestEqual(TEXT("attenuation distance of the target"), Variant.AttenuationDistance, 48.0f);

	// Things that give nothing.
	TestFalse(TEXT("unknown event"), Registry.Pick(FName("nope"), 0.0f, Variant));
	TestFalse(TEXT("empty event"), Registry.Pick(FName("empty"), 0.0f, Variant));
	TestFalse(TEXT("references in a circle give up"), Registry.Pick(FName("loop1"), 0.0f, Variant));

	// What plays: volume and pitch of event and variant multiplied, the pitch kept within 0.5 and 2, the volume capped
	// at 1 with the extra carrying the sound further.
	FMcSoundVariant Quiet;
	Quiet.Volume = 0.5f;
	Quiet.Pitch = 1.5f;
	FMcSoundEvent Event{ FName("x"), FVector::ZeroVector, 0.8f, 1.5f };
	McAudio::FMcSoundPlay Play = McAudio::Resolve(Event, Quiet);
	TestEqual(TEXT("volume"), Play.Volume, 0.4f);
	TestEqual(TEXT("pitch is clamped to 2"), Play.Pitch, 2.0f);
	TestEqual(TEXT("range is 16 at volume under 1"), Play.Range, 16.0f);
	Event.Volume = 4.0f;
	Play = McAudio::Resolve(Event, FMcSoundVariant());
	TestEqual(TEXT("volume capped"), Play.Volume, 1.0f);
	TestEqual(TEXT("range grows with volume"), Play.Range, 64.0f);
	FMcSoundVariant Distant;
	Distant.AttenuationDistance = 48.0f;
	Play = McAudio::Resolve(FMcSoundEvent{ FName("x"), FVector::ZeroVector, 1.0f, 0.1f }, Distant);
	TestEqual(TEXT("range from the variant"), Play.Range, 48.0f);
	TestEqual(TEXT("pitch is clamped to 0.5"), Play.Pitch, 0.5f);

	// Attenuation: linear to nothing at the range.
	const FMcSoundEvent Plain{ FName("x"), FVector::ZeroVector, 1.0f, 1.0f };
	const FMcSoundVariant Standard;
	TestEqual(TEXT("full at the source"), McAudio::GetGainAt(Plain, Standard, 0.0f), 1.0f);
	TestEqual(TEXT("half way"), McAudio::GetGainAt(Plain, Standard, 8.0f), 0.5f);
	TestEqual(TEXT("silent at 16"), McAudio::GetGainAt(Plain, Standard, 16.0f), 0.0f);
	TestEqual(TEXT("silent beyond"), McAudio::GetGainAt(Plain, Standard, 40.0f), 0.0f);
	TestEqual(TEXT("a step (0.15) is quieter"), McAudio::GetGainAt(FMcSoundEvent{ FName("x"), FVector::ZeroVector, 0.15f, 1.0f }, Standard, 0.0f), 0.15f);
	// An explosion (volume 4) is as loud as 1 for 3/4 of its range: 64 blocks, gain falling from the source.
	const FMcSoundEvent Blast{ FName("x"), FVector::ZeroVector, 4.0f, 1.0f };
	TestEqual(TEXT("a blast at 16 blocks"), McAudio::GetGainAt(Blast, Standard, 16.0f), 0.75f);
	TestEqual(TEXT("a blast is silent at 64 blocks"), McAudio::GetGainAt(Blast, Standard, 64.0f), 0.0f);

	return true;
}

// The real thing, when the cache is there: every event the game makes has files that exist and decode.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSoundCacheTest, "McClient.McAudio.CachedSounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcSoundCacheTest::RunTest(const FString& Parameters)
{
	FMcSoundRegistry Registry;
	const FString JsonPath = FPaths::Combine(FMcSoundRegistry::GetSoundsDir(), TEXT("../sounds.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *JsonPath))
	{
		AddInfo(TEXT("cache/assets/minecraft/sounds.json is missing: the cached-sound checks were skipped."));
		return true;
	}
	TestTrue(TEXT("the real sounds.json parses"), Registry.Parse(Text));

	// The events the mobs, blocks, explosions and arrows use.
	const TCHAR* Events[] = {
		TEXT("block.stone.break"), TEXT("block.stone.place"), TEXT("block.stone.step"), TEXT("block.grass.break"),
		TEXT("block.gravel.step"), TEXT("block.wood.break"), TEXT("block.sand.place"), TEXT("block.glass.break"),
		TEXT("entity.zombie.ambient"), TEXT("entity.zombie.hurt"), TEXT("entity.zombie.death"), TEXT("entity.zombie.step"),
		TEXT("entity.pig.ambient"), TEXT("entity.creeper.hurt"), TEXT("entity.creeper.primed"), TEXT("entity.generic.explode"),
		TEXT("entity.skeleton.shoot"), TEXT("entity.arrow.hit"), TEXT("entity.enderman.teleport"), TEXT("entity.slime.jump"),
		TEXT("entity.spider.step"), TEXT("entity.slime.hurt_small"),
		TEXT("block.stone.hit"), TEXT("block.grass.fall"), TEXT("entity.player.hurt"), TEXT("entity.player.death"),
		TEXT("entity.player.small_fall"), TEXT("entity.player.big_fall"),
	};
	for (const TCHAR* Name : Events)
	{
		FMcSoundVariant Variant;
		if (!Registry.Pick(FName(Name), 0.5f, Variant))
		{
			AddError(FString::Printf(TEXT("sounds.json has nothing for %s"), Name));
			continue;
		}
		TArray<uint8> File;
		const FString Path = FPaths::Combine(FMcSoundRegistry::GetSoundsDir(), Variant.Name + TEXT(".ogg"));
		if (!FFileHelper::LoadFileToArray(File, *Path))
		{
			AddError(FString::Printf(TEXT("%s: the sound file %s is missing from the cache"), Name, *Path));
			continue;
		}
		McAudio::FDecodedSound Decoded;
		if (!McAudio::DecodeOgg(File, Decoded))
		{
			AddError(FString::Printf(TEXT("%s: %s doesn't decode"), Name, *Path));
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s has samples"), Name), Decoded.Pcm.Num() > 0);
		TestTrue(*FString::Printf(TEXT("%s sample rate"), Name), Decoded.SampleRate >= 8000 && Decoded.SampleRate <= 48000);
		TestTrue(*FString::Printf(TEXT("%s channels"), Name), Decoded.NumChannels == 1 || Decoded.NumChannels == 2);
		TestTrue(*FString::Printf(TEXT("%s length"), Name), Decoded.Duration > 0.0f && Decoded.Duration < 10.0f);
	}
	return true;
}

#endif
