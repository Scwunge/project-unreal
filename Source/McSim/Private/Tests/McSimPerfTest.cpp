// How long the simulation takes a tick on a busy world: a game tick has 50 ms to keep 20 ticks a second.
// Each scene is built in a 9x9 chunk world, run for a while, and timed; the numbers are logged, and the test
// fails only when a scene's average passes a share of the budget.
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSimPerfTest, "McClient.McSim.SimPerf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::PerfFilter)

bool FMcSimPerfTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Grass = State(TEXT("minecraft:grass_block"));
	constexpr int32 Half = 4;     // chunks each way from the origin
	constexpr int32 Extent = Half * 16;

	struct FScene
	{
		FString Name;
		TFunction<void(FMcBlockWorld&, FMcSimulation&)> Build;
		TFunction<void(FMcBlockWorld&, FMcSimulation&, int32)> Each; // called before each tick
		int32 Ticks = 200;
	};
	TArray<FScene> Scenes;

	// Dust lines with levers that flip every tick: every flip re-solves the line and its neighbours.
	Scenes.Add({TEXT("dust-lines-flipping"),
		[&](FMcBlockWorld& World, FMcSimulation&)
		{
			for (int32 Row = -30; Row <= 30; Row += 2)
			{
				for (int32 X = -30; X <= 30; ++X)
				{
					World.SetBlock(FIntVector(X, 1, Row), State(TEXT("minecraft:redstone_wire")));
				}
				World.SetBlock(FIntVector(-31, 1, Row), State(TEXT("minecraft:lever[face=floor,facing=north,powered=false]")));
			}
		},
		[&](FMcBlockWorld& World, FMcSimulation&, int32 Tick)
		{
			for (int32 Row = -30; Row <= 30; Row += 2)
			{
				World.SetBlock(FIntVector(-31, 1, Row), State((Tick & 1) ? TEXT("minecraft:lever[face=floor,facing=north,powered=true]") : TEXT("minecraft:lever[face=floor,facing=north,powered=false]")));
			}
		}, 100});

	// A dust grid (every cell dust): the worst case for connection and power recomputation.
	Scenes.Add({TEXT("dust-grid-flipping"),
		[&](FMcBlockWorld& World, FMcSimulation&)
		{
			for (int32 X = -12; X <= 12; ++X)
			{
				for (int32 Z = -12; Z <= 12; ++Z)
				{
					World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:redstone_wire")));
				}
			}
			World.SetBlock(FIntVector(-13, 1, 0), State(TEXT("minecraft:lever[face=floor,facing=north,powered=false]")));
		},
		[&](FMcBlockWorld& World, FMcSimulation&, int32 Tick)
		{
			World.SetBlock(FIntVector(-13, 1, 0), State((Tick & 1) ? TEXT("minecraft:lever[face=floor,facing=north,powered=true]") : TEXT("minecraft:lever[face=floor,facing=north,powered=false]")));
		}, 100});

	// Observer chains kicked every 8 ticks.
	Scenes.Add({TEXT("observer-chains"),
		[&](FMcBlockWorld& World, FMcSimulation&)
		{
			for (int32 Row = -30; Row <= 30; Row += 2)
			{
				for (int32 X = -30; X <= 30; ++X)
				{
					World.SetBlock(FIntVector(X, 1, Row), State(TEXT("minecraft:observer[facing=east]")));
				}
			}
		},
		[&](FMcBlockWorld& World, FMcSimulation&, int32 Tick)
		{
			if (Tick % 8 == 0)
			{
				for (int32 Row = -30; Row <= 30; Row += 2)
				{
					World.SetBlock(FIntVector(-31, 1, Row), (Tick & 8) ? Stone : State(TEXT("minecraft:air")));
				}
			}
		}, 160});

	// Piston arrays, each driven by a lever flipped every 6 ticks.
	Scenes.Add({TEXT("piston-array"),
		[&](FMcBlockWorld& World, FMcSimulation&)
		{
			for (int32 X = -30; X <= 30; X += 3)
			{
				for (int32 Z = -30; Z <= 30; Z += 3)
				{
					World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:piston[facing=up,extended=false]")));
					World.SetBlock(FIntVector(X, 3, Z), Stone);
					World.SetBlock(FIntVector(X + 1, 1, Z), State(TEXT("minecraft:lever[face=floor,facing=north,powered=false]")));
				}
			}
		},
		[&](FMcBlockWorld& World, FMcSimulation&, int32 Tick)
		{
			if (Tick % 6 == 0)
			{
				const bool bOn = (Tick / 6) & 1;
				for (int32 X = -30; X <= 30; X += 3)
				{
					for (int32 Z = -30; Z <= 30; Z += 3)
					{
						World.SetBlock(FIntVector(X + 1, 1, Z), State(bOn ? TEXT("minecraft:lever[face=floor,facing=north,powered=true]") : TEXT("minecraft:lever[face=floor,facing=north,powered=false]")));
					}
				}
			}
		}, 240});

	// Water spreading over a big flat floor, then retriggered by removing and replacing a source.
	Scenes.Add({TEXT("water-flood"),
		[&](FMcBlockWorld& World, FMcSimulation&)
		{
			for (int32 X = -Extent + 2; X <= Extent - 2; ++X)
			{
				for (int32 Z = -Extent + 2; Z <= Extent - 2; ++Z)
				{
					if ((X % 20 == 0) && (Z % 20 == 0))
					{
						World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:water[level=0]")));
					}
				}
			}
		},
		nullptr, 300});

	// Random ticks over the whole 9x9 chunk area: grass, crops, saplings, leaves, ice.
	Scenes.Add({TEXT("random-ticks"),
		[&](FMcBlockWorld& World, FMcSimulation& Sim)
		{
			TArray<FIntPoint> Chunks;
			for (int32 Z = -Half; Z <= Half; ++Z)
			{
				for (int32 X = -Half; X <= Half; ++X)
				{
					Chunks.Add(FIntPoint(X, Z));
				}
			}
			Sim.SetTickingChunks(Chunks);
			Sim.SetRandomTickSpeed(3);
			for (int32 X = -Extent + 2; X <= Extent - 2; ++X)
			{
				for (int32 Z = -Extent + 2; Z <= Extent - 2; ++Z)
				{
					World.SetBlock(FIntVector(X, 0, Z), Grass);
					if (((X * 7 + Z * 3) % 5) == 0)
					{
						World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:wheat[age=0]")));
					}
					else if (((X + Z) % 9) == 0)
					{
						World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:oak_sapling[stage=0]")));
					}
				}
			}
		},
		nullptr, 200});

	for (const FScene& Scene : Scenes)
	{
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -Half; Z <= Half; ++Z)
		{
			for (int32 X = -Half; X <= Half; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		for (int32 X = -Extent + 2; X <= Extent - 2; ++X)
		{
			for (int32 Z = -Extent + 2; Z <= Extent - 2; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Stone);
			}
		}
		FMcSimulation Sim(World);
		const double BuildStart = FPlatformTime::Seconds();
		Scene.Build(World, Sim);
		const double BuildMs = (FPlatformTime::Seconds() - BuildStart) * 1000.0;
		double Total = 0.0;
		double Worst = 0.0;
		double First = 0.0;
		for (int32 Tick = 0; Tick < Scene.Ticks; ++Tick)
		{
			const double Start = FPlatformTime::Seconds();
			if (Scene.Each)
			{
				Scene.Each(World, Sim, Tick);
			}
			Sim.Tick();
			const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
			if (Tick == 0)
			{
				First = Ms;
			}
			Total += Ms;
			Worst = FMath::Max(Worst, Ms);
		}
		const double Avg = Total / Scene.Ticks;
		UE_LOG(LogTemp, Display, TEXT("SIMPERF %s: build %.1f ms, tick avg %.3f ms, first %.3f ms, worst %.3f ms, scheduled left %d"), *Scene.Name, BuildMs, Avg, First, Worst, Sim.NumScheduled());
		TestTrue(*FString::Printf(TEXT("%s averages under 25 ms a tick (%.2f)"), *Scene.Name, Avg), Avg < 25.0);
	}
	return true;
}

#endif
