#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// What the vanilla reference saw (tools/refscripts/gen_icemelt.py, run icemelt): at random tick speed 100, with a light block of level
// N beside each, ice melted into water from N=12 and a snow layer from N=13, a snow block never, and none of them below.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcIceMeltTest, "McClient.McSim.IceMelt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcIceMeltTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	TArray<FIntPoint> Chunks;
	for (int32 Z = 0; Z < 11; ++Z) for (int32 X = 0; X < 6; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); Chunks.Add(FIntPoint(X, Z)); }
	FMcSimulation Sim(World);
	Sim.SetTickingChunks(Chunks);
	Sim.SetRandomTickSpeed(100);
	const int32 Levels[] = { 8, 9, 10, 11, 12, 13, 14, 15 };
	const int32 Copies = 30;
	for (int32 X = 0; X < 96; ++X) for (int32 Z = 0; Z < 160; ++Z) World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
	for (int32 Row = 0; Row < 8; ++Row)
	{
		const int32 Z = 20 * Row + 2;
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			const int32 X = 3 * Copy;
			World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:snow[layers=1]")));
			World.SetBlock(FIntVector(X + 1, 1, Z + 4), State(TEXT("minecraft:snow_block")));
			World.SetBlock(FIntVector(X + 1, 1, Z + 8), State(TEXT("minecraft:ice")));
			const FString Light = FString::Printf(TEXT("minecraft:light[level=%d]"), Levels[Row]);
			World.SetBlock(FIntVector(X, 1, Z + 1), State(Light));
			World.SetBlock(FIntVector(X + 1, 1, Z + 5), State(Light));
			World.SetBlock(FIntVector(X + 1, 1, Z + 9), State(Light));
		}
	}
	for (int32 Tick = 0; Tick < 200; ++Tick)
	{
		Sim.Tick();
	}
	for (int32 Row = 0; Row < 8; ++Row)
	{
		const int32 Z = 20 * Row + 2;
		int32 Snow = 0, SnowBlock = 0, Ice = 0;
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			const int32 X = 3 * Copy;
			Snow += Registry.GetBlockOf(World.GetBlock(FIntVector(X, 1, Z))).Name == TEXT("minecraft:snow");
			SnowBlock += Registry.GetBlockOf(World.GetBlock(FIntVector(X + 1, 1, Z + 4))).Name == TEXT("minecraft:snow_block");
			Ice += Registry.GetBlockOf(World.GetBlock(FIntVector(X + 1, 1, Z + 8))).Name == TEXT("minecraft:ice");
		}
		UE_LOG(LogTemp, Display, TEXT("mcmelt light=%d snow=%d snow_block=%d ice=%d"), Levels[Row], Snow, SnowBlock, Ice);
		TestEqual(*FString::Printf(TEXT("snow blocks stay at %d"), Levels[Row]), SnowBlock, Copies);
		// 200 ticks leave about one in a hundred of the melting ones untouched by chance (the reference's last two of 30 went by 200 ticks).
		if (Levels[Row] >= 13) TestTrue(*FString::Printf(TEXT("snow layers melt at %d"), Levels[Row]), Snow <= 2); else TestEqual(*FString::Printf(TEXT("snow layers stay at %d"), Levels[Row]), Snow, Copies);
		if (Levels[Row] >= 12) TestTrue(*FString::Printf(TEXT("ice melts at %d"), Levels[Row]), Ice <= 2); else TestEqual(*FString::Printf(TEXT("ice stays at %d"), Levels[Row]), Ice, Copies);
	}
	return true;
}

#endif
