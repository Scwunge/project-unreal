// The game rules and difficulty the entity host obeys: peaceful, spawn_mobs, mob_drops and block_drops.

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McGameRules.h"
#include "McItemRegistry.h"
#include "McMob.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void FillStone(FMcBlockWorld& World, FMcBlockRegistry& Registry, int32 Height)
	{
		const FMcStateId Stone = Registry.ParseState(TEXT("minecraft:stone")).Get(0);
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		for (int32 X = -16; X <= 31; ++X)
		{
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 Z = -16; Z <= 31; ++Z)
				{
					World.SetBlock(FIntVector(X, Y, Z), Stone);
				}
			}
		}
	}

	int32 CountItems(FMcEntityHost& Host)
	{
		int32 Count = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			Count += Entity->GetType() == FMcItemEntity::StaticType() && !Entity->bRemoved ? 1 : 0;
		}
		return Count;
	}

	int32 CountMobs(FMcEntityHost& Host)
	{
		int32 Count = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			Count += FMcMobType::Find(Entity->GetType()) != nullptr && !Entity->bRemoved ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGameRulesHostTest, "McClient.McServer.GameRulesHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGameRulesHostTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}

	{
		// Peaceful takes the monsters away and keeps the animals; going back to normal doesn't bring them back.
		FMcBlockWorld World{FMcDimensionType()};
		FillStone(World, Registry, 1);
		FMcEntityHost Host(World, 1);
		Host.SpawnMob(TEXT("minecraft:zombie"), FVector(8.5, 1.0, 8.5));
		Host.SpawnMob(TEXT("minecraft:creeper"), FVector(9.5, 1.0, 8.5));
		FMcMob* Cow = Host.SpawnMob(TEXT("minecraft:cow"), FVector(10.5, 1.0, 8.5));
		TestEqual(TEXT("two monsters"), Host.NumHostile(), 2);
		FMcGameRules Rules;
		Rules.Difficulty = 0;
		Host.SetRules(Rules);
		TestEqual(TEXT("no monsters on peaceful"), Host.NumHostile(), 0);
		TestTrue(TEXT("the cow stays"), Cow && !Cow->bRemoved);
		const TArray<FVector> Player = { FVector(8.5, 1.0, 8.5) };
		for (int32 Tick = 0; Tick < 2000; ++Tick)
		{
			Host.Tick(Player, 18000.0);
		}
		TestEqual(TEXT("and none spawn at night"), Host.NumHostile(), 0);
	}

	{
		// spawn_mobs off: a peaceful or normal night brings nothing, and the same goes for a chunk's herd.
		FMcBlockWorld World{FMcDimensionType()};
		FillStone(World, Registry, 1);
		FMcEntityHost Host(World, 2);
		FMcGameRules Rules;
		Rules.bDoMobSpawning = false;
		Host.SetRules(Rules);
		const TArray<FVector> Player = { FVector(8.5, 1.0, 8.5) };
		for (int32 Tick = 0; Tick < 2000; ++Tick)
		{
			Host.Tick(Player, 18000.0);
		}
		TestEqual(TEXT("no mobs spawn by themselves"), CountMobs(Host), 0);
	}

	{
		// mob_drops off: a cow always leaves beef, unless the rule is off.
		for (const bool bLoot : { true, false })
		{
			FMcBlockWorld World{FMcDimensionType()};
			FillStone(World, Registry, 1);
			FMcEntityHost Host(World, 3);
			FMcGameRules Rules;
			Rules.bDoMobLoot = bLoot;
			Host.SetRules(Rules);
			for (int32 Index = 0; Index < 6; ++Index)
			{
				if (FMcMob* Cow = Host.SpawnMob(TEXT("minecraft:cow"), FVector(8.5 + Index, 1.0, 8.5)))
				{
					Cow->Damage(1000.0f, nullptr);
				}
			}
			if (bLoot)
			{
				TestTrue(TEXT("cows leave drops"), CountItems(Host) > 0);
			}
			else
			{
				TestEqual(TEXT("mob_drops off, no drops"), CountItems(Host), 0);
			}
		}
	}

	{
		// block_drops off: an explosion in a stone floor leaves cobblestone, unless the rule is off.
		for (const bool bDrops : { true, false })
		{
			FMcBlockWorld World{FMcDimensionType()};
			FillStone(World, Registry, 4);
			FMcEntityHost Host(World, 4);
			FMcGameRules Rules;
			Rules.bDoTileDrops = bDrops;
			Host.SetRules(Rules);
			Host.GetManager().Explode(FVector(8.5, 4.0625, 8.5), 3.0f);
			if (bDrops)
			{
				TestTrue(TEXT("the crater leaves cobblestone"), CountItems(Host) > 0);
			}
			else
			{
				TestEqual(TEXT("block_drops off, nothing drops"), CountItems(Host), 0);
			}
		}
	}
	return true;
}

#endif
