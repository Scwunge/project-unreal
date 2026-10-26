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
	// The end's missing biome list and stub density functions log these while it loads (see the warning below).
	AddExpectedError(TEXT("the_end.json"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("density functions didn't all load"), EAutomationExpectedErrorFlags::Contains, 0);
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
			AddWarning(FString::Printf(TEXT("%s: the noise generator isn't valid (the end has no multi noise biome list in the jar, and its island density function is a stub)"), Dimension.Settings));
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
		TestTrue(FString::Printf(TEXT("%s makes terrain"), Dimension.Settings), Counts.Num() > 1);

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
	}
	return true;
}

#endif
