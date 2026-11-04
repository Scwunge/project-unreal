#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McConduitHunt.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcConduitHuntTest, "McClient.McEntity.ConduitHunt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcConduitHuntTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registry loads"), FMcBlockRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	// As vanilla's conduit_hunt run: a conduit at 0 0 0, zombies (wet unless said) along x with their feet 1.5 below
	// its middle, as the drowned stood.
	FMcBlockWorld World{FMcDimensionType()};
	FMcEntityManager Manager(World);
	TSet<int32> Dry;
	const auto Add = [&](const TCHAR* Kind, double X, double Z = 0.5, bool bWet = true)
	{
		FMcMob* Mob = new FMcMob(*FMcMobType::Find(Kind));
		Mob->Body.Position = FVector(X, -1.0, Z);
		const int32 Id = Manager.Add(TUniquePtr<FMcEntity>(Mob)).Id;
		if (!bWet)
		{
			Dry.Add(Id);
		}
		return Mob;
	};
	const auto IsWet = [&](const FMcEntity& Mob) { return !Dry.Contains(Mob.Id); };
	const FIntVector Pos(0, 0, 0);

	// Out of reach (9 blocks), dry, or not hostile: never hurt.
	FMcMob* Far = Add(TEXT("minecraft:zombie"), 9.5);
	FMcMob* DryOne = Add(TEXT("minecraft:zombie"), 0.5, 3.5, false);
	FMcMob* Pig = Add(TEXT("minecraft:pig"), 3.5);
	TestEqual(TEXT("nothing to hunt"), McConduitHunt::Pulse(Manager, Pos, 0, IsWet), 0);

	// One at 8 blocks is picked and hit, then dropped on the next check without a hit (8.1 from the middle).
	FMcMob* Edge = Add(TEXT("minecraft:zombie"), 8.5);
	int32 Target = McConduitHunt::Pulse(Manager, Pos, 0, IsWet);
	TestEqual(TEXT("picks the one at 8"), Target, Edge->Id);
	TestEqual(TEXT("and hits it for 4"), Edge->Health, Edge->Type.MaxHealth - 4.0f);
	Edge->HurtTime = 0;
	Target = McConduitHunt::Pulse(Manager, Pos, Target, IsWet);
	TestEqual(TEXT("drops it on the next check"), Target, 0);
	TestEqual(TEXT("without a hit"), Edge->Health, Edge->Type.MaxHealth - 4.0f);
	Edge->bRemoved = true;

	// One at 7 blocks is kept and hit every check until it dies; the check after drops it.
	FMcMob* Near = Add(TEXT("minecraft:zombie"), 7.5);
	Target = McConduitHunt::Pulse(Manager, Pos, 0, IsWet);
	TestEqual(TEXT("picks the one at 7"), Target, Near->Id);
	int32 Checks = 1;
	while (Near->Health > 0.0f && Checks < 10)
	{
		Near->HurtTime = 0;
		Target = McConduitHunt::Pulse(Manager, Pos, Target, IsWet);
		++Checks;
	}
	TestEqual(TEXT("dead after five hits"), Checks, 5);
	TestEqual(TEXT("dropped once dead"), McConduitHunt::Pulse(Manager, Pos, Target, IsWet), 0);
	TestEqual(TEXT("the far one untouched"), Far->Health, Far->Type.MaxHealth);
	TestEqual(TEXT("the dry one untouched"), DryOne->Health, DryOne->Type.MaxHealth);
	TestEqual(TEXT("the pig untouched"), Pig->Health, Pig->Type.MaxHealth);
	return true;
}

#endif
