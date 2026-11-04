#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockUse.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McItemRegistry.h"
#include "McJukebox.h"
#include "McWorldActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// A disc names its song, and the song's file says how long it is and what a comparator reads.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJukeboxSongsTest, "McClient.McItem.JukeboxSongs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJukeboxSongsTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcSong Song;
	if (TestTrue(TEXT("cat is a disc"), McJukebox::FindSong(TEXT("minecraft:music_disc_cat"), Song)))
	{
		TestEqual(TEXT("its song is cat"), Song.Id.ToString(), FString(TEXT("cat")));
		TestEqual(TEXT("185 seconds"), Song.LengthTicks, 3700);
		TestEqual(TEXT("a comparator reads 2"), Song.ComparatorOutput, 2);
		TestEqual(TEXT("the sound event"), McJukebox::GetSoundEvent(Song).ToString(), FString(TEXT("music_disc.cat")));
	}
	TestEqual(TEXT("13 reads 1"), McJukebox::GetComparatorOutput(TEXT("minecraft:music_disc_13")), 1);
	TestEqual(TEXT("5 reads 15"), McJukebox::GetComparatorOutput(TEXT("minecraft:music_disc_5")), 15);
	TestEqual(TEXT("nothing reads 0"), McJukebox::GetComparatorOutput(NAME_None), 0);
	TestFalse(TEXT("cobblestone is no disc"), McJukebox::IsDisc(TEXT("minecraft:cobblestone")));
	TestFalse(TEXT("nor is a disc fragment"), McJukebox::IsDisc(TEXT("minecraft:disc_fragment_5")));
	FMcSong Thirteen;
	if (TestTrue(TEXT("13 is a disc"), McJukebox::FindSong(TEXT("minecraft:music_disc_13"), Thirteen)))
	{
		TestEqual(TEXT("its title shows as the game says"), McJukebox::GetNowPlaying(Thirteen), FString(TEXT("Now Playing: C418 - 13")));
	}
	// Every disc has a song whose output is 1 to 15.
	int32 Discs = 0;
	for (const FMcItemDef& Def : FMcItemRegistry::Get().GetAll())
	{
		FMcSong Each;
		if (McJukebox::FindSong(Def.Name, Each))
		{
			++Discs;
			TestTrue(*FString::Printf(TEXT("%s reads 1 to 15"), *Def.Name.ToString()), Each.ComparatorOutput >= 1 && Each.ComparatorOutput <= 15);
			TestTrue(*FString::Printf(TEXT("%s plays for a while"), *Def.Name.ToString()), Each.LengthTicks > 1000);
		}
	}
	TestEqual(TEXT("there are 22 discs"), Discs, 22);
	return true;
}

