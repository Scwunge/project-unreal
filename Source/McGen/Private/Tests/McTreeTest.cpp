#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McNoiseGenerator.h"
#include "McCache.h"
#include "McRandom.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// How many blocks of a tag a chunk has, and how many logs have stone-like blocks on their sides at once.
	struct FTreeCounts
	{
		int32 Logs = 0;
		int32 Leaves = 0;
		int32 Vines = 0;
		int32 Nests = 0;
	};

	void Count(const FMcChunk& Chunk, FTreeCounts& Out)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcBlockTags& Tags = FMcBlockTags::Get();
		static const int32 Logs = Tags.Find(TEXT("minecraft:logs"));
		static const int32 Leaves = Tags.Find(TEXT("minecraft:leaves"));
		static const int32 Vine = Registry.FindBlock(TEXT("minecraft:vine"))->Index;
		static const int32 Nest = Registry.FindBlock(TEXT("minecraft:bee_nest"))->Index;
		for (int32 Y = Chunk.GetMinY(); Y <= Chunk.GetMaxY(); ++Y)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					const FMcStateId State = Chunk.GetBlock(X, Y, Z);
					Out.Logs += Tags.Has(Logs, State);
					Out.Leaves += Tags.Has(Leaves, State);
					Out.Vines += Registry.GetState(State).Block == Vine;
					Out.Nests += Registry.GetState(State).Block == Nest;
				}
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTreeTest, "McClient.McGen.Tree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcTreeTest::RunTest(const FString& Parameters)
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

	// Every tree feature in the jar parses to a feature that places something (none is left unsupported).
	FMcFeatureLibrary Library(Settings.SeaLevel, Settings.MinY, Settings.Height);
	TestTrue(TEXT("biome features load"), Library.LoadBiomes());
	TestFalse(TEXT("no feature errors"), Library.HasErrors());
	TestFalse(TEXT("trees are not unsupported"), Library.GetUnsupported().Contains(TEXT("minecraft:tree")));

	const FMcBlockTags& Tags = FMcBlockTags::Get();
	const int32 Logs = Tags.Find(TEXT("minecraft:logs"));
	const int32 Leaves = Tags.Find(TEXT("minecraft:leaves"));
	const int32 Stone = Tags.Find(TEXT("minecraft:base_stone_overworld"));

	int32 TotalLogs = 0;
	int32 TotalLeaves = 0;
	int32 Embedded = 0;
	int32 Floating = 0;
	for (const int32 Seed : { 12345, 777, 2024 })
	{
		const FMcNoiseGenerator Generator(Settings, Seed, TEXT("minecraft:overworld"));
		// The patch is 5x5 chunks, a long way from the spawn, so it isn't all one biome and some seeds have forests.
		constexpr int32 Size = 5;
		const FIntPoint Corner(Seed % 7 * 11, Seed % 5 * 13);
		TArray<TUniquePtr<FMcChunk>> Chunks;
		for (int32 Index = 0; Index < Size * Size; ++Index)
		{
			Chunks.Add(MakeUnique<FMcChunk>(Corner + FIntPoint(Index % Size, Index / Size), Settings.MinY, Settings.Height / 16));
			Generator.Generate(*Chunks.Last());
		}
		FTreeCounts Before;
		for (int32 Index : { 6, 7, 8, 11, 12, 13, 16, 17, 18 })
		{
			Count(*Chunks[Index], Before);
		}
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
			FMcGenRegion Region(Corner + FIntPoint(CX, CZ), Around);
			Library.Decorate(Region, Seed);
		}
		FTreeCounts After;
		for (int32 Index : { 6, 7, 8, 11, 12, 13, 16, 17, 18 })
		{
			Count(*Chunks[Index], After);
		}
		TotalLogs += After.Logs - Before.Logs;
		TotalLeaves += After.Leaves - Before.Leaves;
		AddInfo(FString::Printf(TEXT("seed %d: +%d logs, +%d leaves, +%d vines, +%d bee nests"), Seed, After.Logs - Before.Logs, After.Leaves - Before.Leaves, After.Vines - Before.Vines, After.Nests - Before.Nests));

		// A log must not sit inside solid stone (a tree starts on the surface), and the lowest log of a column of
		// logs must stand on something solid (or be a branch, with a log beside or under it).
		for (int32 Index : { 6, 7, 8, 11, 12, 13, 16, 17, 18 })
		{
			const FMcChunk& Chunk = *Chunks[Index];
			for (int32 Y = Chunk.GetMinY() + 1; Y < Chunk.GetMaxY(); ++Y)
			{
				for (int32 Z = 1; Z < 15; ++Z)
				{
					for (int32 X = 1; X < 15; ++X)
					{
						if (!Tags.Has(Logs, Chunk.GetBlock(X, Y, Z)))
						{
							continue;
						}
						int32 StoneAround = 0;
						StoneAround += Tags.Has(Stone, Chunk.GetBlock(X + 1, Y, Z));
						StoneAround += Tags.Has(Stone, Chunk.GetBlock(X - 1, Y, Z));
						StoneAround += Tags.Has(Stone, Chunk.GetBlock(X, Y, Z + 1));
						StoneAround += Tags.Has(Stone, Chunk.GetBlock(X, Y, Z - 1));
						StoneAround += Tags.Has(Stone, Chunk.GetBlock(X, Y + 1, Z));
						Embedded += StoneAround >= 4;
						const FMcStateId Below = Chunk.GetBlock(X, Y - 1, Z);
						Floating += (Tags.Has(Logs, Below) || Tags.Has(Leaves, Below) || !FMcBlockRegistry::Get().IsAir(Below)) ? 0 : 1;
					}
				}
			}
		}
	}
	// Every tree feature in the jar, planted by itself on flat grass, grows logs and leaves above the ground.
	const FMcStateId Grass = Blocks.FindDefaultState(TEXT("minecraft:grass_block")).Get(0);
	const FMcStateId Dirt = Blocks.FindDefaultState(TEXT("minecraft:dirt")).Get(0);
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *McCache::Data(TEXT("worldgen/feature/*.json")), true, false);
	int32 Planted = 0;
	TMap<FString, int32> Trees;
	for (const FString& File : Files)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *McCache::Data(TEXT("worldgen/feature/") + File))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json->GetStringField(TEXT("type")).EndsWith(TEXT(":tree")))
		{
			continue;
		}
		const FString Id = TEXT("minecraft:") + FPaths::GetBaseFilename(File);
		const TSharedPtr<const FMcFeature> Feature = Library.ParseFeature(MakeShared<FJsonValueString>(Id));
		if (!TestTrue(Id + TEXT(" parses"), Feature.IsValid()))
		{
			continue;
		}
		const FMcNoiseGenerator Unused(Settings, 1, TEXT("minecraft:overworld"));
		TArray<TUniquePtr<FMcChunk>> Flat;
		FMcChunk* Around[9];
		for (int32 Index = 0; Index < 9; ++Index)
		{
			Flat.Add(MakeUnique<FMcChunk>(FIntPoint(Index % 3, Index / 3), Settings.MinY, Settings.Height / 16));
			for (int32 Z = 0; Z < 16; ++Z)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					Flat.Last()->SetBlock(X, 62, Z, Dirt);
					Flat.Last()->SetBlock(X, 63, Z, Grass);
				}
			}
			Around[Index] = Flat.Last().Get();
		}
		FMcGenRegion Region(FIntPoint(1, 1), Around);
		FMcRandom Random(99);
		FMcFeatureContext Context{ Region, Random, Library, Settings.SeaLevel };
		const FIntVector Origin(24, 64, 24);
		bool bPlaced = false;
		for (int32 Try = 0; Try < 5 && !bPlaced; ++Try)
		{
			bPlaced = Feature->Place(Context, Origin + FIntVector(Try * 3, 0, 0));
		}
		FTreeCounts Counts;
		for (const TUniquePtr<FMcChunk>& Chunk : Flat)
		{
			Count(*Chunk, Counts);
		}
		TestTrue(Id + TEXT(" places"), bPlaced);
		TestTrue(Id + TEXT(" has logs"), Counts.Logs > 0);
		TestTrue(Id + TEXT(" has leaves"), Counts.Leaves > 0);
		if (Id == TEXT("minecraft:jungle_tree"))
		{
			TestTrue(TEXT("a jungle tree grows vines"), Counts.Vines > 0);
		}
		++Planted;
	}
	TestTrue(TEXT("tree features found"), Planted > 30);
	AddInfo(FString::Printf(TEXT("%d tree features planted on flat grass"), Planted));

	TestTrue(TEXT("logs placed"), TotalLogs > 0);
	TestTrue(TEXT("leaves placed"), TotalLeaves > TotalLogs);
	TestEqual(TEXT("no log embedded in stone"), Embedded, 0);
	AddInfo(FString::Printf(TEXT("%d logs with air under them (branches, mostly)"), Floating));
	return true;
}

#endif
