#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Growth rates from the wiki (Tutorial:Crop farming, Nether Wart, Cocoa Beans), measured over many random ticks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGrowthTest, "McClient.McSim.Growth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGrowthTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	FMcSimulation Sim(World);
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	for (int32 X = -6; X <= 6; ++X)
	{
		for (int32 Z = -6; Z <= 6; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Stone);
		}
	}
	// Plenty of light: a glowstone beside the plot.
	World.SetBlock(FIntVector(4, 3, 4), State(TEXT("minecraft:glowstone")));

	const int32 Trials = 14000;
	// How many of Trials random ticks on a plant at (0,2,0) change it (to anything else), put back each time.
	const auto Rate = [&](const TCHAR* Plant) -> double
	{
		const FMcStateId Start = State(Plant);
		int32 Changed = 0;
		for (int32 Index = 0; Index < Trials; ++Index)
		{
			World.SetBlock(FIntVector(0, 2, 0), Start);
			Sim.RandomTickBlock(FIntVector(0, 2, 0));
			Changed += World.GetBlock(FIntVector(0, 2, 0)) != Start ? 1 : 0;
		}
		World.SetBlock(FIntVector(0, 2, 0), State(TEXT("minecraft:air")));
		return double(Changed) / Trials;
	};
	const auto Near = [&](const TCHAR* What, double Got, double Want)
	{
		// Five standard deviations.
		const double Sigma = FMath::Max(FMath::Sqrt(Want * (1.0 - Want) / Trials), 0.0005);
		TestTrue(*FString::Printf(TEXT("%s: %.4f vs %.4f"), What, Got, Want), FMath::Abs(Got - Want) < 5.0 * Sigma);
	};
	const auto Soil = [&](const TCHAR* Text, int32 DX, int32 DZ) { World.SetBlock(FIntVector(DX, 1, DZ), State(Text)); };
	const TCHAR* Wet = TEXT("minecraft:farmland[moisture=7]");
	const TCHAR* Dry = TEXT("minecraft:farmland[moisture=0]");

	Soil(Wet, 0, 0);
	Near(TEXT("wheat on lone wet farmland"), Rate(TEXT("minecraft:wheat[age=0]")), 1.0 / 7.0);
	Near(TEXT("beetroot, a third fewer"), Rate(TEXT("minecraft:beetroots[age=0]")), 1.0 / 7.0 * 2.0 / 3.0);
	Soil(Dry, 0, 0);
	Near(TEXT("carrots on lone dry farmland"), Rate(TEXT("minecraft:carrots[age=0]")), 1.0 / 13.0);
	for (int32 X = -1; X <= 1; ++X)
	{
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			Soil(Wet, X, Z);
		}
	}
	Near(TEXT("potatoes in a wet 3x3"), Rate(TEXT("minecraft:potatoes[age=0]")), 1.0 / 3.0);
	// The same plant diagonally halves the speed: 10 becomes 5, 1 in 6.
	World.SetBlock(FIntVector(1, 2, 1), State(TEXT("minecraft:potatoes[age=0]")));
	Near(TEXT("with a neighbour on the diagonal"), Rate(TEXT("minecraft:potatoes[age=0]")), 1.0 / 6.0);
	World.SetBlock(FIntVector(1, 2, 1), State(TEXT("minecraft:air")));
	Soil(Wet, 0, 0);
	for (int32 X = -1; X <= 1; ++X)
	{
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			if (X || Z)
			{
				Soil(TEXT("minecraft:dirt"), X, Z);
			}
		}
	}
	// A stem at its full age bears fruit on a side: 1 in 7 for the growth, then one of four sides, here all free.
	{
		const FMcStateId Stem = State(TEXT("minecraft:melon_stem[age=7]"));
		int32 Fruit = 0;
		for (int32 Index = 0; Index < Trials * 2; ++Index)
		{
			World.SetBlock(FIntVector(0, 2, 0), Stem);
			Sim.RandomTickBlock(FIntVector(0, 2, 0));
			if (World.GetBlock(FIntVector(0, 2, 0)) != Stem)
			{
				++Fruit;
				TestTrue(TEXT("the stem leans toward a melon"), Registry.GetBlockOf(World.GetBlock(FIntVector(0, 2, 0))).Name.ToString() == TEXT("minecraft:attached_melon_stem"));
				for (int32 Side = 0; Side < 4; ++Side)
				{
					const FIntVector Step[4] = { FIntVector(0, 2, -1), FIntVector(1, 2, 0), FIntVector(0, 2, 1), FIntVector(-1, 2, 0) };
					World.SetBlock(Step[Side], State(TEXT("minecraft:air")));
				}
			}
		}
		const double Want = 1.0 / 7.0; // 3.57% a side, four sides free
		const double Sigma = FMath::Sqrt(Want * (1.0 - Want) / (Trials * 2));
		TestTrue(*FString::Printf(TEXT("melons: %d of %d"), Fruit, Trials * 2), FMath::Abs(double(Fruit) / (Trials * 2) - Want) < 5.0 * Sigma);
	}
	World.SetBlock(FIntVector(0, 2, 0), State(TEXT("minecraft:air")));
	Soil(TEXT("minecraft:soul_sand"), 0, 0);
	Near(TEXT("nether wart"), Rate(TEXT("minecraft:nether_wart[age=0]")), 0.10);
	Near(TEXT("cocoa"), Rate(TEXT("minecraft:cocoa[age=0,facing=north]")), 0.20);
	// Dark: no growth.
	World.SetBlock(FIntVector(4, 3, 4), State(TEXT("minecraft:air")));
	Soil(Wet, 0, 0);
	Sim.SetDayTime(18000);
	Near(TEXT("wheat at night, unlit"), Rate(TEXT("minecraft:wheat[age=0]")), 0.0);
	return true;
}

#endif
