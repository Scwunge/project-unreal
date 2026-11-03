#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// The field the vanilla reference lit (E:\mc-reference\scripts\fire1.txt): 32 cells in each of four rows, fire on stone
// with nothing to burn, then fire on netherrack beside planks, wool and leaves. Block counts per row every 20 ticks,
// logged as "mcfire row=Z t=T ..." lines to compare with the "mcref box" lines.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFireFieldTest, "McClient.McSim.FireField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFireFieldTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	TArray<FIntPoint> Chunks;
	for (int32 Z = -1; Z <= 1; ++Z) for (int32 X = -1; X <= 8; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); Chunks.Add(FIntPoint(X, Z)); }
	FMcSimulation Sim(World);
	Sim.SetTickingChunks(Chunks);
	for (int32 X = -2; X <= 128; ++X) for (int32 Z = -2; Z <= 16; ++Z) World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
	// Ground y 0 (the reference's -62 and -61 together), fire at y 1.
	for (int32 I = 0; I < 32; ++I)
	{
		const int32 X = 4 * I;
		World.SetBlock(FIntVector(X, 1, 0), State(TEXT("minecraft:fire")));
		const TCHAR* Fuels[3] = { TEXT("minecraft:oak_planks"), TEXT("minecraft:white_wool"), TEXT("minecraft:oak_leaves[persistent=true]") };
		for (int32 Row = 0; Row < 3; ++Row)
		{
			World.SetBlock(FIntVector(X, 0, 4 + 4 * Row), State(TEXT("minecraft:netherrack")));
			World.SetBlock(FIntVector(X, 1, 4 + 4 * Row), State(TEXT("minecraft:fire")));
			World.SetBlock(FIntVector(X + 1, 1, 4 + 4 * Row), State(Fuels[Row]));
		}
	}
	for (int32 Step = 1; Step <= 31; ++Step)
	{
		for (int32 Tick = 0; Tick < 20; ++Tick) Sim.Tick();
		for (const int32 Z : { 0, 4, 8, 12 })
		{
			TMap<FString, int32> Counts;
			for (int32 X = 0; X <= 128; ++X)
			{
				const FMcStateId Block = World.GetBlock(FIntVector(X, 1, Z));
				if (!Registry.IsAir(Block)) Counts.FindOrAdd(Registry.GetBlockOf(Block).Name.ToString())++;
			}
			FString Out;
			for (const TPair<FString, int32>& Pair : Counts) Out += FString::Printf(TEXT("%s=%d "), *Pair.Key, Pair.Value);
			UE_LOG(LogTemp, Display, TEXT("mcfire row=%d t=%d %s"), Z, Step * 20, *Out);
		}
	}
	return true;
}

#endif
