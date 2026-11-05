#include "HAL/PlatformTime.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McVillager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tick cost of a busy village: 48 villagers with beds, job blocks and a bell, a golem and a raid's worth of illagers. Logs milliseconds per tick for
// each part of the day; the game's budget is 50 ms a tick, the test only fails above a tenth of that so a regression stands out.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVillageTickCostTest, "McClient.McEntity.VillageTickCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcVillageTickCostTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{ FMcDimensionType() };
	for (int32 Z = -2; Z <= 2; ++Z)
	{
		for (int32 X = -2; X <= 2; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	for (int32 X = -32; X <= 47; ++X)
	{
		for (int32 Z = -32; Z <= 47; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:grass_block")));
		}
	}
	// Houses are skipped: a bed every 3 blocks along two rows, a job block every 5 along another, one bell.
	for (int32 Index = 0; Index < 24; ++Index)
	{
		World.SetBlock(FIntVector(-20 + Index * 2, 1, -10), State(TEXT("minecraft:red_bed")));
	}
	const TCHAR* const Jobs[4] = { TEXT("minecraft:composter"), TEXT("minecraft:lectern"), TEXT("minecraft:blast_furnace"), TEXT("minecraft:smithing_table") };
	for (int32 Index = 0; Index < 12; ++Index)
	{
		World.SetBlock(FIntVector(-20 + Index * 3, 1, 10), State(Jobs[Index % 4]));
	}
	World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:bell")));
	FMcEntityManager Manager(World);
	for (int32 Index = 0; Index < 48; ++Index)
	{
		FMcVillager* Villager = new FMcVillager(*FMcMobType::Find(FMcVillager::StaticType()));
		Villager->Body.Position = FVector(-12.0 + (Index % 8) * 3.0 + 0.5, 1.0, -8.0 + (Index / 8) * 3.0 + 0.5);
		Manager.Add(TUniquePtr<FMcEntity>(Villager));
	}
	const TCHAR* const Raiders[3] = { TEXT("minecraft:pillager"), TEXT("minecraft:vindicator"), TEXT("minecraft:ravager") };
	for (int32 Index = 0; Index < 24; ++Index)
	{
		FMcMob* Mob = new FMcMob(*FMcMobType::Find(Raiders[Index % 3]));
		Mob->Body.Position = FVector(-30.0 + Index * 2.0 + 0.5, 1.0, -28.0);
		Manager.Add(TUniquePtr<FMcEntity>(Mob));
	}
	struct FPhase { const TCHAR* Name; int32 Time; };
	const FPhase Phases[4] = { { TEXT("work"), 3000 }, { TEXT("meet"), 10000 }, { TEXT("evening"), 11500 }, { TEXT("night"), 13000 } };
	for (const FPhase& Phase : Phases)
	{
		Manager.TimeOfDay = Phase.Time;
		for (int32 Warm = 0; Warm < 100; ++Warm)
		{
			Manager.Tick();
		}
		double Total = 0.0;
		double Worst = 0.0;
		const int32 Ticks = 400;
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			const double Start = FPlatformTime::Seconds();
			Manager.Tick();
			const double Took = (FPlatformTime::Seconds() - Start) * 1000.0;
			Total += Took;
			Worst = FMath::Max(Worst, Took);
		}
		AddInfo(FString::Printf(TEXT("village tick cost, %s: average %.3f ms, worst %.3f ms over %d ticks (%d entities)"), Phase.Name, Total / Ticks, Worst, Ticks, Manager.GetEntities().Num()));
		TestTrue(*FString::Printf(TEXT("%s stays far inside the 50 ms tick"), Phase.Name), Total / Ticks < 5.0);
	}
	return true;
}

#endif
