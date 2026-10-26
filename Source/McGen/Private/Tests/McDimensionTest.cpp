#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McNoiseGenerator.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDimensionTest, "McClient.McGen.Dimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDimensionTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const struct { const TCHAR* Settings; const TCHAR* Preset; } Dimensions[] = {
		{ TEXT("minecraft:nether"), TEXT("minecraft:nether") },
		{ TEXT("minecraft:end"), TEXT("minecraft:the_end") },
	};
	for (const auto& Dimension : Dimensions)
	{
		FMcNoiseSettings Settings;
		if (!FMcNoiseSettings::Load(Dimension.Settings, Settings))
		{
			AddError(FString::Printf(TEXT("%s settings don't load"), Dimension.Settings));
			continue;
		}
		const FMcNoiseGenerator Generator(Settings, 7, Dimension.Preset);
		if (!Generator.IsValid())
		{
			AddError(FString::Printf(TEXT("%s: the noise generator isn't valid"), Dimension.Settings));
			continue;
		}
		TArray<TUniquePtr<FMcChunk>> Chunks;
		for (int32 Index = 0; Index < 25; ++Index)
		{
			Chunks.Add(MakeUnique<FMcChunk>(FIntPoint(Index % 5 - 2, Index / 5 - 2), Settings.MinY, Settings.Height / 16));
			Generator.Generate(*Chunks.Last());
		}
		TMap<FString, int32> Counts;
		for (const TUniquePtr<FMcChunk>& Chunk : Chunks)
		{
			for (int32 Y = Chunk->GetMinY(); Y <= Chunk->GetMaxY(); ++Y)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 X = 0; X < 16; ++X)
					{
						const FMcStateId State = Chunk->GetBlock(X, Y, Z);
						if (!FMcBlockRegistry::Get().IsAir(State))
						{
							++Counts.FindOrAdd(FMcBlockRegistry::Get().GetBlockOf(State).Name.ToString());
						}
					}
				}
			}
		}
		Counts.ValueSort([](int32 A, int32 B) { return A > B; });
		FString Top;
		int32 Shown = 0;
		for (const TPair<FString, int32>& Pair : Counts)
		{
			Top += FString::Printf(TEXT(" %s %d"), *Pair.Key, Pair.Value);
			if (++Shown == 8)
			{
				break;
			}
		}
		AddInfo(FString::Printf(TEXT("%s: top blocks in 25 chunks:%s"), Dimension.Settings, *Top));
		TestTrue(FString::Printf(TEXT("%s makes terrain"), Dimension.Settings), Counts.Num() > (FString(Dimension.Preset) == TEXT("minecraft:the_end") ? 0 : 1));

		// Decoration.
		FMcFeatureLibrary Features(Settings.SeaLevel, Settings.MinY, Settings.Height);
		Features.LoadBiomes();
		FMcChunk* Around[9];
		for (int32 Slot = 0; Slot < 9; ++Slot)
		{
			Around[Slot] = Chunks[(1 + Slot / 3) * 5 + 1 + Slot % 3].Get();
		}
		FMcGenRegion Region(FIntPoint(0, 0), Around);
		Features.Decorate(Region, 7);
		AddInfo(FString::Printf(TEXT("%s: decorated"), Dimension.Settings));

		if (FString(Dimension.Preset) == TEXT("minecraft:the_end"))
		{
			// The central island is end stone around the origin and the biome there is the_end.
			TestEqual(TEXT("the origin is in the_end"), FMcBiomeRegistry::Get().GetName(Generator.GetBiome(0, 64, 0)), FName(TEXT("minecraft:the_end")));
			TestTrue(TEXT("the central island has a surface"), Generator.FindSurfaceY(0, 0) > 40);
			// Outer islands: scan the island function out to 3000 blocks for ground and for each outer biome.
			const int32 Erosion = Generator.GetRouterFunction(TEXT("erosion"));
			FMcDensityContext Context(Generator.GetGraph());
			double Highest = -1.0;
			TSet<FName> Biomes;
			for (int32 Z = -3000; Z <= 3000; Z += 16)
			{
				for (int32 X = 1100; X <= 3000; X += 16)
				{
					Context.SetPos(X, 0, Z);
					Highest = FMath::Max(Highest, Generator.GetGraph().Evaluate(Erosion, Context));
					Biomes.Add(FMcBiomeRegistry::Get().GetName(Generator.GetBiome(X, 64, Z)));
				}
			}
			TestTrue(TEXT("outer islands exist"), Highest > 0.0);
			TestTrue(TEXT("outer biomes include end_highlands"), Biomes.Contains(FName(TEXT("minecraft:end_highlands"))));
			TestTrue(TEXT("outer biomes include end_barrens"), Biomes.Contains(FName(TEXT("minecraft:end_barrens"))));
			AddInfo(FString::Printf(TEXT("the end: highest island density %.3f, %d biomes out there"), Highest, Biomes.Num()));
		}
	}
	return true;
}

#endif
