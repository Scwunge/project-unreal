#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Which water wets farmland (reference run farmland, tools/refscripts/gen_farmland.py, farmcmp.py): a source within 4 blocks either way
// in x and z (the corner at 4,4 too, 5 not), on the farmland's level or the one above; one below or two above does nothing. Rain wets it with no water.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFarmlandReachTest, "McClient.McSim.FarmlandReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFarmlandReachTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	const int32 Offsets[][2] = { { 1, 0 }, { 4, 0 }, { 5, 0 }, { 4, 4 }, { 5, 4 }, { 4, 5 }, { 0, 4 }, { 0, 5 }, { -4, 0 }, { -5, 0 } };
	for (int32 Dy = -1; Dy <= 2; ++Dy) for (const int32* Off : Offsets)
	{
		FMcBlockWorld World{FMcDimensionType()};
		World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
		World.AddChunk(World.MakeChunk(FIntPoint(1, 0)));
		FMcSimulation Sim(World);
		const FIntVector Farm(8, 4, 8);
		for (int32 X = 0; X < 32; ++X) for (int32 Z = 0; Z < 16; ++Z) World.SetBlock(FIntVector(X, 3, Z), State(TEXT("minecraft:stone")));
		World.SetBlock(Farm, State(TEXT("minecraft:farmland[moisture=3]")));
		World.SetBlock(Farm + FIntVector(0, 1, 0), State(TEXT("minecraft:wheat[age=0]")));
		const FIntVector Water = Farm + FIntVector(Off[0], Dy, Off[1]);
		for (const FIntVector& Side : { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) }) if (Water + Side != Farm) World.SetBlock(Water + Side, State(TEXT("minecraft:stone")));
		World.SetBlock(Water, State(TEXT("minecraft:water")));
		for (int32 Tick = 0; Tick < 10; ++Tick) Sim.RandomTickBlock(Farm);
		const bool bWet = Registry.GetPropertyValue(World.GetBlock(Farm), TEXT("moisture")) == TEXT("7");
		const bool bExpected = (Dy == 0 || Dy == 1) && FMath::Abs(Off[0]) <= 4 && FMath::Abs(Off[1]) <= 4;
		TestEqual(*FString::Printf(TEXT("water at dy=%d dx=%d dz=%d wets"), Dy, Off[0], Off[1]), bWet, bExpected);
	}
	return true;
}

#endif
