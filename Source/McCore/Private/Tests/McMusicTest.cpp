#include "McMusic.h"
#include "McBiomeRegistry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMusicTest, "McClient.McCore.Music",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcMusicTest::RunTest(const FString& Parameters)
{
	using EAction = FMcMusicPicker::EAction;
	FMcDimensionType::FMusic Game;
	Game.Sound = TEXT("music.game");

	// The first track after the short wait.
	{
		FMcMusicPicker Picker;
		int32 Ticks = 0;
		while (Picker.Tick(&Game, NAME_None, 0.5f) != EAction::Start && Ticks < 1000)
		{
			++Ticks;
		}
		TestEqual(TEXT("first track after 100 ticks"), Ticks + 1, FMcMusicPicker::FirstDelay);

		// While it plays, nothing; when it ends, a wait between the delays (half way for a draw of 0.5).
		TestTrue(TEXT("nothing while playing"), Picker.Tick(&Game, TEXT("music.game"), 0.5f) == EAction::None);
		TestTrue(TEXT("nothing as it ends"), Picker.Tick(&Game, NAME_None, 0.5f) == EAction::None);
		TestEqual(TEXT("then 15 minutes"), Picker.GetDelay(), 17999);
	}

	// Nowhere with music: nothing starts.
	{
		FMcMusicPicker Picker;
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			TestTrue(TEXT("no music, no track"), Picker.Tick(nullptr, NAME_None, 0.5f) == EAction::None);
		}
	}

	// The End's music stops the Overworld's track and starts at once.
	{
		FMcDimensionType::FMusic End;
		End.Sound = TEXT("music.end");
		End.MinDelay = 6000;
		End.bReplaceCurrent = true;
		FMcMusicPicker Picker;
		TestTrue(TEXT("stops the other track"), Picker.Tick(&End, TEXT("music.game"), 0.5f) == EAction::Stop);
		TestTrue(TEXT("starts its own"), Picker.Tick(&End, NAME_None, 0.5f) == EAction::Start);
		TestTrue(TEXT("leaves its own alone"), Picker.Tick(&End, TEXT("music.end"), 0.5f) == EAction::None);
	}

	// Creative players hear the creative list where there is one.
	{
		TMap<FName, FMcDimensionType::FMusic> Lists;
		Lists.Add(TEXT("default"), Game);
		TestEqual(TEXT("default without a creative list"), FMcMusicPicker::Choose(Lists, true)->Sound, FName(TEXT("music.game")));
		FMcDimensionType::FMusic Creative;
		Creative.Sound = TEXT("music.creative");
		Lists.Add(TEXT("creative"), Creative);
		TestEqual(TEXT("creative"), FMcMusicPicker::Choose(Lists, true)->Sound, FName(TEXT("music.creative")));
		TestEqual(TEXT("survival"), FMcMusicPicker::Choose(Lists, false)->Sound, FName(TEXT("music.game")));
	}

	// Biomes with their own music, from the cache when it's there.
	FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	if (Biomes.EnsureLoaded())
	{
		const FMcDimensionType::FMusic* Meadow = Biomes.GetMusic(Biomes.Find(TEXT("minecraft:meadow")).Get(0), TEXT("default"));
		TestTrue(TEXT("the meadow's own music"), Meadow && Meadow->Sound == FName(TEXT("music.overworld.meadow")));
		TestNull(TEXT("the plains have none of their own"), Biomes.GetMusic(Biomes.GetPlains(), TEXT("default")));
	}
	else
	{
		AddInfo(TEXT("No biomes in cache/: the biome music checks were skipped."));
	}
	return true;
}

#endif
