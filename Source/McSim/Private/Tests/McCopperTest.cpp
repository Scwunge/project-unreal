#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McCopper.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCopperTest, "McClient.McSim.Copper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// Copper weathers on random ticks (reference run copper1b: 42 lone blocks at random tick speed 4096 took 3 steps at about 3.2 % a tick
// for the first and 5 % for the later ones, and a 5x5x5 cube of 125 stayed plain). The chance and the neighbour rule are the wiki's.
bool FMcCopperTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto N = [](const TCHAR* Name) { return FName(Name); };
	int32 Age = -1;
	TestTrue(TEXT("copper block is age 0"), McCopper::GetAge(N(TEXT("minecraft:copper_block")), Age) && Age == 0);
	TestTrue(TEXT("exposed copper is 1"), McCopper::GetAge(N(TEXT("minecraft:exposed_copper")), Age) && Age == 1);
	TestTrue(TEXT("a weathered stairs is 2"), McCopper::GetAge(N(TEXT("minecraft:weathered_cut_copper_stairs")), Age) && Age == 2);
	TestTrue(TEXT("an oxidized door is 3"), McCopper::GetAge(N(TEXT("minecraft:oxidized_copper_door")), Age) && Age == 3);
	TestTrue(TEXT("a lightning rod weathers"), McCopper::GetAge(N(TEXT("minecraft:lightning_rod")), Age) && Age == 0);
	TestFalse(TEXT("waxed does not"), McCopper::GetAge(N(TEXT("minecraft:waxed_copper_block")), Age));
	TestFalse(TEXT("ore does not"), McCopper::GetAge(N(TEXT("minecraft:copper_ore")), Age));
	TestFalse(TEXT("raw copper does not"), McCopper::GetAge(N(TEXT("minecraft:raw_copper_block")), Age));
	TestEqual(TEXT("next of the block"), McCopper::GetNextAge(N(TEXT("minecraft:copper_block"))), N(TEXT("minecraft:exposed_copper")));
	TestEqual(TEXT("next of cut copper"), McCopper::GetNextAge(N(TEXT("minecraft:cut_copper"))), N(TEXT("minecraft:exposed_cut_copper")));
	TestEqual(TEXT("oxidized has no next"), McCopper::GetNextAge(N(TEXT("minecraft:oxidized_copper"))), NAME_None);
	TestEqual(TEXT("previous of oxidized"), McCopper::GetPreviousAge(N(TEXT("minecraft:oxidized_copper"))), N(TEXT("minecraft:weathered_copper")));
	TestEqual(TEXT("previous of exposed block"), McCopper::GetPreviousAge(N(TEXT("minecraft:exposed_copper"))), N(TEXT("minecraft:copper_block")));
	TestEqual(TEXT("waxed twin"), McCopper::GetWaxed(N(TEXT("minecraft:exposed_copper"))), N(TEXT("minecraft:waxed_exposed_copper")));
	TestEqual(TEXT("waxed has no waxed twin"), McCopper::GetWaxed(N(TEXT("minecraft:waxed_copper_block"))), NAME_None);
	TestEqual(TEXT("unwaxed"), McCopper::GetUnwaxed(N(TEXT("minecraft:waxed_copper_block"))), N(TEXT("minecraft:copper_block")));
	TestEqual(TEXT("unwaxed stairs"), McCopper::GetUnwaxed(N(TEXT("minecraft:waxed_weathered_cut_copper_stairs"))), N(TEXT("minecraft:weathered_cut_copper_stairs")));
	TestEqual(TEXT("plain has no unwaxed"), McCopper::GetUnwaxed(N(TEXT("minecraft:copper_block"))), NAME_None);
	TestEqual(TEXT("weather factor of a lone plain block"), McCopper::GetWeatherFactor(0, 0, 0), 0.75f);
	TestEqual(TEXT("and of a lone weathered one"), McCopper::GetWeatherFactor(2, 0, 0), 1.0f);
	TestTrue(TEXT("many of the same age slow it"), McCopper::GetWeatherFactor(1, 0, 30) < 0.01f);

	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	FMcSimulation Sim(World);
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const auto NameAt = [&](const FIntVector& Pos) { return Registry.GetBlockOf(World.GetBlock(Pos)).Name; };
	const FIntVector Lone(2, 5, 2), Held(10, 5, 2), HeldBy(10, 5, 5), Waxed(2, 5, 12);
	World.SetBlock(Lone, State(TEXT("minecraft:copper_block")));
	World.SetBlock(Held, State(TEXT("minecraft:exposed_copper")));
	World.SetBlock(HeldBy, State(TEXT("minecraft:copper_block")));
	World.SetBlock(Waxed, State(TEXT("minecraft:waxed_copper_block")));
	for (int32 Tick = 0; Tick < 6000; ++Tick)
	{
		Sim.RandomTickBlock(Lone);
		Sim.RandomTickBlock(Held);
		Sim.RandomTickBlock(Waxed);
	}
	TestEqual(TEXT("a lone block weathers all the way"), NameAt(Lone), N(TEXT("minecraft:oxidized_copper")));
	TestEqual(TEXT("one with a plainer block beside it waits"), NameAt(Held), N(TEXT("minecraft:exposed_copper")));
	TestEqual(TEXT("waxed stays"), NameAt(Waxed), N(TEXT("minecraft:waxed_copper_block")));
	return true;
}

#endif
