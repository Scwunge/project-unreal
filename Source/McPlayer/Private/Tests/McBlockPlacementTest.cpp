#include "McBlockPlacement.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBlockPlacementTest, "McClient.McPlayer.BlockPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBlockPlacementTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()) || !TestTrue(TEXT("shapes load"), FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));

	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	World.SetBlock(FIntVector(4, 0, 4), Stone);

	// Hits on the stone at (4, 0, 4): its top, and its north face high and low.
	auto MakeHit = [&](int32 Face, double Y)
	{
		FMcBlockHit Hit;
		Hit.Block = FIntVector(4, 0, 4);
		Hit.Face = Face;
		Hit.State = World.GetBlock(Hit.Block);
		Hit.Point = FVector(4.5, Y, Face == 2 ? 4.0 : 4.5);
		return Hit;
	};
	const FMcBlockHit Top = MakeHit(1, 1.0);
	const FMcBlockHit NorthHigh = MakeHit(2, 0.75);
	const FMcBlockHit NorthLow = MakeHit(2, 0.25);

	auto Place = [&](const TCHAR* Block, const FMcBlockHit& Hit, float Yaw, float Pitch = 0.0f)
	{
		const TArray<FMcPlacedBlock> Placed = McBlockPlacement::GetPlacement(World, State(Block), Hit, Yaw, Pitch);
		return Placed.Num() > 0 ? Registry.Describe(Placed[0].State) : FString(TEXT("nothing"));
	};

	TestEqual(TEXT("stone goes on top"), McBlockPlacement::GetPlacement(World, Stone, Top, 0, 0)[0].Pos, FIntVector(4, 1, 4));
	TestEqual(TEXT("and beside a side face"), McBlockPlacement::GetPlacement(World, Stone, NorthHigh, 0, 0)[0].Pos, FIntVector(4, 0, 3));

	TestTrue(TEXT("logs on top stand up"), Place(TEXT("minecraft:oak_log"), Top, 0).Contains(TEXT("axis=y")));
	TestTrue(TEXT("logs on a north face lie along z"), Place(TEXT("minecraft:oak_log"), NorthHigh, 0).Contains(TEXT("axis=z")));

	TestTrue(TEXT("slabs on the upper half of a side go top"), Place(TEXT("minecraft:oak_slab"), NorthHigh, 0).Contains(TEXT("type=top")));
	TestTrue(TEXT("slabs on the lower half go bottom"), Place(TEXT("minecraft:oak_slab"), NorthLow, 0).Contains(TEXT("type=bottom")));

	// Facing south (yaw 0): stairs face south (away), furnaces face north (towards the player).
	TestTrue(TEXT("stairs face the way the player looks"), Place(TEXT("minecraft:oak_stairs"), Top, 0).Contains(TEXT("facing=south")));
	TestTrue(TEXT("upside-down stairs from a high side hit"), Place(TEXT("minecraft:oak_stairs"), NorthHigh, 0).Contains(TEXT("half=top")));
	TestTrue(TEXT("furnaces face the player"), Place(TEXT("minecraft:furnace"), Top, 0).Contains(TEXT("facing=north")));
	TestTrue(TEXT("facing east (yaw 270) stairs face east"), Place(TEXT("minecraft:oak_stairs"), Top, 270).Contains(TEXT("facing=east")));
	TestTrue(TEXT("pistons looked down on face up"), Place(TEXT("minecraft:piston"), Top, 0, 80).Contains(TEXT("facing=up")));

	TestTrue(TEXT("torches on a side hang on the wall"), Place(TEXT("minecraft:torch"), NorthHigh, 0).StartsWith(TEXT("minecraft:wall_torch[facing=north")));
	TestTrue(TEXT("torches on top stand"), Place(TEXT("minecraft:torch"), Top, 0).StartsWith(TEXT("minecraft:torch")));
	TestTrue(TEXT("hoppers on top point down"), Place(TEXT("minecraft:hopper"), Top, 0).Contains(TEXT("facing=down")));
	TestTrue(TEXT("hoppers on a north face point back into it"), Place(TEXT("minecraft:hopper"), NorthHigh, 0).Contains(TEXT("facing=south")));

	// Signs face the player: in sixteen steps standing, square on to the wall on one, and hanging under a block.
	TestEqual(TEXT("a sign on top faces the player"), Place(TEXT("minecraft:oak_sign"), Top, 0), FString(TEXT("minecraft:oak_sign[rotation=8,waterlogged=false]")));
	TestTrue(TEXT("turned a sixteenth"), Place(TEXT("minecraft:oak_sign"), Top, 22.5f).Contains(TEXT("rotation=9")));
	TestTrue(TEXT("a sign on a north face is on the wall facing north"), Place(TEXT("minecraft:oak_sign"), NorthHigh, 0).StartsWith(TEXT("minecraft:oak_wall_sign[facing=north")));
	World.SetBlock(FIntVector(8, 5, 8), Stone);
	FMcBlockHit Under;
	Under.Block = FIntVector(8, 5, 8);
	Under.Face = 0;
	Under.State = Stone;
	Under.Point = FVector(8.5, 5.0, 8.5);
	TestEqual(TEXT("a sign under a block is not placed"), Place(TEXT("minecraft:oak_sign"), Under, 0), FString(TEXT("nothing")));
	TestTrue(TEXT("a hanging sign under a block faces the player"), Place(TEXT("minecraft:oak_hanging_sign"), Under, 0).Contains(TEXT("rotation=8")));
	TestTrue(TEXT("on chains, not attached"), Place(TEXT("minecraft:oak_hanging_sign"), Under, 0).Contains(TEXT("attached=false")));
	TestTrue(TEXT("one on a north face hangs from the wall, across it"), Place(TEXT("minecraft:oak_hanging_sign"), NorthHigh, 0).StartsWith(TEXT("minecraft:oak_wall_hanging_sign[facing=east")) || Place(TEXT("minecraft:oak_hanging_sign"), NorthHigh, 0).StartsWith(TEXT("minecraft:oak_wall_hanging_sign[facing=west")));
	TestEqual(TEXT("a hanging sign on top with nothing above is not placed"), Place(TEXT("minecraft:oak_hanging_sign"), Top, 0), FString(TEXT("nothing")));

	const TArray<FMcPlacedBlock> Door = McBlockPlacement::GetPlacement(World, State(TEXT("minecraft:oak_door")), Top, 0, 0);
	TestEqual(TEXT("doors are two blocks"), Door.Num(), 2);

	// A bottom slab clicked on its top with the same slab becomes a double slab in place.
	World.SetBlock(FIntVector(4, 1, 4), State(TEXT("minecraft:oak_slab[type=bottom]")));
	FMcBlockHit SlabTop;
	SlabTop.Block = FIntVector(4, 1, 4);
	SlabTop.Face = 1;
	SlabTop.State = World.GetBlock(SlabTop.Block);
	SlabTop.Point = FVector(4.5, 1.5, 4.5);
	const TArray<FMcPlacedBlock> Double = McBlockPlacement::GetPlacement(World, State(TEXT("minecraft:oak_slab")), SlabTop, 0, 0);
	TestTrue(TEXT("double slab"), Double.Num() == 1 && Double[0].Pos == SlabTop.Block && Registry.Describe(Double[0].State).Contains(TEXT("type=double")));
	return true;
}

#endif
