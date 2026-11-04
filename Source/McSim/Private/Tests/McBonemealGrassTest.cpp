#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Bone meal on a grass block with air above, as the vanilla reference did it (run bonemeal2, tools/refscripts/gen_bonemeal2.py): grass floor,
// 20 meals 14 blocks apart with a dispenser block beside each. Plants per Chebyshev distance from the meal, over the 20 meals, logged as
// "mcgrass d=D n=N" for comparison. Vanilla (20 meals): d0 31, d1 137, d2 128, d3 61, d4 21, d5 5; short grass 278, flowers 55, tall grass 25 pairs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBonemealGrassTest, "McClient.McSim.BonemealGrass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBonemealGrassTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	TMap<int32, int32> ByDistance;
	TMap<FString, int32> ByKind;
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 X = 0; X < 19; ++X) for (int32 Z = 0; Z < 2; ++Z) World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
	FMcSimulation Sim(World);
	for (int32 X = 0; X < 19 * 16; ++X) for (int32 Z = 0; Z < 32; ++Z) World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:grass_block")));
	for (int32 Run = 0; Run < 20; ++Run)
	{
		const FIntVector Target(20 + 14 * Run, 0, 16);
		World.SetBlock(Target + FIntVector(1, 0, 0), State(TEXT("minecraft:dispenser[facing=west]")));
		Sim.Bonemeal(Target);
		for (int32 X = Target.X - 6; X <= Target.X + 6; ++X) for (int32 Z = 10; Z <= 22; ++Z) for (int32 Y = 1; Y <= 2; ++Y)
		{
			const FMcStateId Block = World.GetBlock(FIntVector(X, Y, Z));
			if (Registry.IsAir(Block)) continue;
			ByDistance.FindOrAdd(FMath::Max(FMath::Abs(X - Target.X), FMath::Abs(Z - Target.Z)))++;
			ByKind.FindOrAdd(Registry.GetBlockOf(Block).Name.ToString() + Registry.GetPropertyValue(Block, TEXT("half")).ToString())++;
		}
	}
	for (int32 D = 0; D < 12; ++D) UE_LOG(LogTemp, Display, TEXT("mcgrass d=%d n=%d"), D, ByDistance.FindRef(D));
	for (const TPair<FString, int32>& Pair : ByKind) UE_LOG(LogTemp, Display, TEXT("mcgrass kind %s n=%d"), *Pair.Key, Pair.Value);
	int32 Total = 0;
	for (const TPair<int32, int32>& Pair : ByDistance) Total += Pair.Value;
	TestTrue(TEXT("about as many plants as vanilla (383 in 20 meals, upper halves counted)"), Total > 330 && Total < 440);
	return true;
}

#endif
