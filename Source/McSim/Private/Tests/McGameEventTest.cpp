#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McGameEvent.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGameEventTest, "McClient.McSim.GameEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGameEventTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));

	// The wiki's frequencies.
	TestEqual(TEXT("step"), McGameEvents::FrequencyOf(TEXT("minecraft:step")), 1);
	TestEqual(TEXT("hit ground"), McGameEvents::FrequencyOf(TEXT("hit_ground")), 2);
	TestEqual(TEXT("projectile shoot"), McGameEvents::FrequencyOf(TEXT("projectile_shoot")), 3);
	TestEqual(TEXT("equip"), McGameEvents::FrequencyOf(TEXT("equip")), 5);
	TestEqual(TEXT("damage"), McGameEvents::FrequencyOf(TEXT("entity_damage")), 7);
	TestEqual(TEXT("eat"), McGameEvents::FrequencyOf(TEXT("eat")), 8);
	TestEqual(TEXT("door open"), McGameEvents::FrequencyOf(TEXT("block_open")), 10);
	TestEqual(TEXT("place"), McGameEvents::FrequencyOf(TEXT("block_place")), 13);
	TestEqual(TEXT("explode"), McGameEvents::FrequencyOf(TEXT("explode")), 15);
	TestEqual(TEXT("resonate 6"), McGameEvents::FrequencyOf(TEXT("resonate_6")), 6);
	TestEqual(TEXT("unknown"), McGameEvents::FrequencyOf(TEXT("nonsense")), 0);

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
			World.SetBlock(FIntVector(X, 0, Z), Stone);
		}
	}
	FMcSimulation Sim(World);
	const FIntVector Sensor(0, 1, 0);
	World.SetBlock(Sensor, State(TEXT("minecraft:sculk_sensor[sculk_sensor_phase=inactive,power=0,waterlogged=false]")));
	for (int32 Index = 0; Index < 12; ++Index) { Sim.Tick(); }

	const auto PowerOf = [&]() { return Registry.GetPropertyValue(World.GetBlock(Sensor), TEXT("power")).ToString(); };
	const auto Settle = [&]() { for (int32 Index = 0; Index < 60; ++Index) { Sim.Tick(); } };

	// A step 4 blocks away is heard 4 ticks later with strength 15 - floor(15/8*4) = 8.
	Sim.EmitGameEvent(TEXT("step"), FIntVector(4, 1, 0));
	for (int32 Index = 0; Index < 3; ++Index) { Sim.Tick(); }
	TestEqual(TEXT("not yet"), PowerOf(), FString(TEXT("0")));
	Sim.Tick();
	TestEqual(TEXT("heard after 4 ticks, strength 8"), PowerOf(), FString(TEXT("8")));
	Settle();

	// A sneaking player is not heard at frequency 1 to 3, but is at 7.
	FMcGameEventSource Sneak;
	Sneak.bPlayer = true;
	Sneak.bSneaking = true;
	Sim.EmitGameEvent(TEXT("step"), FIntVector(2, 1, 0), Sneak);
	Settle();
	TestEqual(TEXT("sneaking step unheard"), PowerOf(), FString(TEXT("0")));
	Sim.EmitGameEvent(TEXT("entity_damage"), FIntVector(2, 1, 0), Sneak);
	for (int32 Index = 0; Index < 2; ++Index) { Sim.Tick(); }
	TestEqual(TEXT("sneaking damage heard, strength 15-floor(15/8*2)=12"), PowerOf(), FString(TEXT("12")));
	Settle();

	// A warden's events are not heard by a sensor.
	FMcGameEventSource Warden;
	Warden.bWarden = true;
	Sim.EmitGameEvent(TEXT("step"), FIntVector(1, 1, 0), Warden);
	Settle();
	TestEqual(TEXT("warden unheard"), PowerOf(), FString(TEXT("0")));

	// Wool in the way stops it.
	World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:white_wool")));
	Sim.EmitGameEvent(TEXT("step"), FIntVector(4, 1, 0));
	Settle();
	TestEqual(TEXT("wool stops it"), PowerOf(), FString(TEXT("0")));
	TestTrue(TEXT("occluded"), Sim.IsVibrationOccluded(FIntVector(4, 1, 0), Sensor));
	World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:air")));

	// Listeners hear events (a warden's hearing).
	int32 Heard = 0;
	const int32 Handle = Sim.AddGameEventListener([&](const FMcGameEvent& Event) { Heard += Event.Frequency; });
	Sim.EmitGameEvent(TEXT("explode"), FIntVector(9, 1, 9));
	TestEqual(TEXT("listener told"), Heard, 15);
	Sim.RemoveGameEventListener(Handle);
	Sim.EmitGameEvent(TEXT("explode"), FIntVector(9, 1, 9));
	TestEqual(TEXT("and not once removed"), Heard, 15);
	FMcGameEvent Near;
	Near.Frequency = 5;
	Near.Pos = FIntVector(15, 0, 0);
	TestTrue(TEXT("warden hears 15 away"), McGameEvents::WardenCanHear(Near, FIntVector::ZeroValue));
	Near.Pos = FIntVector(16, 0, 0);
	TestFalse(TEXT("not 16 away"), McGameEvents::WardenCanHear(Near, FIntVector::ZeroValue));
	Near.Pos = FIntVector(3, 0, 0);
	Near.Source.bCreativeOrSpectator = true;
	TestFalse(TEXT("not a creative player"), McGameEvents::WardenCanHear(Near, FIntVector::ZeroValue));

	// A shrieker next to a sensor shrieks for 90 ticks when a player sets the sensor off, and raises the warning level of a
	// natural one; four activations (10 s apart) ask for a warden.
	const FIntVector Shrieker(6, 1, 0);
	World.SetBlock(Shrieker, State(TEXT("minecraft:sculk_shrieker[can_summon=true,shrieking=false,waterlogged=false]")));
	int32 Summons = 0, Levels = 0;
	Sim.OnWardenSummon = [&](const FIntVector&, int32) { ++Summons; };
	Sim.OnShriek = [&](const FIntVector&, int32, int32 Level) { Levels = Level; };
	FMcGameEventSource Player;
	Player.bPlayer = true;
	Player.PlayerId = 7;
	Sim.EmitGameEvent(TEXT("block_place"), FIntVector(1, 1, 0), Player);
	Sim.Tick(); Sim.Tick();
	TestEqual(TEXT("shrieking"), Registry.GetPropertyValue(World.GetBlock(Shrieker), TEXT("shrieking")).ToString(), FString(TEXT("true")));
	TestEqual(TEXT("warning level 1"), Sim.GetWarningLevel(7), 1);
	TestEqual(TEXT("told of the shriek"), Levels, 1);
	for (int32 Index = 0; Index < 92; ++Index) { Sim.Tick(); }
	TestEqual(TEXT("done shrieking after 90 ticks"), Registry.GetPropertyValue(World.GetBlock(Shrieker), TEXT("shrieking")).ToString(), FString(TEXT("false")));
	TestEqual(TEXT("no warden yet"), Summons, 0);
	// Within the 10 seconds the same player cannot set it off again.
	Sim.ShriekerTouched(Shrieker, 7);
	TestEqual(TEXT("still level 1"), Sim.GetWarningLevel(7), 1);
	for (int32 Round = 0; Round < 3; ++Round)
	{
		for (int32 Index = 0; Index < 120; ++Index) { Sim.Tick(); }
		Sim.ShriekerTouched(Shrieker, 7);
		for (int32 Index = 0; Index < 100; ++Index) { Sim.Tick(); }
	}
	TestEqual(TEXT("level 4"), Sim.GetWarningLevel(7), 4);
	TestEqual(TEXT("a warden is asked for"), Summons, 1);
	// Twelve thousand ticks later the level drops by one.
	for (int32 Index = 0; Index < 12000; ++Index) { Sim.Tick(); }
	TestEqual(TEXT("level 3 after 10 minutes"), Sim.GetWarningLevel(7), 3);

	// A chiseled bookshelf reads as the number of the slot last used (reference run bookshelf: 21 cases, whatever the shelf holds).
	for (int32 Slot = -1; Slot <= 5; ++Slot)
	{
		const FIntVector Shelf(-10, 1, 3 + (Slot + 1) * 2);
		World.SetBlock(Shelf, State(TEXT("minecraft:chiseled_bookshelf[facing=south,slot_0_occupied=true,slot_1_occupied=false,slot_2_occupied=false,slot_3_occupied=false,slot_4_occupied=false,slot_5_occupied=false]")));
		World.SetBlock(Shelf + FIntVector(1, 0, 0), State(TEXT("minecraft:comparator[facing=west,mode=compare,powered=false]")));
		World.SetBlock(Shelf + FIntVector(2, 0, 0), State(TEXT("minecraft:redstone_wire")));
		Sim.BookshelfUsed(Shelf, Slot);
		for (int32 Index = 0; Index < 6; ++Index) { Sim.Tick(); }
		TestEqual(*FString::Printf(TEXT("slot %d reads %d"), Slot, Slot + 1), Sim.GetDustPower(Shelf + FIntVector(2, 0, 0)), Slot + 1);
	}
	return true;
}

#endif
