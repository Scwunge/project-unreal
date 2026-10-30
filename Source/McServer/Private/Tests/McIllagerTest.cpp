#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "McVillager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcIllagerTest, "McClient.McServer.Illagers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcIllagerTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto MakeFloor = [&](FMcBlockWorld& World)
	{
		for (int32 ChunkX = 0; ChunkX < 2; ++ChunkX)
		{
			for (int32 ChunkZ = 0; ChunkZ < 2; ++ChunkZ)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(ChunkX, ChunkZ)));
			}
		}
		for (int32 X = 0; X < 32; ++X)
		{
			for (int32 Z = 0; Z < 32; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
			}
		}
	};
	for (const TCHAR* Kind : { TEXT("minecraft:pillager"), TEXT("minecraft:vindicator"), TEXT("minecraft:evoker"), TEXT("minecraft:vex"), TEXT("minecraft:ravager"), TEXT("minecraft:witch") })
	{
		const FMcMobType* Type = FMcMobType::Find(Kind);
		if (TestNotNull(*FString::Printf(TEXT("%s is a mob"), Kind), Type))
		{
			TestTrue(*FString::Printf(TEXT("%s counts as a monster"), Kind), Type->IsHostile());
		}
	}
	{
		// A vindicator's axe kills a villager that can't run in a few swings, and it does not rise as a zombie.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		FMcGameRules Rules;
		Rules.Difficulty = 3;
		Host.SetRules(Rules);
		Host.SpawnMob(TEXT("minecraft:vindicator"), FVector(8.0, 1.0, 8.5));
		FMcMob* Villager = Host.SpawnMob(TEXT("minecraft:villager"), FVector(9.0, 1.0, 8.5));
		if (!Villager)
		{
			return false;
		}
		Villager->bFrozen = true;
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			Villager->HurtTime = 0;
			Host.GetManager().Tick();
		}
		TestTrue(TEXT("the villager is dead"), Villager->Health <= 0.0f);
		int32 ZombieVillagers = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			ZombieVillagers += (!Entity->bRemoved && Entity->GetType() == FName(TEXT("minecraft:zombie_villager"))) ? 1 : 0;
		}
		TestEqual(TEXT("no zombie villager rises"), ZombieVillagers, 0);
	}
	{
		// An evoker sends fangs along the ground at a villager and summons vexes; the fangs bite.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 2);
		Host.SpawnMob(TEXT("minecraft:evoker"), FVector(8.5, 1.0, 8.5));
		FMcMob* Villager = Host.SpawnMob(TEXT("minecraft:villager"), FVector(14.5, 1.0, 8.5));
		if (!Villager)
		{
			return false;
		}
		Villager->bFrozen = true;
		int32 MostFangs = 0;
		int32 MostVexes = 0;
		for (int32 Tick = 0; Tick < 1200; ++Tick)
		{
			Villager->HurtTime = 0;
			Host.GetManager().Tick();
			int32 Fangs = 0;
			int32 Vexes = 0;
			for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
			{
				Fangs += (!Entity->bRemoved && Entity->GetType() == FName(TEXT("minecraft:evoker_fangs"))) ? 1 : 0;
				Vexes += (!Entity->bRemoved && Entity->GetType() == FName(TEXT("minecraft:vex"))) ? 1 : 0;
			}
			MostFangs = FMath::Max(MostFangs, Fangs);
			MostVexes = FMath::Max(MostVexes, Vexes);
		}
		TestTrue(TEXT("fangs come up"), MostFangs > 0);
		TestTrue(TEXT("vexes are summoned"), MostVexes >= 3);
		TestTrue(TEXT("the villager was hurt"), Villager->Health < Villager->Type.MaxHealth);
	}
	return true;
}

#endif
