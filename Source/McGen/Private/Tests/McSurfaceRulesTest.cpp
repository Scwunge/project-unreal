#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McChunk.h"
#include "McDensityFunction.h"
#include "McNoiseGenerator.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSurfaceRulesTest, "McClient.McGen.SurfaceRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSurfaceRulesTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Blocks.EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	TestEqual(TEXT("the overworld names its surface rule"), Settings.SurfaceRule, FString(TEXT("minecraft:overworld")));
	const FMcNoiseGenerator Generator(Settings, 12345, TEXT("minecraft:overworld"));
	if (!TestTrue(TEXT("generator with surface rules is valid"), Generator.IsValid()))
	{
		return false;
	}

	const auto State = [&Blocks](const TCHAR* Name) { return Blocks.FindDefaultState(Name).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Air = State(TEXT("minecraft:air"));
	const FMcStateId Water = State(TEXT("minecraft:water"));
	const FMcStateId Bedrock = State(TEXT("minecraft:bedrock"));
	const FMcStateId Deepslate = State(TEXT("minecraft:deepslate"));

	// A patch of chunks: count what tops dry land and the sea floor, and what's at the bottom and deep down.
	TMap<FMcStateId, int32> LandTops;
	TMap<FMcStateId, int32> SeaFloors;
	TMap<int32, int32> FloorGaps;  // preliminary surface minus the sea floor, in steps of 8
	int32 BedrockFloor = 0;
	int32 Columns = 0;
	int32 DeepStone = 0;
	int32 DeepSlate = 0;
	int32 VeinBlocks = 0;
	const FMcStateId VeinStates[] = {
		State(TEXT("minecraft:granite")), State(TEXT("minecraft:copper_ore")), State(TEXT("minecraft:tuff")), State(TEXT("minecraft:iron_ore")),
		State(TEXT("minecraft:raw_copper_block")), State(TEXT("minecraft:raw_iron_block")),
	};
	double Milliseconds = 0.0;
	for (int32 ChunkIndex = 0; ChunkIndex < 16; ++ChunkIndex)
	{
		FMcChunk Chunk(FIntPoint((ChunkIndex % 4) * 7 - 10, (ChunkIndex / 4) * 7 - 10), -64, 24);
		const double Start = FPlatformTime::Seconds();
		Generator.Generate(Chunk);
		Milliseconds += (FPlatformTime::Seconds() - Start) * 1000.0;
		for (int32 Z = 0; Z < 16; ++Z)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				++Columns;
				BedrockFloor += Chunk.GetBlock(X, -64, Z) == Bedrock ? 1 : 0;
				for (int32 Y = -40; Y < -10; ++Y)
				{
					const FMcStateId Block = Chunk.GetBlock(X, Y, Z);
					DeepStone += Block == Stone ? 1 : 0;
					DeepSlate += Block == Deepslate ? 1 : 0;
				}
				for (int32 Y = -64; Y < 60; ++Y)
				{
					const FMcStateId Block = Chunk.GetBlock(X, Y, Z);
					for (const FMcStateId Vein : VeinStates)
					{
						VeinBlocks += Block == Vein ? 1 : 0;
					}
				}
				for (int32 Y = 319; Y > -64; --Y)
				{
					const FMcStateId Block = Chunk.GetBlock(X, Y, Z);
					if (Block == Air)
					{
						continue;
					}
					if (Block == Water)
					{
						for (int32 Below = Y - 1; Below > -64; --Below)
						{
							if (Chunk.GetBlock(X, Below, Z) != Water)
							{
								const int32 Gap = FMath::RoundToInt32(Generator.GetPreliminarySurface(Chunk.GetPos().X * 16 + X, Chunk.GetPos().Y * 16 + Z)) - Below;
								FloorGaps.FindOrAdd(FMath::Clamp(Gap / 8, -4, 8))++;
								SeaFloors.FindOrAdd(Chunk.GetBlock(X, Below, Z))++;
								break;
							}
						}
					}
					else
					{
						LandTops.FindOrAdd(Block)++;
					}
					break;
				}
			}
		}
	}

	const auto Describe = [&Blocks](TMap<FMcStateId, int32> Counts)
	{
		Counts.ValueSort([](int32 A, int32 B) { return A > B; });
		FString List;
		for (const TPair<FMcStateId, int32>& Entry : Counts)
		{
			List += FString::Printf(TEXT("%s %d, "), *Blocks.Describe(Entry.Key).RightChop(10), Entry.Value);
		}
		return List;
	};
	AddInfo(TEXT("Land tops: ") + Describe(LandTops));
	AddInfo(TEXT("Sea floors: ") + Describe(SeaFloors));
	FloorGaps.KeySort(TLess<int32>());
	FString Gaps;
	for (const TPair<int32, int32>& Entry : FloorGaps)
	{
		Gaps += FString::Printf(TEXT("%d..%d: %d, "), Entry.Key * 8, Entry.Key * 8 + 7, Entry.Value);
	}
	AddInfo(TEXT("Sea floor below the preliminary surface: ") + Gaps);
	AddInfo(FString::Printf(TEXT("16 chunks in %.0f ms; %d ore vein blocks"), Milliseconds, VeinBlocks));

	// How well the preliminary surface tracks the terrain under the sea: solid just below it, open just above.
	{
		const FMcDensityGraph& Graph = Generator.GetGraph();
		const int32 Final = Generator.GetRouterFunction(TEXT("final_density"));
		FMcDensityContext Context(Graph);
		int32 Sea = 0;
		int32 SolidBelow[4] = {};
		for (int32 Index = 0; Index < 1600; ++Index)
		{
			const int32 X = (Index % 40) * 97 - 2000;
			const int32 Z = (Index / 40) * 97 - 2000;
			const int32 Surface = FMath::FloorToInt32(Generator.GetPreliminarySurface(X, Z));
			if (Surface >= Settings.SeaLevel - 5)
			{
				continue;
			}
			++Sea;
			const int32 Depths[4] = { 2, 8, 16, 32 };
			for (int32 D = 0; D < 4; ++D)
			{
				Context.SetPos(X, Surface - Depths[D], Z);
				SolidBelow[D] += Graph.Evaluate(Final, Context) > 0.0 ? 1 : 0;
			}
		}
		AddInfo(FString::Printf(TEXT("Under the sea (%d columns), solid at 2/8/16/32 below the preliminary surface: %d %d %d %d"),
			Sea, SolidBelow[0], SolidBelow[1], SolidBelow[2], SolidBelow[3]));
	}

	TestEqual(TEXT("bedrock covers the bottom layer"), BedrockFloor, Columns);
	TestTrue(FString::Printf(TEXT("deepslate below y 0 (%d deepslate, %d stone)"), DeepSlate, DeepStone), DeepSlate > DeepStone * 20);
	int32 Land = 0;
	for (const TPair<FMcStateId, int32>& Entry : LandTops)
	{
		Land += Entry.Value;
	}
	TestTrue(TEXT("dry land mostly isn't bare stone"), LandTops.FindRef(Stone) < Land / 2);
	TestTrue(TEXT("grass tops some dry land"), LandTops.FindRef(State(TEXT("minecraft:grass_block"))) > 0);
	int32 Floors = 0;
	for (const TPair<FMcStateId, int32>& Entry : SeaFloors)
	{
		Floors += Entry.Value;
	}
	TestTrue(TEXT("no grass under water"), SeaFloors.FindRef(State(TEXT("minecraft:grass_block"))) == 0);
	TestTrue(TEXT("sea floors are mostly sediment"), SeaFloors.FindRef(Stone) < Floors / 2 + 1);
	return true;
}

#endif
