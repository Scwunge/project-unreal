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
		Run(Sim, 30);
		TestEqual(TEXT("sand lands on the floor"), Registry.GetBlockOf(World.GetBlock(FIntVector(0, 1, 0))).Name.ToString(), FString(TEXT("minecraft:sand")));
		TestTrue(TEXT("and leaves its place"), Registry.IsAir(World.GetBlock(FIntVector(0, 6, 0))));
	}
	{
		// A sand block falling 8 blocks moves as the reference client's falling_block entity does (rs log falling_ents):
		// down 0.04 and then times 0.98 a tick, so after 20 ticks it has dropped 7.4256, and it lands on the next.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 9, 0), State(TEXT("minecraft:sand")));
		double LastY = 0.0;
		int32 Observed = 0;
		for (int32 Tick = 0; Tick < 40; ++Tick)
		{
			Sim.Tick();
			if (Sim.GetFallingBlocks().IsEmpty())
			{
				if (Observed > 0)
				{
					break;
				}
				continue;
			}
			LastY = Sim.GetFallingBlocks()[0].Y;
			++Observed;
		}
		TestEqual(TEXT("it fell 7.4256 before the tick it landed"), 9.0 - LastY, 7.4256, 0.001);
		TestEqual(TEXT("and landed"), Registry.GetBlockOf(World.GetBlock(FIntVector(0, 1, 0))).Name.ToString(), FString(TEXT("minecraft:sand")));
	}
	{
		// An anvil that lands says how far it fell, for the server to hurt what is under it.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		double Fallen = 0.0;
		Sim.OnFallingBlockLanded = [&Fallen](const FIntVector&, FMcStateId, double Distance) { Fallen = Distance; };
		World.SetBlock(FIntVector(0, 9, 0), State(TEXT("minecraft:anvil[facing=north]")));
		Run(Sim, 40);
		TestEqual(TEXT("it reports 8 blocks fallen"), Fallen, 8.0, 0.001);
	}
	{
		// Random tick rules, run on single blocks.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		const auto Name = [&](const FIntVector& Pos) { return Registry.GetBlockOf(World.GetBlock(Pos)).Name.ToString(); };
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:grass_block")));
		World.SetBlock(FIntVector(0, 2, 0), Stone);
		Sim.RandomTickBlock(FIntVector(0, 1, 0));
		TestEqual(TEXT("grass under stone turns to dirt"), Name(FIntVector(0, 1, 0)), FString(TEXT("minecraft:dirt")));

		World.SetBlock(FIntVector(5, 1, 0), State(TEXT("minecraft:oak_leaves[distance=7,persistent=false]")));
		Sim.RandomTickBlock(FIntVector(5, 1, 0));
		TestTrue(TEXT("leaves far from a log decay"), Registry.IsAir(World.GetBlock(FIntVector(5, 1, 0))));
		World.SetBlock(FIntVector(5, 1, 0), State(TEXT("minecraft:oak_leaves[distance=7,persistent=true]")));
		Sim.RandomTickBlock(FIntVector(5, 1, 0));
		TestEqual(TEXT("player-placed leaves stay"), Name(FIntVector(5, 1, 0)), FString(TEXT("minecraft:oak_leaves")));

		// A leaf beside a log gets distance 1 after its tick, and one more block out gets 2.
		World.SetBlock(FIntVector(8, 1, 0), State(TEXT("minecraft:oak_log")));
		World.SetBlock(FIntVector(9, 1, 0), State(TEXT("minecraft:oak_leaves[distance=7]")));
		World.SetBlock(FIntVector(10, 1, 0), State(TEXT("minecraft:oak_leaves[distance=7]")));
		Run(Sim, 10);
		TestEqual(TEXT("leaf beside a log"), Registry.GetPropertyValue(World.GetBlock(FIntVector(9, 1, 0)), TEXT("distance")).ToString(), FString(TEXT("1")));
		TestEqual(TEXT("leaf beside that leaf"), Registry.GetPropertyValue(World.GetBlock(FIntVector(10, 1, 0)), TEXT("distance")).ToString(), FString(TEXT("2")));
		World.SetBlock(FIntVector(8, 1, 0), Air);
		Run(Sim, 20);
		TestEqual(TEXT("with the log gone the distances climb"), Registry.GetPropertyValue(World.GetBlock(FIntVector(9, 1, 0)), TEXT("distance")).ToString(), FString(TEXT("7")));

		// Wheat grows with random ticks, and stops at its last stage.
		World.SetBlock(FIntVector(0, 1, 5), State(TEXT("minecraft:farmland[moisture=7]")));
		World.SetBlock(FIntVector(0, 2, 5), State(TEXT("minecraft:wheat[age=0]")));
		for (int32 Index = 0; Index < 2000; ++Index)
		{
			Sim.RandomTickBlock(FIntVector(0, 2, 5));
		}
		TestEqual(TEXT("wheat reaches full growth"), Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 2, 5)), TEXT("age")).ToString(), FString(TEXT("7")));

		// Farmland with water beside it is wet; without any it dries and reverts.
		World.SetBlock(FIntVector(0, 2, 5), Air);
		World.SetBlock(FIntVector(0, 1, 5), State(TEXT("minecraft:farmland[moisture=0]")));
		Sim.RandomTickBlock(FIntVector(0, 1, 5));
		TestEqual(TEXT("dry farmland turns to dirt"), Name(FIntVector(0, 1, 5)), FString(TEXT("minecraft:dirt")));
		World.SetBlock(FIntVector(0, 1, 5), State(TEXT("minecraft:farmland[moisture=0]")));
		World.SetBlock(FIntVector(2, 1, 5), Source);
		Sim.RandomTickBlock(FIntVector(0, 1, 5));
		TestEqual(TEXT("farmland beside water is moist"), Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 1, 5)), TEXT("moisture")).ToString(), FString(TEXT("7")));

		// A flower breaks when the block under it goes.
		World.SetBlock(FIntVector(-5, 1, 0), State(TEXT("minecraft:dirt")));
		World.SetBlock(FIntVector(-5, 2, 0), State(TEXT("minecraft:dandelion")));
		Run(Sim, 3);
		TestEqual(TEXT("a flower stays on dirt"), Name(FIntVector(-5, 2, 0)), FString(TEXT("minecraft:dandelion")));
		World.SetBlock(FIntVector(-5, 1, 0), Air);
		Run(Sim, 3);
		TestTrue(TEXT("and goes without it"), Registry.IsAir(World.GetBlock(FIntVector(-5, 2, 0))));
	}
	{
		// Fire on a wool floor burns it and spreads; fire on stone with nothing to burn goes out.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		Sim.SetWeatherCycle(false);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:fire")));
		Run(Sim, 700);
		TestTrue(TEXT("fire with no fuel goes out"), Registry.IsAir(World.GetBlock(FIntVector(0, 1, 0))));
		for (int32 X = 5; X <= 9; ++X)
		{
			World.SetBlock(FIntVector(X, 1, 0), State(TEXT("minecraft:white_wool")));
		}
		World.SetBlock(FIntVector(5, 2, 0), State(TEXT("minecraft:fire")));
		Run(Sim, 3000);
		int32 Burnt = 0;
		for (int32 X = 5; X <= 9; ++X)
		{
			Burnt += Registry.GetBlockOf(World.GetBlock(FIntVector(X, 1, 0))).Name == TEXT("minecraft:white_wool") ? 0 : 1;
		}
		TestTrue(TEXT("fire eats wool beside it"), Burnt >= 2);
	}
	{
		// Water over soul sand becomes a bubble column 20 ticks later, pushing up; over magma it pushes down; it goes when
		// the block under it does (the delays are fitted to the bubble2 vanilla run).
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		const auto Name = [&](const FIntVector& At) { return Registry.GetBlockOf(World.GetBlock(At)).Name; };
		for (int32 Y = 1; Y <= 5; ++Y)
		{
			for (const FIntVector& Side : { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) })
			{
				World.SetBlock(FIntVector(0, Y, 0) + Side, Stone);
			}
		}
		for (int32 Y = 2; Y <= 5; ++Y)
		{
			World.SetBlock(FIntVector(0, Y, 0), Source);
		}
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:soul_sand")));
		Run(Sim, 12);
		TestEqual(TEXT("no column yet at 12 ticks"), Name(FIntVector(0, 2, 0)), FName(TEXT("minecraft:water")));
		Run(Sim, 20);
		for (int32 Y = 2; Y <= 5; ++Y)
		{
			TestEqual(*FString::Printf(TEXT("column at y %d"), Y), Name(FIntVector(0, Y, 0)), FName(TEXT("minecraft:bubble_column")));
			TestEqual(TEXT("pushing up"), Registry.GetPropertyValue(World.GetBlock(FIntVector(0, Y, 0)), TEXT("drag")), FName(TEXT("false")));
		}
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:magma_block")));
		Run(Sim, 40);
		TestEqual(TEXT("magma pulls down"), Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 3, 0)), TEXT("drag")), FName(TEXT("true")));
		World.SetBlock(FIntVector(0, 1, 0), Stone);
		Run(Sim, 60);
		for (int32 Y = 2; Y <= 5; ++Y)
		{
			TestEqual(*FString::Printf(TEXT("water again at y %d"), Y), Name(FIntVector(0, Y, 0)), FName(TEXT("minecraft:water")));
		}
	}
	return true;
}

#endif
