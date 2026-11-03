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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVillagerInfectionTest, "McClient.McServer.VillagerInfection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcVillagerInfectionTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	// A zombie next to a villager that can't run: on Hard the villager rises as a zombie villager, on Easy it never rises.
	const auto Run = [&](int32 Difficulty, int32& OutZombieVillagers, int32& OutVillagers)
	{
		FMcBlockWorld World{FMcDimensionType()};
		World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
		for (int32 X = 0; X < 16; ++X)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
			}
		}
		FMcEntityHost Host(World, 1);
		FMcGameRules Rules;
		Rules.Difficulty = Difficulty;
		Host.SetRules(Rules);
		FMcMob* Zombie = Host.SpawnMob(TEXT("minecraft:zombie"), FVector(6.5, 1.0, 6.5));
		FMcMob* Villager = Host.SpawnMob(TEXT("minecraft:villager"), FVector(8.0, 1.0, 6.5));
		if (!Zombie || !Villager)
		{
			return false;
		}
		Villager->bFrozen = true;
		for (int32 Tick = 0; Tick < 600; ++Tick)
		{
			Villager->HurtTime = 0;
			Host.GetManager().Tick();
		}
		OutZombieVillagers = 0;
		OutVillagers = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (!Entity->bRemoved)
			{
				OutZombieVillagers += Entity->GetType() == FMcZombieVillager::StaticType() ? 1 : 0;
				OutVillagers += Entity->GetType() == FMcVillager::StaticType() ? 1 : 0;
			}
		}
		return true;
	};
	int32 ZombieVillagers = 0;
	int32 Villagers = 0;
	if (TestTrue(TEXT("the hard run runs"), Run(3, ZombieVillagers, Villagers)))
	{
		TestEqual(TEXT("on Hard the villager is gone"), Villagers, 0);
		TestEqual(TEXT("and a zombie villager stands in its place"), ZombieVillagers, 1);
	}
	if (TestTrue(TEXT("the easy run runs"), Run(1, ZombieVillagers, Villagers)))
	{
		TestEqual(TEXT("on Easy the villager is gone"), Villagers, 0);
		TestEqual(TEXT("and no zombie villager rises"), ZombieVillagers, 0);
	}
	return true;
}

#endif
