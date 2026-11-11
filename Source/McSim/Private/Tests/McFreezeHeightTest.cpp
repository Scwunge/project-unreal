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

// Reference runs freeze4a, freeze4c-f (tools/refscripts/gen_freeze3.py): a chunk of isolated water holes on a two-high stone floor, in the
// rain on a superflat (sea level -63), 2000 ticks. Ice in the 128 holes: taiga (0.25) 0 at y -61 and -10, 6 at 30, 46 at 60, 32 to 47 from 100
// to 260; stony shore (0.2) 2 at -61, 43 at 60 and up; snowy beach (0.05) 46 at -61; plains (0.8) none at 120 to 300.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFreezeHeightTest, "McClient.McSim.FreezeHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFreezeHeightTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	struct FCase { const TCHAR* Biome; int32 Y; int32 Min; int32 Max; };
	const FCase Cases[] = { { TEXT("taiga"), -61, 0, 3 }, { TEXT("taiga"), -10, 0, 3 }, { TEXT("taiga"), 60, 30, 128 }, { TEXT("taiga"), 200, 30, 128 },
		{ TEXT("stony_shore"), -61, 0, 6 }, { TEXT("stony_shore"), 60, 30, 128 }, { TEXT("snowy_beach"), -61, 30, 128 }, { TEXT("plains"), 120, 0, 0 }, { TEXT("plains"), 300, 0, 0 } };
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	for (const FCase& Case : Cases)
	{
		FMcBlockWorld World{FMcDimensionType()};
		TUniquePtr<FMcChunk> Chunk = World.MakeChunk(FIntPoint(0, 0));
		Chunk->FillBiome(FMcBiomeRegistry::Get().Find(Case.Biome).Get(0));
		World.AddChunk(MoveTemp(Chunk));
		FMcSimulation Sim(World);
		Sim.SetTickingChunks({ FIntPoint(0, 0) });
		Sim.SetSeaLevel(-63);
		for (int32 X = 0; X < 16; ++X) for (int32 Z = 0; Z < 16; ++Z)
		{
			World.SetBlock(FIntVector(X, Case.Y - 1, Z), State(TEXT("minecraft:stone")));
			World.SetBlock(FIntVector(X, Case.Y, Z), State((X + Z) % 2 == 0 ? TEXT("minecraft:water") : TEXT("minecraft:stone")));
		}
		Sim.SetWeather(EMcWeather::Rain, 1000000);
		for (int32 Tick = 0; Tick < 2000; ++Tick) Sim.Tick();
		int32 Ice = 0;
		for (int32 X = 0; X < 16; ++X) for (int32 Z = 0; Z < 16; ++Z)
		{
			Ice += Registry.GetBlockOf(World.GetBlock(FIntVector(X, Case.Y, Z))).Name == TEXT("minecraft:ice");
		}
		AddInfo(FString::Printf(TEXT("%s y=%d: %d ice"), Case.Biome, Case.Y, Ice));
		TestTrue(*FString::Printf(TEXT("%s y=%d ice %d in %d..%d"), Case.Biome, Case.Y, Ice, Case.Min, Case.Max), Ice >= Case.Min && Ice <= Case.Max);
	}
	return true;
}

#endif
