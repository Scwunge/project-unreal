#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "McStructureSpawns.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcStructureSpawnTest, "McClient.McServer.StructureSpawns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcStructureSpawnTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 ChunkX = 0; ChunkX < 8; ++ChunkX)
	{
		for (int32 ChunkZ = 0; ChunkZ < 8; ++ChunkZ)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(ChunkX, ChunkZ)));
		}
	}
	for (int32 X = 0; X < 128; ++X)
	{
		for (int32 Z = 0; Z < 128; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
		}
	}
	FMcStructureSpawns& Spawns = FMcStructureSpawns::Get();
	Spawns.SetRecording(true);
	FMcStructureZone Outpost;
	Outpost.Structure = TEXT("minecraft:pillager_outpost");
	Outpost.Min = FIntVector(90, 0, 90);
	Outpost.Max = FIntVector(105, 12, 105);
	FMcSpawnEntry Pillager;
	Pillager.Kind = TEXT("minecraft:pillager");
	Pillager.Weight = 1;
	Outpost.Monsters.Add(Pillager);
	Spawns.AddZone(Outpost);
	Spawns.AddZone(Outpost);
	FMcStructureZone Found;
	TestTrue(TEXT("the zone is found inside"), Spawns.FindMonsterZone(FIntVector(95, 3, 95), Found));
	TestFalse(TEXT("and not outside"), Spawns.FindMonsterZone(FIntVector(60, 3, 60), Found));
	// A chunk's save carries the zones that reach it, once, and loading it brings them back.
	const TArray<uint8> Saved = Spawns.EncodeChunk(FIntPoint(5, 5));
	TestTrue(TEXT("the chunk under the outpost has it"), !Saved.IsEmpty());
	TestTrue(TEXT("a chunk away from it has none"), Spawns.EncodeChunk(FIntPoint(0, 0)).IsEmpty());
	Spawns.Clear();
	TestFalse(TEXT("forgotten"), Spawns.FindMonsterZone(FIntVector(95, 3, 95), Found));
	Spawns.DecodeChunk(Saved);
	Spawns.DecodeChunk(Saved);
	TestTrue(TEXT("and back with the chunk"), Spawns.FindMonsterZone(FIntVector(95, 3, 95), Found) && Found.Monsters.Num() == 1 && Found.Monsters[0].Kind == FName(TEXT("minecraft:pillager")));
	TestTrue(TEXT("not twice"), Spawns.EncodeChunk(FIntPoint(5, 5)) == Saved);

	FMcStructureMob Witch;
	Witch.Kind = TEXT("minecraft:witch");
	Witch.Position = FVector(120.5, 1.0, 120.5);
	Spawns.AddMob(TEXT("hut"), Witch);
	Spawns.AddMob(TEXT("hut"), Witch);

	FMcEntityHost Host(World, 1);
	const TArray<FVector> Players = { FVector(5.5, 1.0, 5.5) };
	// A player 25 to 48 blocks from the outpost's middle has the ring cross it now and then.
	const TArray<FVector> Near2 = { FVector(64.5, 1.0, 98.5) };
	for (int32 Tick = 0; Tick < 60; ++Tick)
	{
		Host.Tick(Players, 6000.0);
	}
	int32 Witches = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
	{
		if (Entity->GetType() == FName(TEXT("minecraft:witch")))
		{
			++Witches;
			TestTrue(TEXT("the hut's witch stays"), static_cast<FMcMob&>(*Entity).bPersistent);
		}
	}
	TestEqual(TEXT("one witch, though it was added twice"), Witches, 1);
	for (int32 Tick = 0; Tick < 200; ++Tick)
	{
		Host.Tick(Players, 6000.0);
	}
	bool bWitchLives = false;
	for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
	{
		bWitchLives |= Entity->GetType() == FName(TEXT("minecraft:witch")) && !Entity->bRemoved;
	}
	TestTrue(TEXT("alive after the far-mob sweep"), bWitchLives);

	// Pillagers come where the ordinary spawning ring falls inside the outpost: a player right beside it.
	for (int32 Tick = 0; Tick < 6000; ++Tick)
	{
		Host.Tick(Near2, 6000.0);
	}
	int32 Pillagers = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
	{
		if (Entity->GetType() == FName(TEXT("minecraft:pillager")) && !Entity->bRemoved)
		{
			++Pillagers;
		}
	}
	TestTrue(TEXT("some came"), Pillagers >= 1);
	Spawns.SetRecording(false);
	return true;
}

#endif
