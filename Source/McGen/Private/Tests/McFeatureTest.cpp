#include "Async/ParallelFor.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFeatureTest, "McClient.McGen.Feature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFeatureTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Blocks.EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const FMcNoiseGenerator Generator(Settings, 12345, TEXT("minecraft:overworld"));

	// Every biome's features parse; types not written yet are listed, not errors.
	FMcFeatureLibrary Library(Settings.SeaLevel, Settings.MinY, Settings.Height);
	const double LoadStart = FPlatformTime::Seconds();
	TestTrue(TEXT("biome features load"), Library.LoadBiomes());
	TestFalse(TEXT("no feature errors"), Library.HasErrors());
	AddInfo(FString::Printf(TEXT("features loaded in %.0f ms"), (FPlatformTime::Seconds() - LoadStart) * 1000.0));
	TArray<FString> Unsupported;
	for (const TPair<FString, int32>& Entry : Library.GetUnsupported())
	{
		Unsupported.Add(FString::Printf(TEXT("%s x%d"), *Entry.Key, Entry.Value));
	}
	Unsupported.Sort();
	AddInfo(TEXT("not written yet: ") + FString::Join(Unsupported, TEXT(", ")));

	// Decorate the middle chunks of a 5x5 patch of noise chunks (each needs its 8 neighbours generated).
	constexpr int32 Size = 5;
	TArray<TUniquePtr<FMcChunk>> Chunks;
	for (int32 Index = 0; Index < Size * Size; ++Index)
	{
		Chunks.Add(MakeUnique<FMcChunk>(FIntPoint(Index % Size, Index / Size), Settings.MinY, Settings.Height / 16));
		Generator.Generate(*Chunks.Last());
	}
	TMap<int32, int32> Before;
	const auto Count = [&Chunks](TMap<int32, int32>& Out)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		for (int32 Index : { 6, 7, 8, 11, 12, 13, 16, 17, 18 })
		{
			const FMcChunk& Chunk = *Chunks[Index];
			for (int32 Y = Chunk.GetMinY(); Y <= Chunk.GetMaxY(); ++Y)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 X = 0; X < 16; ++X)
					{
						Out.FindOrAdd(Registry.GetState(Chunk.GetBlock(X, Y, Z)).Block)++;
					}
				}
			}
		}
	};
	Count(Before);
	const double DecorateStart = FPlatformTime::Seconds();
	for (int32 Index : { 6, 7, 8, 11, 12, 13, 16, 17, 18 })
	{
		const int32 CX = Index % Size;
		const int32 CZ = Index / Size;
		FMcChunk* Around[9];
		for (int32 DZ = -1; DZ <= 1; ++DZ)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				Around[(DZ + 1) * 3 + DX + 1] = Chunks[(CZ + DZ) * Size + CX + DX].Get();
			}
		}
		FMcGenRegion Region(FIntPoint(CX, CZ), Around);
		Library.Decorate(Region, 12345);
	}
	AddInfo(FString::Printf(TEXT("9 chunks decorated in %.0f ms"), (FPlatformTime::Seconds() - DecorateStart) * 1000.0));

	// The game decorates on several workers at once, regions apart. That has to give what one thread gives.
	{
		constexpr int32 Across = 9;
		const auto Decorated = [&](bool bTogether)
		{
			TArray<TUniquePtr<FMcChunk>> Patch;
			for (int32 Index = 0; Index < Across * Across; ++Index)
			{
				Patch.Add(MakeUnique<FMcChunk>(FIntPoint(Index % Across + 200, Index / Across + 200), Settings.MinY, Settings.Height / 16));
				Generator.Generate(*Patch.Last());
			}
			// The middle of each of the 3x3 blocks of chunks, so no two regions share one.
			ParallelFor(9, [&](int32 Job)
			{
				const int32 CX = Job % 3 * 3 + 1;
				const int32 CZ = Job / 3 * 3 + 1;
				FMcChunk* Around[9];
				for (int32 Index = 0; Index < 9; ++Index)
				{
					Around[Index] = Patch[(CZ + Index / 3 - 1) * Across + CX + Index % 3 - 1].Get();
				}
				FMcGenRegion Region(FIntPoint(CX + 200, CZ + 200), Around);
				Library.Decorate(Region, 12345);
			}, bTogether ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
			TArray<FMcStateId> States;
			for (const TUniquePtr<FMcChunk>& Chunk : Patch)
			{
				for (int32 Y = Chunk->GetMinY(); Y <= Chunk->GetMaxY(); ++Y)
				{
					for (int32 Z = 0; Z < 16; ++Z)
					{
						for (int32 X = 0; X < 16; ++X)
						{
							States.Add(Chunk->GetBlock(X, Y, Z));
						}
					}
				}
			}
			return States;
		};
		// Together first, so the threads are the ones to fill the structure caches.
		const TArray<FMcStateId> Together = Decorated(true);
		TestTrue(TEXT("Decorating regions apart on several threads gives what one thread gives"), Together == Decorated(false));
	}
	TMap<int32, int32> After;
	Count(After);

	TArray<FString> Changes;
	for (const TPair<int32, int32>& Entry : After)
	{
		const int32 Delta = Entry.Value - Before.FindRef(Entry.Key);
		if (Delta != 0)
		{
			Changes.Add(FString::Printf(TEXT("%s %+d"), *Blocks.GetBlock(Entry.Key).Name.ToString(), Delta));
		}
	}
	Changes.Sort();
	AddInfo(TEXT("changes: ") + FString::Join(Changes, TEXT(", ")));

	const auto Gained = [&](const TCHAR* Name)
	{
		const FMcBlock* Block = Blocks.FindBlock(Name);
		return Block ? After.FindRef(Block->Index) - Before.FindRef(Block->Index) : 0;
	};
	TestTrue(TEXT("coal ore placed"), Gained(TEXT("minecraft:coal_ore")) + Gained(TEXT("minecraft:deepslate_coal_ore")) > 0);
	TestTrue(TEXT("iron ore placed"), Gained(TEXT("minecraft:iron_ore")) + Gained(TEXT("minecraft:deepslate_iron_ore")) > 0);
	TestTrue(TEXT("granite, diorite or andesite blobs placed"), Gained(TEXT("minecraft:granite")) + Gained(TEXT("minecraft:diorite")) + Gained(TEXT("minecraft:andesite")) > 0);
	return true;
}

#endif
