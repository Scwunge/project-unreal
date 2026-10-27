#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McPathfinding.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPathfindingTest, "McClient.McEntity.Pathfinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPathfindingTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Air = State(TEXT("minecraft:air"));

	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	// A stone floor at y 0: feet stand in y 1.
	for (int32 X = -16; X <= 31; ++X)
	{
		for (int32 Z = -16; Z <= 31; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Stone);
		}
	}
	const auto Fill = [&](int32 X0, int32 Y0, int32 Z0, int32 X1, int32 Y1, int32 Z1, FMcStateId Block)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 Z = Z0; Z <= Z1; ++Z)
				{
					World.SetBlock(FIntVector(X, Y, Z), Block);
				}
			}
		}
	};
	FMcPathOptions Options;
	TArray<FIntVector> Path;

	{
		// Open ground: straight there, the diagonal cut.
		const bool bReached = McPath::Find(World, FIntVector(0, 1, 0), FIntVector(5, 1, 3), Options, Path);
		TestTrue(TEXT("open ground: reaches the goal"), bReached);
		TestTrue(TEXT("and ends on it"), !Path.IsEmpty() && Path.Last() == FIntVector(5, 1, 3));
		TestEqual(TEXT("in five moves, three of them diagonal"), Path.Num(), 5);
		TestFalse(TEXT("starting on the goal needs no path"), !McPath::Find(World, FIntVector(2, 1, 2), FIntVector(2, 1, 2), Options, Path) || !Path.IsEmpty());
	}
	{
		// A wall across the way, seven wide and two high: round it.
		Fill(3, 1, -3, 3, 2, 3, Stone);
		const bool bReached = McPath::Find(World, FIntVector(0, 1, 0), FIntVector(6, 1, 0), Options, Path);
		TestTrue(TEXT("a wall: a way round"), bReached);
		bool bThroughWall = false;
		int32 MaxZ = 0;
		for (const FIntVector& Cell : Path)
		{
			bThroughWall |= Cell.X == 3 && Cell.Z >= -3 && Cell.Z <= 3;
			MaxZ = FMath::Max(MaxZ, FMath::Abs(Cell.Z));
		}
		TestFalse(TEXT("which doesn't go through it"), bThroughWall);
		TestTrue(TEXT("but out round its end"), MaxZ >= 4);
		TestTrue(TEXT("and ends at the goal"), !Path.IsEmpty() && Path.Last() == FIntVector(6, 1, 0));
		Fill(3, 1, -3, 3, 2, 3, Air);
	}
	{
		// Up a stair of full blocks, a step at a time.
		Fill(3, 1, 0, 3, 1, 0, Stone);
		Fill(4, 1, 0, 4, 2, 0, Stone);
		Fill(5, 1, 0, 5, 3, 0, Stone);
		Fill(6, 1, 0, 6, 4, 0, Stone);
		const bool bReached = McPath::Find(World, FIntVector(1, 1, 0), FIntVector(6, 5, 0), Options, Path);
		TestTrue(TEXT("stairs: reaches the top"), bReached);
		int32 Previous = 1;
		int32 BiggestStep = 0;
		for (const FIntVector& Cell : Path)
		{
			BiggestStep = FMath::Max(BiggestStep, Cell.Y - Previous);
			Previous = Cell.Y;
		}
		TestEqual(TEXT("climbing one block at a time"), BiggestStep, 1);
		TestTrue(TEXT("up to the top"), !Path.IsEmpty() && Path.Last() == FIntVector(6, 5, 0));
		// A wall two high is no step.
		Fill(9, 1, -16, 9, 2, 31, Stone);
		TestFalse(TEXT("a wall two blocks high isn't climbed"), McPath::Find(World, FIntVector(7, 1, 0), FIntVector(11, 1, 0), Options, Path));
		Fill(9, 1, -16, 9, 2, 31, Air);
		Fill(3, 1, 0, 6, 4, 0, Air);
	}
	{
		// A real stair block is a step, too.
		World.SetBlock(FIntVector(4, 1, 0), State(TEXT("minecraft:oak_stairs[facing=east,half=bottom,shape=straight]")));
		TestTrue(TEXT("onto a stair block"), McPath::Find(World, FIntVector(2, 1, 0), FIntVector(4, 2, 0), Options, Path));
		World.SetBlock(FIntVector(4, 1, 0), Air);
	}
	{
		// Drops: three are fine, five are not.
		Fill(20, 1, 20, 20, 3, 20, Stone);
		TestTrue(TEXT("a drop of three"), McPath::Find(World, FIntVector(20, 4, 20), FIntVector(23, 1, 20), Options, Path));
		Fill(20, 4, 20, 20, 5, 20, Stone);
		const bool bReached = McPath::Find(World, FIntVector(20, 6, 20), FIntVector(23, 1, 20), Options, Path);
		TestFalse(TEXT("a drop of five: no way down"), bReached);
		TestTrue(TEXT("and no way anywhere, with the ground that far down"), Path.IsEmpty());
		Fill(20, 1, 20, 20, 5, 20, Air);
	}
	{
		// Lava: a walled corridor with lava in the floor halfway along has no way through.
		Fill(-1, 1, -1, 11, 3, 1, Stone);
		Fill(0, 1, 0, 10, 2, 0, Air);
		World.SetBlock(FIntVector(5, 0, 0), State(TEXT("minecraft:lava[level=0]")));
		const bool bReached = McPath::Find(World, FIntVector(0, 1, 0), FIntVector(8, 1, 0), Options, Path);
		TestFalse(TEXT("lava in the floor of a corridor: no way through"), bReached);
		bool bOnLava = false;
		int32 FarthestX = 0;
		for (const FIntVector& Cell : Path)
		{
			bOnLava |= Cell.X == 5;
			FarthestX = FMath::Max(FarthestX, Cell.X);
		}
		TestFalse(TEXT("the way that gets closest doesn't cross it"), bOnLava);
		TestEqual(TEXT("it stops at the edge"), FarthestX, 4);

		// The same without the lava.
		World.SetBlock(FIntVector(5, 0, 0), Stone);
		TestTrue(TEXT("a stone floor: through"), McPath::Find(World, FIntVector(0, 1, 0), FIntVector(8, 1, 0), Options, Path));
		TestEqual(TEXT("straight along"), Path.Num(), 8);

		// Fire and cactus on the way are no better.
		World.SetBlock(FIntVector(5, 1, 0), State(TEXT("minecraft:fire[age=0,east=false,north=false,south=false,up=false,west=false]")));
		TestFalse(TEXT("fire in the corridor: no way through"), McPath::Find(World, FIntVector(0, 1, 0), FIntVector(8, 1, 0), Options, Path));
		World.SetBlock(FIntVector(5, 1, 0), Air);
		World.SetBlock(FIntVector(5, 0, 0), State(TEXT("minecraft:magma_block")));
		TestFalse(TEXT("magma for a floor: no way through"), McPath::Find(World, FIntVector(0, 1, 0), FIntVector(8, 1, 0), Options, Path));
		World.SetBlock(FIntVector(5, 0, 0), Stone);
		World.SetBlock(FIntVector(5, 1, 0), State(TEXT("minecraft:water[level=0]")));
		TestFalse(TEXT("water in the corridor: no way through"), McPath::Find(World, FIntVector(0, 1, 0), FIntVector(8, 1, 0), Options, Path));
		World.SetBlock(FIntVector(5, 1, 0), Air);
		Fill(-1, 1, -1, 11, 3, 1, Air);
	}
	{
		// A head-high ceiling: a body two high doesn't fit under a one-block gap, a body one high does.
		Fill(10, 1, -16, 10, 2, 31, Stone);
		Fill(10, 1, 0, 10, 1, 0, Air);
		FMcPathOptions Tall;
		Tall.Clearance = 2;
		FMcPathOptions Short;
		Short.Clearance = 1;
		TestFalse(TEXT("through a gap of one: not if two high"), McPath::Find(World, FIntVector(8, 1, 0), FIntVector(12, 1, 0), Tall, Path));
		TestTrue(TEXT("but if one high"), McPath::Find(World, FIntVector(8, 1, 0), FIntVector(12, 1, 0), Short, Path));
		Fill(10, 1, -16, 10, 2, 31, Air);
	}
	{
		// The node cap gives a partial path towards the goal.
		FMcPathOptions Capped;
		Capped.MaxNodes = 12;
		const bool bReached = McPath::Find(World, FIntVector(-14, 1, 0), FIntVector(28, 1, 0), Capped, Path);
		TestFalse(TEXT("too far for 12 nodes"), bReached);
		TestTrue(TEXT("but it leads some way towards it"), !Path.IsEmpty() && Path.Last().X > -14);
		TestTrue(TEXT("and not past what was looked at"), Path.Num() <= 12);
	}
	return true;
}

#endif
