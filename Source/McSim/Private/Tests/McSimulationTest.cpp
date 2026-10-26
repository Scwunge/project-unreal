#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSimulationTest, "McClient.McSim.Simulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSimulationTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Air = State(TEXT("minecraft:air"));
	const FMcStateId Source = State(TEXT("minecraft:water[level=0]"));
	const FMcStateId Lava = State(TEXT("minecraft:lava[level=0]"));
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();

	// A stone floor at y 0 with 3x3 chunks of air above.
	const auto MakeWorld = [&](FMcBlockWorld& World)
	{
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
	};
	const auto Run = [](FMcSimulation& Sim, int32 Ticks)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Sim.Tick();
		}
	};

	{
		// Water on a floor spreads to level 7 and 7 blocks out (taxicab), then stops.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), Source);
		Run(Sim, 100);
		TestEqual(TEXT("one block out is level 1"), static_cast<int32>(Shapes.GetFluidLevel(World.GetBlock(FIntVector(1, 1, 0)))), 1);
		TestEqual(TEXT("seven blocks out is level 7"), static_cast<int32>(Shapes.GetFluidLevel(World.GetBlock(FIntVector(7, 1, 0)))), 7);
		TestTrue(TEXT("seven blocks out holds water"), Shapes.GetFluid(World.GetBlock(FIntVector(7, 1, 0))) == EMcFluid::Water);
		TestTrue(TEXT("eight blocks out is dry"), Shapes.GetFluid(World.GetBlock(FIntVector(8, 1, 0))) == EMcFluid::None);
		TestTrue(TEXT("a diagonal reaches 4 and 3"), Shapes.GetFluid(World.GetBlock(FIntVector(4, 1, 3))) == EMcFluid::Water);
		TestEqual(TEXT("nothing left scheduled"), Sim.NumScheduled(), 0);

		// Take the source away and the flow drains.
		World.SetBlock(FIntVector(0, 1, 0), Air);
		Run(Sim, 400);
		TestTrue(TEXT("it dries up"), Shapes.GetFluid(World.GetBlock(FIntVector(1, 1, 0))) == EMcFluid::None && Shapes.GetFluid(World.GetBlock(FIntVector(5, 1, 0))) == EMcFluid::None);
	}
	{
		// A source above a drop falls, and spreads out at the bottom.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 5, 0), Source);
		Run(Sim, 200);
		for (int32 Y = 1; Y <= 4; ++Y)
		{
			TestTrue(FString::Printf(TEXT("water falls through y %d"), Y), Shapes.GetFluid(World.GetBlock(FIntVector(0, Y, 0))) == EMcFluid::Water);
		}
		TestEqual(TEXT("the fall is falling water"), static_cast<int32>(Shapes.GetFluidLevel(World.GetBlock(FIntVector(0, 3, 0)))), 8);
		TestTrue(TEXT("it spreads along the floor"), Shapes.GetFluid(World.GetBlock(FIntVector(5, 1, 0))) == EMcFluid::Water);
	}
	{
		// Two sources beside each other over a floor make a third: an infinite source.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), Source);
		World.SetBlock(FIntVector(2, 1, 0), Source);
		Run(Sim, 60);
		TestEqual(TEXT("the gap between two sources fills with a source"), static_cast<int32>(Shapes.GetFluidLevel(World.GetBlock(FIntVector(1, 1, 0)))), 0);
		TestTrue(TEXT("and is water"), Shapes.GetFluid(World.GetBlock(FIntVector(1, 1, 0))) == EMcFluid::Water);
	}
	{
		// Water goes around a wall towards a hole.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(3, 0, 0), Air);
		World.SetBlock(FIntVector(0, 1, 0), Source);
		Run(Sim, 200);
		TestTrue(TEXT("water reaches the hole"), Shapes.GetFluid(World.GetBlock(FIntVector(3, 0, 0))) == EMcFluid::Water);
	}
	{
		// Water on lava source makes obsidian; on flowing lava, cobblestone; lava spreads two levels a block.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), Lava);
		Run(Sim, 400);
		TestEqual(TEXT("lava level at 1 block"), static_cast<int32>(Shapes.GetFluidLevel(World.GetBlock(FIntVector(1, 1, 0)))), 2);
		TestEqual(TEXT("lava level at 3 blocks"), static_cast<int32>(Shapes.GetFluidLevel(World.GetBlock(FIntVector(3, 1, 0)))), 6);
		TestTrue(TEXT("lava stops at 4 blocks"), Shapes.GetFluid(World.GetBlock(FIntVector(4, 1, 0))) == EMcFluid::None);
		World.SetBlock(FIntVector(0, 2, 0), Source);
		Run(Sim, 400);
		TestEqual(TEXT("water over a lava source makes obsidian"), Registry.GetBlockOf(World.GetBlock(FIntVector(0, 1, 0))).Name.ToString(), FString(TEXT("minecraft:obsidian")));
		TestEqual(TEXT("and beside flowing lava, cobblestone"), Registry.GetBlockOf(World.GetBlock(FIntVector(1, 1, 0))).Name.ToString(), FString(TEXT("minecraft:cobblestone")));
	}
	{
		// Sand with nothing under it falls to the floor.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 6, 0), State(TEXT("minecraft:sand")));
		Run(Sim, 10);
		TestEqual(TEXT("sand lands on the floor"), Registry.GetBlockOf(World.GetBlock(FIntVector(0, 1, 0))).Name.ToString(), FString(TEXT("minecraft:sand")));
		TestTrue(TEXT("and leaves its place"), Registry.IsAir(World.GetBlock(FIntVector(0, 6, 0))));
	}
	return true;
}

#endif
