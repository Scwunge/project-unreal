#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McConduit.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcConduitTest, "McClient.McSim.Conduit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcConduitTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Water = State(TEXT("minecraft:water"));
	const FMcStateId Prismarine = State(TEXT("minecraft:prismarine"));
	const FMcStateId Stone = State(TEXT("minecraft:stone"));

	TestEqual(TEXT("16 frame blocks reach 32"), McConduit::GetRange(16), 32);
	TestEqual(TEXT("20 still 32"), McConduit::GetRange(20), 32);
	TestEqual(TEXT("21 reach 48"), McConduit::GetRange(21), 48);
	TestEqual(TEXT("the whole frame 96"), McConduit::GetRange(42), 96);

	// A conduit in water at 8 10 8 with a ring of 16 in the x-y plane: checked the tick after it is set, then every
	// 40 ticks, as vanilla (conduit_log, conduit_rain).
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	const FIntVector Pos(8, 10, 8);
	for (int32 X = 4; X <= 12; ++X)
	{
		for (int32 Y = 6; Y <= 14; ++Y)
		{
			for (int32 Z = 4; Z <= 12; ++Z)
			{
				World.SetBlock(FIntVector(X, Y, Z), Water);
			}
		}
	}
	for (const FIntVector& Place : McConduit::GetFramePlaces())
	{
		if (Place.Z == 0)
		{
			World.SetBlock(Pos + Place, Prismarine);
		}
	}
	FMcSimulation Sim(World);
	TArray<int64> Pulses;
	int32 Range = 0;
	bool bHunting = true;
	Sim.OnConduitPulse = [&](const FIntVector& At, int32 InRange, bool bInHunting)
	{
		TestEqual(TEXT("from the conduit"), At, Pos);
		Pulses.Add(Sim.GetGameTime());
		Range = InRange;
		bHunting = bInHunting;
	};
	World.SetBlock(Pos, State(TEXT("minecraft:conduit[waterlogged=true]")));
	const int64 Placed = Sim.GetGameTime();
	for (int32 Tick = 0; Tick < 125; ++Tick)
	{
		Sim.Tick();
	}
	if (TestEqual(TEXT("four pulses in 125 ticks"), Pulses.Num(), 4))
	{
		TestEqual(TEXT("the tick after it is set"), Pulses[0], Placed + 1);
		TestEqual(TEXT("then 40 on"), Pulses[1], Placed + 41);
		TestEqual(TEXT("and 40 more"), Pulses[3], Placed + 121);
	}
	TestEqual(TEXT("reach 32"), Range, 32);
	TestFalse(TEXT("not hunting with 16"), bHunting);

	// A frame block broken: it sleeps, no pulses; the whole frame: it hunts, reaching 96.
	Pulses.Reset();
	World.SetBlock(Pos + FIntVector(2, 2, 0), Stone);
	for (int32 Tick = 0; Tick < 80; ++Tick)
	{
		Sim.Tick();
	}
	TestEqual(TEXT("asleep with 15"), Pulses.Num(), 0);
	for (const FIntVector& Place : McConduit::GetFramePlaces())
	{
		World.SetBlock(Pos + Place, Prismarine);
	}
	for (int32 Tick = 0; Tick < 41; ++Tick)
	{
		Sim.Tick();
	}
	TestTrue(TEXT("awake again"), Pulses.Num() >= 1);
	TestEqual(TEXT("reach 96"), Range, 96);
	TestTrue(TEXT("hunting with 42"), bHunting);
	return true;
}

#endif
