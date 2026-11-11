#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// The ice and snow field the vanilla reference ran in the rain (tools/refscripts/gen_icesnow.py, E:\mc-reference\scripts\icesnow.txt):
// four chunks side by side, stone two high; the second and third have water in every other cell of the top layer (isolated cells),
// the first three are snowy plains, the fourth plains. Block counts per chunk every 100 ticks for 4000, logged as
// "mcice chunk=N t=T ..." lines to compare with the "mcref box" lines (tools/refscripts/icecmp.py).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcIceSnowFieldTest, "McClient.McSim.IceSnowField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcIceSnowFieldTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcBiomeId Plains = FMcBiomeRegistry::Get().Find(TEXT("plains")).Get(0);
	const FMcBiomeId Snowy = FMcBiomeRegistry::Get().Find(TEXT("snowy_plains")).Get(0);
	FMcBlockWorld World{FMcDimensionType()};
	TArray<FIntPoint> Chunks;
	for (int32 X = 0; X < 4; ++X)
	{
		TUniquePtr<FMcChunk> Chunk = World.MakeChunk(FIntPoint(X, 0));
		Chunk->FillBiome(X < 3 ? Snowy : Plains);
		World.AddChunk(MoveTemp(Chunk));
		Chunks.Add(FIntPoint(X, 0));
	}
	FMcSimulation Sim(World);
	Sim.SetTickingChunks(Chunks);
	for (int32 X = 0; X < 64; ++X)
	{
		for (int32 Z = 0; Z < 16; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
			const bool bWater = (X / 16 == 1 || X / 16 == 2) && ((X + Z) % 2 == 0);
			World.SetBlock(FIntVector(X, 1, Z), State(bWater ? TEXT("minecraft:water") : TEXT("minecraft:stone")));
		}
	}
	Sim.SetWeather(EMcWeather::Rain, 1000000);
	for (int32 Step = 0; Step <= 40; ++Step)
	{
		for (int32 Chunk = 0; Chunk < 4; ++Chunk)
		{
			TMap<FString, int32> Counts;
			for (int32 X = Chunk * 16; X < Chunk * 16 + 16; ++X) for (int32 Z = 0; Z < 16; ++Z) for (int32 Y = 1; Y <= 2; ++Y)
			{
				const FMcStateId Block = World.GetBlock(FIntVector(X, Y, Z));
				if (!Registry.IsAir(Block)) Counts.FindOrAdd(Registry.GetBlockOf(Block).Name.ToString() + (Registry.GetBlockOf(Block).Name == TEXT("minecraft:snow") ? FString::Printf(TEXT("[layers=%s]"), *Registry.GetPropertyValue(Block, TEXT("layers")).ToString()) : FString()))++;
			}
			FString Out;
			for (const TPair<FString, int32>& Pair : Counts) Out += FString::Printf(TEXT("%s=%d "), *Pair.Key, Pair.Value);
			UE_LOG(LogTemp, Display, TEXT("mcice chunk=%d t=%d %s"), Chunk, Step * 100, *Out);
		}
		for (int32 Tick = 0; Tick < 100; ++Tick) Sim.Tick();
	}
	return true;
}

#endif
