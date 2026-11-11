#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// What flowing water and lava did to each block next to a source (reference run displace, tools/refscripts/gen_displace.py,
// displacecmp.py): the first list was replaced by the fluid, the second stayed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFluidDisplaceTest, "McClient.McSim.FluidDisplace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFluidDisplaceTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const TCHAR* Replaced[] = { TEXT("end_rod"), TEXT("torch"), TEXT("redstone_wire"), TEXT("stone_button"), TEXT("lever"), TEXT("white_carpet"), TEXT("snow[layers=1]"),
		TEXT("flower_pot"), TEXT("short_grass"), TEXT("poppy"), TEXT("oak_sapling"), TEXT("tall_grass[half=lower]"), TEXT("fire"), TEXT("cobweb"), TEXT("repeater"), TEXT("comparator"), TEXT("oak_button"), TEXT("tripwire") };
	const TCHAR* Stayed[] = { TEXT("oak_sign"), TEXT("oak_wall_sign"), TEXT("ladder"), TEXT("oak_slab"), TEXT("oak_stairs"), TEXT("lantern"), TEXT("iron_bars"), TEXT("glass_pane"), TEXT("oak_fence"), TEXT("oak_trapdoor"),
		TEXT("chest"), TEXT("light"), TEXT("barrier"), TEXT("scaffolding"), TEXT("candle"), TEXT("lightning_rod"), TEXT("hopper"), TEXT("rail"), TEXT("oak_pressure_plate"), TEXT("cauldron"),
		TEXT("oak_door"), TEXT("decorated_pot"), TEXT("stone_slab"), TEXT("copper_chain"), TEXT("pointed_dripstone") };
	const auto Run = [&](const TCHAR* Name, const TCHAR* Fluid) -> bool
	{
		FMcBlockWorld World{FMcDimensionType()};
		World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
		FMcSimulation Sim(World);
		const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
		for (int32 X = 0; X < 12; ++X) for (int32 Z = 0; Z < 3; ++Z) World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:grass_block")));
		for (int32 X = 0; X < 12; ++X) World.SetBlock(FIntVector(X, 1, 0), State(TEXT("minecraft:stone")));
		for (int32 X = 0; X < 12; ++X) World.SetBlock(FIntVector(X, 1, 2), State(TEXT("minecraft:stone")));
		World.SetBlock(FIntVector(2, 1, 1), State(FString::Printf(TEXT("minecraft:%s"), Name)));
		World.SetBlock(FIntVector(3, 1, 1), State(FString::Printf(TEXT("minecraft:%s"), Fluid)));
		for (int32 Tick = 0; Tick < 200; ++Tick) Sim.Tick();
		return Registry.GetBlockOf(World.GetBlock(FIntVector(2, 1, 1))).Name == FString::Printf(TEXT("minecraft:%s"), Fluid) || Registry.GetBlockOf(World.GetBlock(FIntVector(2, 1, 1))).Name == TEXT("minecraft:air");
	};
	for (const TCHAR* Fluid : { TEXT("water"), TEXT("lava") })
	{
		for (const TCHAR* Name : Replaced) TestTrue(*FString::Printf(TEXT("%s replaces %s"), Fluid, Name), Run(Name, Fluid));
		for (const TCHAR* Name : Stayed) TestFalse(*FString::Printf(TEXT("%s leaves %s"), Fluid, Name), Run(Name, Fluid));
	}
	return true;
}

#endif