// A hopper puts a disc into a jukebox, which then plays for the length of the song and keeps it; a hopper under it
// takes the disc out, which stops it. Anything but a disc stays out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJukeboxHopperTest, "McClient.McWorld.JukeboxHoppers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJukeboxHopperTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()) || !GEngine)
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AMcWorldActor* Actor = World->SpawnActor<AMcWorldActor>();
	const auto Finish = [&]()
	{
		if (Actor)
		{
			Actor->Destroy();
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	if (Actor && !Actor->HasActorBegunPlay())
	{
		Actor->DispatchBeginPlay();
	}
	UMcBlockUseSubsystem* Use = World->GetSubsystem<UMcBlockUseSubsystem>();
	if (!TestNotNull(TEXT("a world actor"), Actor) || !TestNotNull(TEXT("the block use subsystem"), Use) || !TestNotNull(TEXT("a block world"), Actor ? Actor->GetBlockWorld() : nullptr))
	{
		Finish();
		return false;
	}
	FMcBlockWorld& Blocks = *Actor->GetBlockWorld();
	if (!Blocks.HasChunk(FIntPoint(0, 0)))
	{
		Blocks.AddChunk(Blocks.MakeChunk(FIntPoint(0, 0)));
	}
	FMcChunk* Chunk = Blocks.FindChunk(FIntPoint(0, 0));
	const auto Tick = [&](int32 Ticks)
	{
		for (int32 Step = 0; Step < Ticks; ++Step)
		{
			Use->Tick(0.05f + 1e-4f);
		}
	};
	const auto HasRecord = [&](const FIntVector& Pos)
	{
		return Registry.GetPropertyValue(Blocks.GetBlock(Pos), TEXT("has_record")) == TEXT("true");
	};

	// The subsystem watches the world's blocks from its first tick, which is when hoppers placed after it are noticed.
	Use->Tick(0.0f);
	// A hopper over a jukebox, with a disc and a stone in it.
	const FIntVector Jukebox(2, 64, 2);
	const FIntVector Hopper(2, 65, 2);
	Blocks.SetBlock(Jukebox, Registry.ParseState(TEXT("minecraft:jukebox")).Get(0));
	Blocks.SetBlock(Hopper, Registry.ParseState(TEXT("minecraft:hopper[facing=down]")).Get(0));
	FMcBlockEntity Feed = FMcBlockEntity::MakeContainer(5);
	Feed.Slots[0] = FMcItemStack(TEXT("minecraft:cobblestone"), 1);
	Feed.Slots[1] = FMcItemStack(TEXT("minecraft:music_disc_cat"), 1);
	Chunk->SetBlockEntity(Hopper.X, Hopper.Y, Hopper.Z, Feed);
	TestFalse(TEXT("an empty jukebox is not playing"), Use->IsJukeboxPlaying(*Actor, Jukebox));
	Tick(20);
	TestTrue(TEXT("the disc is in"), HasRecord(Jukebox));
	TestTrue(TEXT("and playing"), Use->IsJukeboxPlaying(*Actor, Jukebox));
	TArray<FMcItemStack> Held;
	UMcBlockUseSubsystem::GetContents(*Actor, Jukebox, Held);
	TestTrue(TEXT("the jukebox holds the disc"), Held.Num() == 1 && Held[0].Item == TEXT("minecraft:music_disc_cat"));
	TArray<FMcItemStack> Rest;
	UMcBlockUseSubsystem::GetContents(*Actor, Hopper, Rest);
	TestTrue(TEXT("the stone is still in the hopper"), Rest.ContainsByPredicate([](const FMcItemStack& Stack) { return Stack.Item == TEXT("minecraft:cobblestone"); }));

	// The song is 185 seconds; then it stops, and the disc stays.
	Tick(3600);
	TestTrue(TEXT("still playing near the end"), Use->IsJukeboxPlaying(*Actor, Jukebox));
	Tick(200);
	TestFalse(TEXT("the song has ended"), Use->IsJukeboxPlaying(*Actor, Jukebox));
	TestTrue(TEXT("with the disc still in"), HasRecord(Jukebox));

	// A hopper under another does not take its disc: the jukebox's signal locks the hopper and the disc stays (reference run
	// lg_juke2: hopper enabled=false, disc still in after 24 ticks).
	const FIntVector Second(6, 65, 2);
	const FIntVector Below(6, 64, 2);
	const FIntVector Chest(6, 63, 2);
	Blocks.SetBlock(Chest, Registry.ParseState(TEXT("minecraft:chest")).Get(0));
	Blocks.SetBlock(Below, Registry.ParseState(TEXT("minecraft:hopper[facing=down]")).Get(0));
	Blocks.SetBlock(Second, Registry.ParseState(TEXT("minecraft:jukebox")).Get(0));
	FMcBlockEntity Disc = FMcBlockEntity::MakeContainer(1);
	Disc.Slots[0] = FMcItemStack(TEXT("minecraft:music_disc_13"), 1);
	Chunk->SetBlockEntity(Second.X, Second.Y, Second.Z, Disc);
	Blocks.SetBlock(Second, Registry.ParseState(TEXT("minecraft:jukebox[has_record=true]")).Get(0));
	Tick(20);
	TestTrue(TEXT("the jukebox still holds its disc"), HasRecord(Second));
	TArray<FMcItemStack> Out;
	UMcBlockUseSubsystem::GetContents(*Actor, Chest, Out);
	TestTrue(TEXT("the chest is empty"), Out.IsEmpty());

	Finish();
	return true;
}

#endif
