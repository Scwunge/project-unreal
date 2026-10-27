#include "McBiomeRegistry.h"
#include "McBlockEntity.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWeatherTest, "McClient.McSim.Weather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWeatherTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Air = State(TEXT("minecraft:air"));
	const FMcBiomeId Plains = FMcBiomeRegistry::Get().Find(TEXT("plains")).Get(0);
	const FMcBiomeId Snowy = FMcBiomeRegistry::Get().Find(TEXT("snowy_plains")).Get(0);

	// Stone at y 0 under 3x3 chunks of air, all in one biome.
	const auto MakeWorld = [&](FMcBlockWorld& World, FMcBiomeId Biome, FMcStateId Floor)
	{
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				TUniquePtr<FMcChunk> Chunk = World.MakeChunk(FIntPoint(X, Z));
				Chunk->FillBiome(Biome);
				World.AddChunk(MoveTemp(Chunk));
			}
		}
		for (int32 X = -16; X <= 31; ++X)
		{
			for (int32 Z = -16; Z <= 31; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Floor);
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
	const auto Name = [&](FMcBlockWorld& World, const FIntVector& Pos) { return Registry.GetBlockOf(World.GetBlock(Pos)).Name.ToString(); };
	const auto Property = [&](FMcBlockWorld& World, const FIntVector& Pos, const TCHAR* Key) { return Registry.GetPropertyValue(World.GetBlock(Pos), Key).ToString(); };

	{
		// The weather runs on a timer, and the sun follows the day clock.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World, Plains, Stone);
		FMcSimulation Sim(World);
		TestTrue(TEXT("it starts clear"), Sim.GetWeather() == EMcWeather::Clear);
		Sim.SetWeather(EMcWeather::Rain, 100);
		TestTrue(TEXT("rain"), Sim.GetWeather() == EMcWeather::Rain);
		Run(Sim, 99);
		TestTrue(TEXT("still rain a tick before it ends"), Sim.GetWeather() == EMcWeather::Rain);
		Run(Sim, 1);
		TestTrue(TEXT("clear when the time is up"), Sim.GetWeather() == EMcWeather::Clear);
		Sim.SetWeather(EMcWeather::Thunder, 50);
		TestTrue(TEXT("thunder"), Sim.GetWeather() == EMcWeather::Thunder);
		Run(Sim, 50);
		TestTrue(TEXT("and it ends"), Sim.GetWeather() == EMcWeather::Clear);

		Sim.SetWeather(EMcWeather::Clear, 100000);
		Sim.SetDayTime(6000);
		TestEqual(TEXT("noon, full sun"), Sim.GetSunLevel(), 15);
		Sim.SetDayTime(18000);
		TestEqual(TEXT("midnight, 4"), Sim.GetSunLevel(), 4);
		Sim.SetDayTime(6000);
		Sim.SetWeather(EMcWeather::Rain, 100000);
		TestEqual(TEXT("rain dims the noon sun"), Sim.GetSunLevel(), 12);
		Sim.SetWeather(EMcWeather::Thunder, 100000);
		TestEqual(TEXT("thunder dims it more"), Sim.GetSunLevel(), 10);
		Sim.SetDayTime(23999);
		Run(Sim, 2);
		TestEqual(TEXT("the day wraps"), static_cast<int32>(Sim.GetDayTime()), 1);
	}
	{
		// A daylight detector reads the sim's own clock: full at noon, nothing at midnight.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World, Plains, Stone);
		FMcSimulation Sim(World);
		Sim.SetWeather(EMcWeather::Clear, 1000000);
		Sim.SetDayTime(6000);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:daylight_detector[inverted=false,power=0]")));
		Run(Sim, 3);
		TestEqual(TEXT("noon reads 15"), Property(World, FIntVector(0, 1, 0), TEXT("power")), FString(TEXT("15")));
		Sim.SetDayTime(18000);
		Run(Sim, 25);
		TestEqual(TEXT("midnight reads 0"), Property(World, FIntVector(0, 1, 0), TEXT("power")), FString(TEXT("0")));
		Sim.SetDayTime(6000);
		Sim.SetWeather(EMcWeather::Thunder, 1000000);
		Run(Sim, 25);
		TestEqual(TEXT("a storm at noon reads less"), Property(World, FIntVector(0, 1, 0), TEXT("power")), FString(TEXT("10")));
	}
	{
		// Rain fills a cauldron in the open, a roof keeps it dry, and fair weather leaves it empty.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World, Plains, Stone);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:cauldron")));
		World.SetBlock(FIntVector(6, 1, 0), State(TEXT("minecraft:cauldron")));
		World.SetBlock(FIntVector(6, 3, 0), Stone);
		World.SetBlock(FIntVector(12, 1, 0), State(TEXT("minecraft:cauldron")));
		Sim.SetWeather(EMcWeather::Clear, 1000000);
		for (int32 Index = 0; Index < 300; ++Index)
		{
			Sim.RandomTickBlock(FIntVector(12, 1, 0));
		}
		TestEqual(TEXT("no rain, no water"), Name(World, FIntVector(12, 1, 0)), FString(TEXT("minecraft:cauldron")));
		Sim.SetWeather(EMcWeather::Rain, 1000000);
		for (int32 Index = 0; Index < 600; ++Index)
		{
			Sim.RandomTickBlock(FIntVector(0, 1, 0));
			Sim.RandomTickBlock(FIntVector(6, 1, 0));
		}
		TestEqual(TEXT("the open cauldron fills"), Name(World, FIntVector(0, 1, 0)), FString(TEXT("minecraft:water_cauldron")));
		TestEqual(TEXT("to the top"), Property(World, FIntVector(0, 1, 0), TEXT("level")), FString(TEXT("3")));
		TestEqual(TEXT("the roofed one stays empty"), Name(World, FIntVector(6, 1, 0)), FString(TEXT("minecraft:cauldron")));
		TestTrue(TEXT("rain falls in the open"), Sim.IsRainingAt(FIntVector(0, 1, 0)));
		TestFalse(TEXT("not under a roof"), Sim.IsRainingAt(FIntVector(6, 1, 0)));
	}
	{
		// Rain puts out fire in the open.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World, Plains, Stone);
		FMcSimulation Sim(World);
		Sim.SetWeather(EMcWeather::Rain, 1000000);
		TArray<FIntVector> Fires;
		for (int32 X = -15; X <= 30; X += 3)
		{
			Fires.Add(FIntVector(X, 1, 0));
			World.SetBlock(Fires.Last(), State(TEXT("minecraft:fire")));
		}
		Run(Sim, 1000);
		int32 Burning = 0;
		for (const FIntVector& Pos : Fires)
		{
			Burning += Name(World, Pos) == TEXT("minecraft:fire") ? 1 : 0;
		}
		TestEqual(TEXT("every fire is out"), Burning, 0);
	}
	{
		// Farmland is wet in the rain with no water near it.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World, Plains, Stone);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:farmland[moisture=0]")));
		Sim.SetWeather(EMcWeather::Clear, 1000000);
		Sim.RandomTickBlock(FIntVector(0, 1, 0));
		TestEqual(TEXT("dry farmland in fair weather"), Name(World, FIntVector(0, 1, 0)), FString(TEXT("minecraft:dirt")));
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:farmland[moisture=0]")));
		Sim.SetWeather(EMcWeather::Rain, 1000000);
		Sim.RandomTickBlock(FIntVector(0, 1, 0));
		TestEqual(TEXT("rain wets it"), Property(World, FIntVector(0, 1, 0), TEXT("moisture")), FString(TEXT("7")));
	}
	{
		// Cold freezes the edge of a pool and leaves it be in a warm biome; snow lays on the ground only in the cold.
		for (int32 Cold = 0; Cold < 2; ++Cold)
		{
			FMcBlockWorld World{FMcDimensionType()};
			MakeWorld(World, Cold ? Snowy : Plains, Stone);
			FMcSimulation Sim(World);
			Sim.SetTickingChunks({ FIntPoint(0, 0) });
			Sim.SetRandomTickSpeed(1);
			for (int32 X = 5; X <= 7; ++X)
			{
				for (int32 Z = 5; Z <= 7; ++Z)
				{
					World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:water[level=0]")));
				}
			}
			Sim.SetWeather(EMcWeather::Clear, 1000000);
			// A column is picked one tick in sixteen of 256, so give every pool block time to be picked.
			Run(Sim, 20000);
			if (Cold)
			{
				TestEqual(TEXT("the rim of the pool freezes"), Name(World, FIntVector(5, 0, 5)), FString(TEXT("minecraft:ice")));
				TestEqual(TEXT("then the middle"), Name(World, FIntVector(6, 0, 6)), FString(TEXT("minecraft:ice")));
			}
			else
			{
				TestEqual(TEXT("warm water stays water"), Name(World, FIntVector(5, 0, 5)), FString(TEXT("minecraft:water")));
			}
			int32 Snow = 0;
			for (int32 X = 0; X < 16; ++X)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					Snow += Name(World, FIntVector(X, 1, Z)) == TEXT("minecraft:snow") ? 1 : 0;
				}
			}
			TestEqual(TEXT("no snow in fair weather"), Snow, 0);

			Sim.SetWeather(EMcWeather::Rain, 1000000);
			Run(Sim, 6000);
			Snow = 0;
			int32 MaxLayers = 0;
			for (int32 X = 0; X < 16; ++X)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					if (Name(World, FIntVector(X, 1, Z)) == TEXT("minecraft:snow"))
					{
						++Snow;
						MaxLayers = FMath::Max(MaxLayers, FCString::Atoi(*Property(World, FIntVector(X, 1, Z), TEXT("layers"))));
					}
				}
			}
			if (Cold)
			{
				TestTrue(TEXT("snow settles on the ground"), Snow > 100);
				TestEqual(TEXT("one layer by the default rule"), MaxLayers, 1);
				Sim.SetSnowAccumulation(3);
				Run(Sim, 6000);
				MaxLayers = 0;
				for (int32 X = 0; X < 16; ++X)
				{
					for (int32 Z = 0; Z < 16; ++Z)
					{
						if (Name(World, FIntVector(X, 1, Z)) == TEXT("minecraft:snow"))
						{
							MaxLayers = FMath::Max(MaxLayers, FCString::Atoi(*Property(World, FIntVector(X, 1, Z), TEXT("layers"))));
						}
					}
				}
				TestEqual(TEXT("it piles up to the limit set"), MaxLayers, 3);
			}
			else
			{
				TestEqual(TEXT("rain is only rain in a warm biome"), Snow, 0);
			}
		}
	}
	{
		// Ice and snow melt beside a light, and stay as they are away from one.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World, Snowy, Stone);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(3, 1, 0), State(TEXT("minecraft:torch")));
		World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:ice")));
		World.SetBlock(FIntVector(4, 1, 0), State(TEXT("minecraft:snow[layers=2]")));
		World.SetBlock(FIntVector(20, 1, 0), State(TEXT("minecraft:ice")));
		World.SetBlock(FIntVector(20, 1, 3), State(TEXT("minecraft:snow[layers=2]")));
		for (const FIntVector& Pos : { FIntVector(2, 1, 0), FIntVector(4, 1, 0), FIntVector(20, 1, 0), FIntVector(20, 1, 3) })
		{
			Sim.RandomTickBlock(Pos);
		}
		TestEqual(TEXT("ice beside a torch melts to water"), Name(World, FIntVector(2, 1, 0)), FString(TEXT("minecraft:water")));
		TestEqual(TEXT("snow beside a torch melts away"), Name(World, FIntVector(4, 1, 0)), FString(TEXT("minecraft:air")));
		TestEqual(TEXT("ice in the dark stays"), Name(World, FIntVector(20, 1, 0)), FString(TEXT("minecraft:ice")));
		TestEqual(TEXT("snow in the dark stays"), Name(World, FIntVector(20, 1, 3)), FString(TEXT("minecraft:snow")));
	}
	{
		// A sapling needs light, goes through stage 0 and 1 and then grows a tree; bone meal hurries it.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World, Plains, State(TEXT("minecraft:grass_block")));
		FMcSimulation Sim(World);
		Sim.SetWeather(EMcWeather::Clear, 1000000);
		Sim.SetDayTime(18000);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:oak_sapling[stage=0]")));
		for (int32 Index = 0; Index < 300; ++Index)
		{
			Sim.RandomTickBlock(FIntVector(0, 1, 0));
		}
		TestEqual(TEXT("it doesn't grow in the dark"), Property(World, FIntVector(0, 1, 0), TEXT("stage")), FString(TEXT("0")));
		Sim.SetDayTime(6000);
		int32 Tries = 0;
		while (Property(World, FIntVector(0, 1, 0), TEXT("stage")) == TEXT("0") && ++Tries < 500)
		{
			Sim.RandomTickBlock(FIntVector(0, 1, 0));
		}
		TestEqual(TEXT("in the light it reaches stage 1"), Property(World, FIntVector(0, 1, 0), TEXT("stage")), FString(TEXT("1")));
		Tries = 0;
		while (Name(World, FIntVector(0, 1, 0)) == TEXT("minecraft:oak_sapling") && ++Tries < 500)
		{
			Sim.RandomTickBlock(FIntVector(0, 1, 0));
		}
		TestTrue(TEXT("then it is a trunk"), Name(World, FIntVector(0, 1, 0)).EndsWith(TEXT("_log")));
		int32 Leaves = 0;
		int32 Logs = 0;
		for (int32 Y = 1; Y < 20; ++Y)
		{
			for (int32 X = -6; X <= 6; ++X)
			{
				for (int32 Z = -6; Z <= 6; ++Z)
				{
					const FString Block = Name(World, FIntVector(X, Y, Z));
					Leaves += Block.EndsWith(TEXT("_leaves")) ? 1 : 0;
					Logs += Block.EndsWith(TEXT("_log")) ? 1 : 0;
				}
			}
		}
		TestTrue(TEXT("with leaves on it"), Leaves > 10);
		TestTrue(TEXT("and more than a stump"), Logs >= 3);

		// Bone meal on a birch sapling, until it grows (45% a time, and stage 0 first).
		World.SetBlock(FIntVector(12, 1, 12), State(TEXT("minecraft:birch_sapling[stage=0]")));
		int32 Meal = 0;
		while (Name(World, FIntVector(12, 1, 12)) == TEXT("minecraft:birch_sapling") && ++Meal < 200)
		{
			TestTrue(TEXT("bone meal is used up"), Sim.Bonemeal(FIntVector(12, 1, 12)));
		}
		TestEqual(TEXT("bone meal grows a birch"), Name(World, FIntVector(12, 1, 12)), FString(TEXT("minecraft:birch_log")));

		// Four spruce saplings in a square grow a big tree; one dark oak alone does nothing.
		for (int32 DX = 0; DX < 2; ++DX)
		{
			for (int32 DZ = 0; DZ < 2; ++DZ)
			{
				World.SetBlock(FIntVector(-12 + DX, 1, 12 + DZ), State(TEXT("minecraft:spruce_sapling[stage=1]")));
			}
		}
		Meal = 0;
		while (Name(World, FIntVector(-12, 1, 12)) == TEXT("minecraft:spruce_sapling") && ++Meal < 400)
		{
			Sim.Bonemeal(FIntVector(-11, 1, 13));
		}
		TestEqual(TEXT("a 2x2 of spruce grows"), Name(World, FIntVector(-12, 1, 12)), FString(TEXT("minecraft:spruce_log")));
		TestEqual(TEXT("with a trunk 2 wide"), Name(World, FIntVector(-11, 1, 13)), FString(TEXT("minecraft:spruce_log")));
		World.SetBlock(FIntVector(20, 1, 20), State(TEXT("minecraft:dark_oak_sapling[stage=1]")));
		for (int32 Index = 0; Index < 100; ++Index)
		{
			Sim.Bonemeal(FIntVector(20, 1, 20));
		}
		TestEqual(TEXT("one dark oak sapling can't"), Name(World, FIntVector(20, 1, 20)), FString(TEXT("minecraft:dark_oak_sapling")));
	}
	{
		// Bone meal on crops, short grass and grass blocks.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World, Plains, State(TEXT("minecraft:grass_block")));
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 0, 0), State(TEXT("minecraft:farmland[moisture=7]")));
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:wheat[age=0]")));
		TestTrue(TEXT("wheat takes bone meal"), Sim.Bonemeal(FIntVector(0, 1, 0)));
		TestTrue(TEXT("two to five stages at once"), FCString::Atoi(*Property(World, FIntVector(0, 1, 0), TEXT("age"))) >= 2);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Sim.Bonemeal(FIntVector(0, 1, 0));
		}
		TestEqual(TEXT("and ripe in the end"), Property(World, FIntVector(0, 1, 0), TEXT("age")), FString(TEXT("7")));
		TestFalse(TEXT("ripe wheat doesn't use it up"), Sim.Bonemeal(FIntVector(0, 1, 0)));

		World.SetBlock(FIntVector(5, 1, 5), State(TEXT("minecraft:short_grass")));
		TestTrue(TEXT("short grass takes it"), Sim.Bonemeal(FIntVector(5, 1, 5)));
		TestEqual(TEXT("and grows tall"), Name(World, FIntVector(5, 2, 5)), FString(TEXT("minecraft:tall_grass")));

		TestTrue(TEXT("grass takes it"), Sim.Bonemeal(FIntVector(10, 0, 10)));
		int32 Plants = 0;
		for (int32 X = -10; X <= 30; ++X)
		{
			for (int32 Z = -10; Z <= 30; ++Z)
			{
				const FString Block = Name(World, FIntVector(X, 1, Z));
				Plants += (Block == TEXT("minecraft:short_grass") || Block == TEXT("minecraft:dandelion") || Block == TEXT("minecraft:poppy")) && !(X == 5 && Z == 5) ? 1 : 0;
			}
		}
		TestTrue(TEXT("grass and flowers spread around it"), Plants >= 5);
	}
	return true;
}

#endif
