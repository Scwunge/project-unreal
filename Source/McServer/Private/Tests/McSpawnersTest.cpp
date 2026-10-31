// Monster spawners: the wait counts down only with a player within 16 blocks, the first wait is 20 ticks, a cycle
// puts out mobs around it and starts a wait of 200 to 799, and 6 of the mob near it stop a cycle (spawner_log).

#include "McBlockEntity.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McItemRegistry.h"
#include "McMob.h"
#include "McSpawners.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSpawnersTest, "McClient.McServer.Spawners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSpawnersTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const FIntVector Pos(8, 2, 8);
	const FName Pig(TEXT("minecraft:pig"));
	// A stone floor at y 0 and a pig spawner on it at 8 2 8 (pigs need no dark).
	const auto MakeWorld = [&](FMcBlockWorld& World)
	{
		TUniquePtr<FMcChunk> Chunk = World.MakeChunk(FIntPoint(0, 0));
		for (int32 X = 0; X < 16; ++X)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				Chunk->SetBlock(X, 0, Z, Registry.ParseState(TEXT("minecraft:stone")).Get(0));
			}
		}
		Chunk->SetBlock(Pos.X, Pos.Y, Pos.Z, Registry.ParseState(TEXT("minecraft:spawner")).Get(0));
		Chunk->SetBlockEntity(Pos.X, Pos.Y, Pos.Z, FMcBlockEntity::MakeSpawner(Pig));
		World.AddChunk(MoveTemp(Chunk));
	};
	const auto CountPigs = [&](FMcEntityHost& Host)
	{
		int32 Count = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			Count += !Entity->bRemoved && Entity->GetType() == Pig ? 1 : 0;
		}
		return Count;
	};

	{
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcEntityHost Host(World, 1);
		Host.OnChunkAdded(FIntPoint(0, 0));
		FMcSpawners& Spawners = Host.GetSpawners();
		TestEqual(TEXT("found as its chunk loads"), Spawners.Num(), 1);
		TestEqual(TEXT("a first wait of 20"), Spawners.GetDelay(Pos), 20);
		const TArray<FVector> Far = { FVector(8.5, 1.0, 25.5) };
		for (int32 Tick = 0; Tick < 30; ++Tick)
		{
			Host.Tick(Far, 6000.0);
		}
		TestEqual(TEXT("no countdown with the player 17 off"), Spawners.GetDelay(Pos), 20);
		const TArray<FVector> Near = { FVector(8.5, 1.0, 14.5) };
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			Host.Tick(Near, 6000.0);
		}
		TestEqual(TEXT("counted down"), Spawners.GetDelay(Pos), 0);
		TestEqual(TEXT("nothing yet"), CountPigs(Host), 0);
		Host.Tick(Near, 6000.0);
		const int32 Pigs = CountPigs(Host);
		TestTrue(TEXT("one to four come the tick after"), Pigs >= 1 && Pigs <= 4);
		TestTrue(TEXT("and the next wait is 200 to 799"), Spawners.GetDelay(Pos) >= 200 && Spawners.GetDelay(Pos) <= 799);
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (Entity->GetType() == Pig)
			{
				const FVector& Feet = Entity->Body.Position;
				TestTrue(TEXT("within 4 across"), FMath::Abs(Feet.X - 8.5) <= 4.0 && FMath::Abs(Feet.Z - 8.5) <= 4.0);
				TestTrue(TEXT("a layer below to a layer above"), Feet.Y >= 0.9 && Feet.Y <= 3.0);
			}
		}
	}

	// Six pigs about it already: the cycle puts out none and the wait starts again.
	{
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcEntityHost Host(World, 2);
		Host.OnChunkAdded(FIntPoint(0, 0));
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Host.SpawnMob(Pig, FVector(6.5 + Index * 0.6, 1.0, 6.5));
		}
		const TArray<FVector> Near = { FVector(8.5, 1.0, 14.5) };
		for (int32 Tick = 0; Tick < 21; ++Tick)
		{
			Host.Tick(Near, 6000.0);
		}
		TestEqual(TEXT("still six"), CountPigs(Host), 6);
		TestTrue(TEXT("the wait started again"), Host.GetSpawners().GetDelay(Pos) >= 200);
	}

	// Broken: forgotten.
	{
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcEntityHost Host(World, 3);
		Host.OnChunkAdded(FIntPoint(0, 0));
		World.SetBlock(Pos, Registry.ParseState(TEXT("minecraft:air")).Get(0));
		Host.Tick({ FVector(8.5, 1.0, 14.5) }, 6000.0);
		TestEqual(TEXT("a broken spawner is dropped"), Host.GetSpawners().Num(), 0);
	}
	return true;
}

#endif
