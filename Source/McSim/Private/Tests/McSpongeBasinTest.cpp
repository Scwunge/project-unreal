#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// A sponge set in a walled basin full of water (reference run sponge2, tools/refscripts/gen_sponge2.py): the water left right after, counted
// over the basin. It takes at most 65 blocks besides its own cell, and none further than 6 steps through water. NOT matched: a 3x3x12
// column (run sponge2, sponge3), where vanilla left the middle two cells of the line straight down at depth 5 and 6 and took 48, not the 50 that
// 6 steps allow, while a 1x1x12 column took the full 6 down; no rule tried (BFS or DFS, any direction order) gave 48, 6 and 16 for the three columns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSpongeBasinTest, "McClient.McSim.SpongeBasins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSpongeBasinTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	struct FBasin { const TCHAR* Name; int32 W, D, H; int32 Where; int32 Left; };
	// Where: 0 top middle, 1 bottom middle. Left is the vanilla count of water blocks after the sponge went in.
	const FBasin Basins[] = {
		{ TEXT("flat9"), 9, 9, 1, 0, 15 }, { TEXT("flat13"), 13, 13, 1, 0, 103 }, { TEXT("cube4"), 4, 4, 4, 0, 1 }, { TEXT("cube5"), 5, 5, 5, 0, 59 },
		{ TEXT("cube5low"), 5, 5, 5, 1, 59 }, { TEXT("cube6"), 6, 6, 6, 0, 150 }, { TEXT("flat5deep3"), 5, 5, 3, 0, 9 },
		{ TEXT("line30"), 30, 1, 1, 0, 17 }, { TEXT("cube3"), 3, 3, 3, 0, 0 },
	};
	for (const FBasin& Basin : Basins)
	{
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 X = 0; X < 3; ++X) World.AddChunk(World.MakeChunk(FIntPoint(X, 0)));
		FMcSimulation Sim(World);
		const FIntVector Origin(3, 1, 3);
		for (int32 X = -1; X <= Basin.W; ++X) for (int32 Z = -1; Z <= Basin.D; ++Z) for (int32 Y = -1; Y <= Basin.H; ++Y)
		{
			const bool bInside = X >= 0 && X < Basin.W && Z >= 0 && Z < Basin.D && Y >= 0 && Y < Basin.H;
			World.SetBlock(Origin + FIntVector(X, Y, Z), State(bInside ? TEXT("minecraft:water") : (Y == Basin.H ? TEXT("minecraft:air") : TEXT("minecraft:stone"))));
		}
		World.SetBlock(Origin + FIntVector(Basin.W / 2, Basin.Where == 0 ? Basin.H - 1 : 0, Basin.D / 2), State(TEXT("minecraft:sponge")));
		Sim.Tick();
		int32 Water = 0;
		for (int32 X = 0; X < Basin.W; ++X) for (int32 Z = 0; Z < Basin.D; ++Z) for (int32 Y = 0; Y < Basin.H; ++Y)
		{
			Water += Registry.GetBlockOf(World.GetBlock(Origin + FIntVector(X, Y, Z))).Name == TEXT("minecraft:water");
		}
		TestEqual(*FString::Printf(TEXT("water left in %s"), Basin.Name), Water, Basin.Left);
	}
	return true;
}

#endif
