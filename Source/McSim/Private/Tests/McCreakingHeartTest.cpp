#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCreakingHeartTest, "McClient.McSim.CreakingHeart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcCreakingHeartTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	for (int32 X = -20; X <= 20; ++X)
	{
		for (int32 Z = -20; Z <= 20; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
		}
	}
	FMcSimulation Sim(World);
	int32 Spawned = 0;
	int32 Released = 0;
	bool bReleasedBroken = false;
	double Distance = -1.0;
	Sim.OnCreakingSpawn = [&](const FIntVector&) { ++Spawned; Distance = 16.0; return true; };
	Sim.GetCreakingDistance = [&](const FIntVector&) { return Distance; };
	Sim.OnCreakingRelease = [&](const FIntVector&, bool bBroken) { ++Released; bReleasedBroken = bBroken; Distance = -1.0; };

	const FIntVector Heart(0, 1, 0);
	const auto HeartState = [&]() { return Registry.GetPropertyValue(World.GetBlock(Heart), TEXT("creaking_heart_state")).ToString(); };
	const auto Run = [&](int32 Ticks) { for (int32 Index = 0; Index < Ticks; ++Index) { Sim.Tick(); } };

	// Alone it is uprooted; between two pale oak logs of its axis it is dormant by day.
	Sim.SetDayTime(1000);
	World.SetBlock(Heart, State(TEXT("minecraft:creaking_heart[axis=z,creaking_heart_state=uprooted,natural=true]")));
	Run(30);
	TestEqual(TEXT("uprooted alone"), HeartState(), FString(TEXT("uprooted")));
	World.SetBlock(Heart + FIntVector(0, 0, -1), State(TEXT("minecraft:pale_oak_log[axis=z]")));
	World.SetBlock(Heart + FIntVector(0, 0, 1), State(TEXT("minecraft:pale_oak_log[axis=z]")));
	Run(30);
	TestEqual(TEXT("dormant by day between logs"), HeartState(), FString(TEXT("dormant")));
	TestEqual(TEXT("no creaking by day"), Spawned, 0);

	// At night it wakes and raises one, once.
	Sim.SetDayTime(13000);
	Run(80);
	TestEqual(TEXT("awake at night"), HeartState(), FString(TEXT("awake")));
	TestEqual(TEXT("one creaking"), Spawned, 1);

	// The comparator beside it reads 15 less floor(16 / 32 * 15) = 8.
	World.SetBlock(Heart + FIntVector(1, 0, 0), State(TEXT("minecraft:comparator[facing=west,mode=compare,powered=false]")));
	World.SetBlock(Heart + FIntVector(2, 0, 0), State(TEXT("minecraft:redstone_wire")));
	Run(10);
	TestEqual(TEXT("the comparator reads the distance"), Sim.GetDustPower(Heart + FIntVector(2, 0, 0)), 8);
	Distance = 4.0;
	Run(20);
	TestEqual(TEXT("closer is stronger: 15 - floor(4/32*15) = 14"), Sim.GetDustPower(Heart + FIntVector(2, 0, 0)), 14);

	// With a creaking out it stays awake even when a log goes.
	World.SetBlock(Heart + FIntVector(0, 0, 1), State(TEXT("minecraft:air")));
	Run(30);
	TestEqual(TEXT("not uprooted while a creaking is out"), HeartState(), FString(TEXT("awake")));
	TestEqual(TEXT("no second one"), Spawned, 1);

	// A hit at night grows resin on a pale oak log, once in 5 seconds.
	const auto CountResin = [&]()
	{
		int32 Count = 0;
		const FMcStateId Clump = Registry.FindDefaultState(TEXT("minecraft:resin_clump")).Get(0);
		const FName ClumpName = Registry.GetBlockOf(Clump).Name;
		for (int32 X = -6; X <= 6; ++X)
			for (int32 Y = 0; Y <= 6; ++Y)
				for (int32 Z = -6; Z <= 6; ++Z)
				{
					const FMcStateId At = World.GetBlock(Heart + FIntVector(X, Y, Z));
					Count += Registry.IsValidState(At) && Registry.GetBlockOf(At).Name == ClumpName;
				}
		return Count;
	};
	TestEqual(TEXT("no resin yet"), CountResin(), 0);
	Sim.CreakingHit(Heart);
	TestEqual(TEXT("a clump grew"), CountResin(), 1);
	Sim.CreakingHit(Heart);
	TestEqual(TEXT("not again within 5 seconds"), CountResin(), 1);
	Run(110);
	Sim.CreakingHit(Heart);
	TestEqual(TEXT("again after"), CountResin(), 2);

	// At daybreak it lets the creaking go and sleeps.
	Sim.SetDayTime(1000);
	Run(40);
	TestTrue(TEXT("released at day"), Released >= 1 && !bReleasedBroken);
	TestEqual(TEXT("uprooted: the log it lost is still missing"), HeartState(), FString(TEXT("uprooted")));

	// Broken with a creaking out: it is told.
	Sim.SetDayTime(13000);
	World.SetBlock(Heart + FIntVector(0, 0, 1), State(TEXT("minecraft:pale_oak_log[axis=z]")));
	Run(80);
	TestTrue(TEXT("a second creaking by the next night"), Spawned >= 2);
	const int32 Before = Released;
	World.SetBlock(Heart, State(TEXT("minecraft:air")));
	TestEqual(TEXT("told when broken"), Released, Before + 1);
	TestTrue(TEXT("and it was a break"), bReleasedBroken);
	return true;
}

#endif
