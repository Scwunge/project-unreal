#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "Misc/AutomationTest.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEntityHostAttackTest, "McClient.McServer.PlayerAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcEntityHostAttackTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
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
	FMcMob* Pig = new FMcMob(*FMcMobType::Find(TEXT("minecraft:pig")));
	Pig->Body.Position = FVector(8.5, 1.0, 8.5);
	Host.GetManager().Add(TUniquePtr<FMcEntity>(Pig));
	const float Before = Pig->Health;

	// Looking at it from two blocks off hits it; looking away, or from too far, doesn't.
	const FVector Eye(8.5, 1.62, 6.5);
	TestFalse(TEXT("a miss"), Host.Attack(Eye, FVector(1, 0, 0), 3.0f, 1.0f));
	TestFalse(TEXT("out of reach"), Host.Attack(FVector(8.5, 1.62, 2.5), FVector(0, 0, 1), 3.0f, 1.0f));
	TestEqual(TEXT("untouched"), Pig->Health, Before);
	TestTrue(TEXT("a hit"), Host.Attack(Eye, FVector(0, -0.2, 1), 3.0f, 1.0f));
	TestTrue(TEXT("it took damage"), Pig->Health < Before);
	TestEqual(TEXT("and is flashing"), Pig->HurtTime, 10);
	TestEqual(TEXT("and the hit is counted"), Pig->HurtEvents, 1);

	// A direction that isn't one (it comes from the client) hits nothing.
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("an empty direction"), Host.Attack(Eye, FVector::ZeroVector, 3.0f, 1.0f));
	TestFalse(TEXT("a NaN direction"), Host.Attack(Eye, FVector(NaN, 0.0, 1.0), 3.0f, 1.0f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEntityHostPlayersTest, "McClient.McServer.MobsWithoutPlayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcEntityHostPlayersTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
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
	const auto AddMob = [&Host](const TCHAR* Kind, const FVector& Pos)
	{
		FMcMob* Mob = new FMcMob(*FMcMobType::Find(Kind));
		Mob->Body.Position = Pos;
		Host.GetManager().Add(TUniquePtr<FMcEntity>(Mob));
		return Mob;
	};
	const auto Count = [&Host]() { return Host.GetManager().GetEntities().Num(); };
	// Night, so nothing burns.
	const double Night = 18000.0;

	AddMob(TEXT("minecraft:zombie"), FVector(8.5, 1.0, 8.5));
	// Several despawn checks (every 40 ticks) pass with nobody in the world.
	for (int32 Tick = 0; Tick < 130; ++Tick)
	{
		Host.Tick({}, Night);
	}
	TestEqual(TEXT("a hostile mob stays while there are no players"), Count(), 1);

	// With a player far away it goes.
	for (int32 Tick = 0; Tick < 40; ++Tick)
	{
		Host.Tick({ FVector(1000.0, 1.0, 1000.0) }, Night);
	}
	TestEqual(TEXT("and is removed once someone is far from it"), Count(), 0);

	// The mobs in a chunk leave with it; those elsewhere stay.
	AddMob(TEXT("minecraft:pig"), FVector(8.5, 1.0, 8.5));
	AddMob(TEXT("minecraft:pig"), FVector(20.5, 1.0, 8.5));
	Host.OnChunkRemoved(FIntPoint(0, 0));
	Host.Tick({}, Night);
	TestEqual(TEXT("mobs go when their chunk unloads"), Count(), 1);

	return true;
}

#endif
