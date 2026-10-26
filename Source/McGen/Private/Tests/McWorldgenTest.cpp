// The features of caves, seas, the Nether and the End, and the structures built in code (mineshafts,
// strongholds, monuments, fortresses, mansions, end cities, nether fossils) and trial chambers' pool aliases.

#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McNoiseGenerator.h"
#include "McRandom.h"
#include "McStructures.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FMcStateId Parse(const TCHAR* Text)
	{
		return FMcBlockRegistry::Get().ParseState(Text).Get(0);
	}

	enum class EScene : uint8
	{
		Land,    // grass at y 63
		Sea,     // water to sea level over a sandy floor at y 45
		Cave,    // stone with an open cave y 45 to 58 over the middle
		Roof,    // a stone cave under a one block thick ceiling with open sky above
		Nether,  // netherrack to y 40, air above
		End,     // end stone to y 60, air above
	};

	// A square of chunks round a base chunk, filled for a scene.
	struct FScene
	{
		TArray<TUniquePtr<FMcChunk>> Chunks;
		int32 Size;
		FIntPoint Base;

		FScene(const FMcNoiseSettings& Settings, EScene Kind, int32 InSize, FIntPoint InBase) : Size(InSize), Base(InBase)
		{
			const FMcStateId Dirt = Parse(TEXT("minecraft:dirt"));
			const FMcStateId Grass = Parse(TEXT("minecraft:grass_block"));
			const FMcStateId Sand = Parse(TEXT("minecraft:sand"));
			const FMcStateId Water = Parse(TEXT("minecraft:water"));
			const FMcStateId Stone = Parse(TEXT("minecraft:stone"));
			const FMcStateId Netherrack = Parse(TEXT("minecraft:netherrack"));
			const FMcStateId EndStone = Parse(TEXT("minecraft:end_stone"));
			const FMcBiomeId Biome = FMcBiomeRegistry::Get().GetPlains();
			for (int32 Index = 0; Index < Size * Size; ++Index)
			{
				Chunks.Add(MakeUnique<FMcChunk>(FIntPoint(Base.X + Index % Size, Base.Y + Index / Size), Settings.MinY, Settings.Height / 16));
				FMcChunk& Chunk = *Chunks.Last();
				Chunk.FillBiome(Biome);
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 X = 0; X < 16; ++X)
					{
						switch (Kind)
						{
						case EScene::Land:
							Chunk.SetBlock(X, 62, Z, Dirt);
							Chunk.SetBlock(X, 63, Z, Grass);
							break;
						case EScene::Sea:
							for (int32 Y = 40; Y < 45; ++Y)
							{
								Chunk.SetBlock(X, Y, Z, Dirt);
							}
							Chunk.SetBlock(X, 45, Z, Sand);
							for (int32 Y = 46; Y < Settings.SeaLevel; ++Y)
							{
								Chunk.SetBlock(X, Y, Z, Water);
							}
							break;
						case EScene::Cave:
							for (int32 Y = 30; Y < 70; ++Y)
							{
								if (Y < 45 || Y > 58 || Index != Size * Size / 2)
								{
									Chunk.SetBlock(X, Y, Z, Stone);
								}
							}
							break;
						case EScene::Roof:
							for (int32 Y = 30; Y < 45; ++Y)
							{
								if (Y < 36 || Y > 41 || Index != Size * Size / 2)
								{
									Chunk.SetBlock(X, Y, Z, Y == 44 ? Dirt : Stone);
								}
							}
							break;
						case EScene::Nether:
							for (int32 Y = 20; Y < 41; ++Y)
							{
								Chunk.SetBlock(X, Y, Z, Netherrack);
							}
							break;
						case EScene::End:
							for (int32 Y = 50; Y < 61; ++Y)
							{
								Chunk.SetBlock(X, Y, Z, EndStone);
							}
							break;
						}
					}
				}
			}
		}

		FMcChunk& At(int32 CX, int32 CZ) { return *Chunks[(CZ - Base.Y) * Size + (CX - Base.X)]; }

		// The region round a chunk of the scene.
		void Around(int32 CX, int32 CZ, FMcChunk** Out)
		{
			for (int32 Slot = 0; Slot < 9; ++Slot)
			{
				Out[Slot] = &At(CX + Slot % 3 - 1, CZ + Slot / 3 - 1);
			}
		}

		TSet<FString> Names(const TArray<TArray<FMcStateId>>& Before, int32& OutChanged) const
		{
			TSet<FString> Result;
			OutChanged = 0;
			for (int32 Index = 0; Index < Chunks.Num(); ++Index)
			{
				const FMcChunk& Chunk = *Chunks[Index];
				int32 Cursor = 0;
				for (int32 Y = Chunk.GetMinY(); Y <= Chunk.GetMaxY(); ++Y)
				{
					for (int32 Z = 0; Z < 16; ++Z)
					{
						for (int32 X = 0; X < 16; ++X, ++Cursor)
						{
							const FMcStateId Now = Chunk.GetBlock(X, Y, Z);
							if (Now != Before[Index][Cursor])
							{
								++OutChanged;
								Result.Add(FMcBlockRegistry::Get().GetBlockOf(Now).Name.ToString());
							}
						}
					}
				}
			}
			return Result;
		}

		TArray<TArray<FMcStateId>> Snapshot() const
		{
			TArray<TArray<FMcStateId>> Result;
			for (const TUniquePtr<FMcChunk>& Chunk : Chunks)
			{
				TArray<FMcStateId>& Blocks = Result.AddDefaulted_GetRef();
				for (int32 Y = Chunk->GetMinY(); Y <= Chunk->GetMaxY(); ++Y)
				{
					for (int32 Z = 0; Z < 16; ++Z)
					{
						for (int32 X = 0; X < 16; ++X)
						{
							Blocks.Add(Chunk->GetBlock(X, Y, Z));
						}
					}
				}
			}
			return Result;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorldFeatureTest, "McClient.McGen.WorldFeatures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWorldFeatureTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	FMcFeatureLibrary Library(Settings.SeaLevel, Settings.MinY, Settings.Height);
	TestTrue(TEXT("biome features load"), Library.LoadBiomes());
	TestFalse(TEXT("no feature errors"), Library.HasErrors());
	// Every feature type any biome uses is written now.
	FString Missing;
	for (const TPair<FString, int32>& Entry : Library.GetUnsupported())
	{
		Missing += Entry.Key + TEXT(" ");
	}
	TestTrue(FString::Printf(TEXT("no feature type is left unwritten (%s)"), *Missing), Library.GetUnsupported().IsEmpty());

	struct FCase
	{
		const TCHAR* Feature;
		EScene Scene;
		FIntPoint BaseChunk;   // the scene's lowest chunk; the region is round its middle
		FIntVector Origin;
		const TCHAR* Marker;   // a block name the feature leaves
		int32 Tries;
		const TCHAR* FloorBlock = nullptr;  // laid under the origin first
	};
	const FCase Cases[] = {
		{ TEXT("minecraft:large_dripstone"), EScene::Cave, FIntPoint(0, 0), FIntVector(24, 50, 24), TEXT("minecraft:dripstone_block"), 6 },
		{ TEXT("minecraft:dripstone_cluster"), EScene::Cave, FIntPoint(0, 0), FIntVector(24, 50, 24), TEXT("minecraft:pointed_dripstone"), 6 },
		{ TEXT("minecraft:iceberg_blue"), EScene::Sea, FIntPoint(0, 0), FIntVector(24, 62, 24), TEXT("minecraft:blue_ice"), 6 },
		{ TEXT("minecraft:chorus_plant"), EScene::End, FIntPoint(0, 0), FIntVector(24, 61, 24), TEXT("minecraft:chorus_plant"), 6 },
		{ TEXT("minecraft:end_spike"), EScene::End, FIntPoint(1, -1), FIntVector(40, 61, 0), TEXT("minecraft:obsidian"), 1 },
		{ TEXT("minecraft:end_island"), EScene::End, FIntPoint(0, 0), FIntVector(24, 62, 24), TEXT("minecraft:end_stone"), 1 },
		{ TEXT("minecraft:end_gateway_return"), EScene::End, FIntPoint(0, 0), FIntVector(24, 70, 24), TEXT("minecraft:end_gateway"), 1 },
		{ TEXT("minecraft:end_platform"), EScene::End, FIntPoint(0, 0), FIntVector(24, 70, 24), TEXT("minecraft:obsidian"), 1 },
		{ TEXT("minecraft:end_podium_active"), EScene::End, FIntPoint(0, 0), FIntVector(24, 61, 24), TEXT("minecraft:end_portal"), 1 },
		{ TEXT("minecraft:delta"), EScene::Nether, FIntPoint(0, 0), FIntVector(24, 41, 24), TEXT("minecraft:lava"), 6 },
		{ TEXT("minecraft:crimson_fungus"), EScene::Nether, FIntPoint(0, 0), FIntVector(24, 41, 24), TEXT("minecraft:crimson_stem"), 6, TEXT("minecraft:crimson_nylium") },
		{ TEXT("minecraft:basalt_blobs"), EScene::Nether, FIntPoint(0, 0), FIntVector(24, 41, 24), TEXT("minecraft:basalt"), 6 },
		{ TEXT("minecraft:large_basalt_columns"), EScene::Nether, FIntPoint(0, 0), FIntVector(24, 41, 24), TEXT("minecraft:basalt"), 6 },
		{ TEXT("minecraft:basalt_pillar"), EScene::Nether, FIntPoint(0, 0), FIntVector(24, 60, 24), TEXT("minecraft:basalt"), 1 },
		{ TEXT("minecraft:sculk_patch_deep_dark"), EScene::Cave, FIntPoint(0, 0), FIntVector(24, 45, 24), TEXT("minecraft:sculk"), 4 },
		{ TEXT("minecraft:rooted_azalea_tree"), EScene::Roof, FIntPoint(0, 0), FIntVector(24, 41, 24), TEXT("minecraft:azalea_leaves"), 6 },
		{ TEXT("minecraft:warm_ocean_vegetation"), EScene::Sea, FIntPoint(0, 0), FIntVector(24, 46, 24), TEXT("coral_block"), 40 },
	};
	for (const FCase& Case : Cases)
	{
		const TSharedPtr<const FMcFeature> Feature = Library.ParseFeature(MakeShared<FJsonValueString>(Case.Feature));
		if (!TestTrue(FString::Printf(TEXT("%s parses"), Case.Feature), Feature.IsValid()))
		{
			continue;
		}
		// The region is round the scene's middle chunk.
		const int32 Size = 3;
		FScene Scene(Settings, Case.Scene, Size, Case.BaseChunk);
		FMcChunk* Around[9];
		Scene.Around(Case.BaseChunk.X + 1, Case.BaseChunk.Y + 1, Around);
		FMcGenRegion Region(FIntPoint(Case.BaseChunk.X + 1, Case.BaseChunk.Y + 1), Around);
		if (Case.FloorBlock)
		{
			for (int32 DX = 0; DX < 3; ++DX)
			{
				for (int32 DZ = 0; DZ < 3; ++DZ)
				{
					Region.SetBlock(Case.Origin + FIntVector(DX, -1, DZ), Parse(Case.FloorBlock));
				}
			}
		}
		const TArray<TArray<FMcStateId>> Prepared = Scene.Snapshot();
		for (int32 Try = 0; Try < Case.Tries; ++Try)
		{
			FMcRandom Random(100 + Try);
			FMcFeatureContext Context{ Region, Random, Library, Settings.SeaLevel, nullptr };
			// Tries move the origin about a little.
			const FIntVector At = Case.Origin + FIntVector(Try % 3, 0, (Try / 3) % 3);
			Feature->Place(Context, At);
		}
		int32 Changed = 0;
		const TSet<FString> Names = Scene.Names(Prepared, Changed);
		AddInfo(FString::Printf(TEXT("%s: %d blocks changed"), Case.Feature, Changed));
		TestTrue(FString::Printf(TEXT("%s places something"), Case.Feature), Changed > 0);
		bool bMarker = Names.Contains(Case.Marker);
		for (const FString& Name : Names)
		{
			bMarker = bMarker || Name.Contains(Case.Marker);
		}
		TestTrue(FString::Printf(TEXT("%s leaves %s"), Case.Feature, Case.Marker), bMarker);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorldStructureTest, "McClient.McGen.WorldStructures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWorldStructureTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	FMcFeatureLibrary Features(Settings.SeaLevel, Settings.MinY, Settings.Height);
	Features.LoadBiomes();
	FMcStructureLibrary Structures(Settings.SeaLevel, Settings.MinY);
	if (!TestTrue(TEXT("structure sets load"), Structures.Load(&Features)))
	{
		return false;
	}
	FString Left;
	for (const TPair<FString, int32>& Entry : Structures.GetUnsupported())
	{
		Left += FString::Printf(TEXT("%s(%d) "), *Entry.Key, Entry.Value);
	}
	TestTrue(FString::Printf(TEXT("every structure type is built (%s)"), *Left), Structures.GetUnsupported().IsEmpty());

	// The strongholds: 128 of them in rings, the first three about 2000 blocks out.
	{
		TArray<FName> Here;
		int32 Found = 0;
		int32 Nearest = MAX_int32;
		for (int32 Z = -260; Z <= 260; ++Z)
		{
			for (int32 X = -260; X <= 260; ++X)
			{
				Here.Reset();
				Structures.GetCandidates(777, FIntPoint(X, Z), Here);
				if (Here.Contains(TEXT("minecraft:stronghold")))
				{
					++Found;
					Nearest = FMath::Min(Nearest, FMath::RoundToInt32(FMath::Sqrt(static_cast<float>(X * X + Z * Z))));
				}
			}
		}
		AddInfo(FString::Printf(TEXT("strongholds within 260 chunks: %d, nearest %d chunks"), Found, Nearest));
		TestTrue(TEXT("the first ring has its three strongholds"), Found >= 3 && Found <= 6);
		TestTrue(TEXT("the first ring is 80 to 176 chunks out"), Nearest >= 70 && Nearest <= 190);
	}

	struct FCase
	{
		const TCHAR* Id;
		EScene Scene;
		int32 MinChanged;
		const TCHAR* Marker;
	};
	const FCase Cases[] = {
		{ TEXT("minecraft:mineshaft"), EScene::Land, 800, TEXT("minecraft:oak_planks") },
		{ TEXT("minecraft:mineshaft_mesa"), EScene::Land, 800, TEXT("minecraft:dark_oak_planks") },
		{ TEXT("minecraft:stronghold"), EScene::Land, 800, TEXT("minecraft:stone_bricks") },
		{ TEXT("minecraft:monument"), EScene::Sea, 5000, TEXT("minecraft:prismarine_bricks") },
		{ TEXT("minecraft:fortress"), EScene::Land, 800, TEXT("minecraft:nether_bricks") },
		{ TEXT("minecraft:mansion"), EScene::Land, 3000, TEXT("minecraft:dark_oak_planks") },
		{ TEXT("minecraft:end_city"), EScene::Land, 1000, TEXT("minecraft:purpur_block") },
		{ TEXT("minecraft:nether_fossil"), EScene::Land, 10, TEXT("minecraft:bone_block") },
		{ TEXT("minecraft:trial_chambers"), EScene::Land, 3000, nullptr },
	};
	const FIntPoint Start(7, 7);
	constexpr int32 Size = 15;
	for (const FCase& Case : Cases)
	{
		const int32 Floor = Case.Scene == EScene::Sea ? 46 : 64;
		Structures.SetHeightSource([Floor](int32, int32) { return Floor; });
		int32 BestChanged = 0;
		bool bMarker = Case.Marker == nullptr;
		TSet<FString> AllNames;
		for (const int32 Seed : { 1, 2, 3 })
		{
			FScene Forward(Settings, Case.Scene, Size, FIntPoint(0, 0));
			FScene Backward(Settings, Case.Scene, Size, FIntPoint(0, 0));
			const TArray<TArray<FMcStateId>> Before = Forward.Snapshot();
			// Every chunk of the middle builds its own part; the order they go in must not matter.
			for (int32 Index = 0; Index < 13 * 13; ++Index)
			{
				FMcChunk* Around[9];
				const int32 CX = 1 + Index % 13;
				const int32 CZ = 1 + Index / 13;
				Forward.Around(CX, CZ, Around);
				FMcGenRegion Region(FIntPoint(CX, CZ), Around);
				if (!TestTrue(FString::Printf(TEXT("%s is built"), Case.Id), Structures.PlaceStructure(Region, Case.Id, Seed, Start)))
				{
					return false;
				}
			}
			for (int32 Index = 13 * 13 - 1; Index >= 0; --Index)
			{
				FMcChunk* Around[9];
				const int32 CX = 1 + Index % 13;
				const int32 CZ = 1 + Index / 13;
				Backward.Around(CX, CZ, Around);
				FMcGenRegion Region(FIntPoint(CX, CZ), Around);
				Structures.PlaceStructure(Region, Case.Id, Seed, Start);
			}
			int32 Changed = 0;
			const TSet<FString> Names = Forward.Names(Before, Changed);
			int32 ChangedBackward = 0;
			Backward.Names(Before, ChangedBackward);
			TestEqual(FString::Printf(TEXT("%s builds the same in any order (seed %d)"), Case.Id, Seed), ChangedBackward, Changed);
			bool bSame = true;
			for (int32 Index = 0; Index < Forward.Chunks.Num() && bSame; ++Index)
			{
				for (int32 Y = -64; Y < 320 && bSame; ++Y)
				{
					for (int32 Z = 0; Z < 16 && bSame; ++Z)
					{
						for (int32 X = 0; X < 16; ++X)
						{
							if (Forward.Chunks[Index]->GetBlock(X, Y, Z) != Backward.Chunks[Index]->GetBlock(X, Y, Z))
							{
								bSame = false;
								break;
							}
						}
					}
				}
			}
			TestTrue(FString::Printf(TEXT("%s: the chunks agree block for block (seed %d)"), Case.Id, Seed), bSame);
			BestChanged = FMath::Max(BestChanged, Changed);
			AllNames.Append(Names);
			bMarker = bMarker || Names.Contains(Case.Marker);
		}
		TestTrue(FString::Printf(TEXT("%s changes enough blocks (best %d)"), Case.Id, BestChanged), BestChanged >= Case.MinChanged);
		TestTrue(FString::Printf(TEXT("%s puts down its marker block"), Case.Id), bMarker);
		TArray<FString> Sorted = AllNames.Array();
		Sorted.Sort();
		AddInfo(FString::Printf(TEXT("%s: best %d blocks changed; blocks: %s"), Case.Id, BestChanged, *FString::Join(Sorted, TEXT(" ")).Left(400)));
	}

	// ---- Trial chambers: their spawner pools come through the aliases ----
	{
		FScene Scene(Settings, EScene::Land, Size, FIntPoint(0, 0));
		Structures.SetHeightSource([](int32, int32) { return 64; });
		FMcChunk* Around[9];
		Scene.Around(7, 7, Around);
		FMcGenRegion Region(FIntPoint(7, 7), Around);
		Structures.PlaceStructure(Region, TEXT("minecraft:trial_chambers"), 5, Start);
		const TSharedPtr<FMcJigsawLibrary::FLayout> Layout = Structures.FindLayout(TEXT("minecraft:trial_chambers"), 5, Start);
		if (TestTrue(TEXT("trial chambers lay out"), Layout.IsValid() && Layout->Pieces.Num() > 10))
		{
			int32 Spawners = 0;
			int32 Contents = 0;
			for (const FMcJigsawLibrary::FPiece& Piece : Layout->Pieces)
			{
				Spawners += Piece.Element->Location.Contains(TEXT("trial_chambers/spawner/")) ? 1 : 0;
				Contents += Piece.Element->Location.Contains(TEXT("/contents/")) ? 1 : 0;
			}
			AddInfo(FString::Printf(TEXT("trial chambers: %d pieces, %d spawner pieces"), Layout->Pieces.Num(), Spawners));
			TestTrue(TEXT("trial chambers get spawner pieces through the aliases"), Spawners > 0);
			TestEqual(TEXT("no piece is left as an unresolved alias"), Contents, 0);
		}
	}
	return true;
}

#endif
