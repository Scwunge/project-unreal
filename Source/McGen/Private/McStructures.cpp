#include "McStructures.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McChunk.h"
#include "McGen.h"
#include "McGenRegion.h"
#include "McPoolAliases.h"
#include "McProceduralStructures.h"
#include "McRandom.h"
#include "McStructureSpawns.h"
#include "McStructureTemplate.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

struct FMcStructureLibrary::FStructure
{
	struct FSetup
	{
		FString Placement;
		float Mossiness = 0.2f;
		float AirPocket = 0.0f;
		bool bCold = false;
		bool bOvergrown = false;
		bool bVines = false;
		bool bBlackstone = false;
		int32 Weight = 1;
	};

	FName Id;
	FString Type;
	FString Variant;  // a mineshaft's "normal" or "mesa"
	TBitArray<> Biomes;
	bool bBeached = false;
	bool bWarm = false;
	float Cluster = 0.9f;
	float Large = 0.3f;
	TArray<FSetup> Setups;
	int32 SetupWeight = 0;

	// Jigsaw structures.
	FName StartPool;
	FName StartJigsaw;
	int32 Size = 6;
	int32 MaxDistance = 80;
	int32 StartHeightMin = 0;
	int32 StartHeightMax = 0;
	bool bProject = false;
	bool bBeard = false;
	TArray<TSharedPtr<FJsonValue>> PoolAliases;
	bool bSupported = false;

	// Mobs that spawn here instead of the biome's (spawn_overrides): by category, and whether the box is the whole
	// structure ("full") or each piece ("piece").
	TArray<FMcSpawnEntry> OverrideMonsters;
	TArray<FMcSpawnEntry> OverrideCreatures;
	bool bMonstersPerPiece = false;
};

struct FMcStructureLibrary::FSet
{
	FName Id;
	int32 Spacing = 1;
	int32 Separation = 0;
	int32 Salt = 0;
	bool bTriangular = false;
	float Frequency = 1.0f;
	// Structure index and weight.
	TArray<TPair<int32, int32>> Entries;
	int32 TotalWeight = 0;
	FName ExclusionSet;
	int32 ExclusionChunks = 0;
	// How many chunks from its start chunk a structure of this set can reach.
	int32 Reach = 1;
	// Concentric rings (strongholds): how many starts, how far apart the rings are, how many in the first.
	bool bRings = false;
	int32 RingCount = 0;
	int32 RingDistance = 32;
	int32 RingSpread = 3;
};

namespace
{
	TSharedPtr<FJsonObject> LoadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json))
		{
			return nullptr;
		}
		return Json;
	}

	int32 FloorDiv(int32 A, int32 B)
	{
		return FMath::FloorToInt(static_cast<double>(A) / B);
	}

	// The chunks strongholds start in, by seed.
	const TArray<FIntPoint>& RingChunks(uint64 Seed, int32 Count, int32 Distance, int32 Spread)
	{
		static FCriticalSection Lock;
		static TMap<FString, TArray<FIntPoint>> Cache;
		FScopeLock Guard(&Lock);
		const FString Key = FString::Printf(TEXT("%llu/%d/%d/%d"), Seed, Count, Distance, Spread);
		if (!Cache.Contains(Key))
		{
			TArray<FIntPoint> Chunks;
			McProcedural::StrongholdChunks(Seed, Count, Distance, Spread, Chunks);
			if (Cache.Num() > 8)
			{
				Cache.Reset();
			}
			Cache.Add(Key, MoveTemp(Chunks));
		}
		return Cache[Key];
	}

	bool IsSupported(const FString& Type)
	{
		static const TCHAR* const Types[] = { TEXT("igloo"), TEXT("desert_pyramid"), TEXT("jungle_temple"), TEXT("swamp_hut"), TEXT("shipwreck"),
			TEXT("ocean_ruin"), TEXT("ruined_portal"), TEXT("buried_treasure"), TEXT("mineshaft"), TEXT("stronghold"), TEXT("ocean_monument"),
			TEXT("fortress"), TEXT("woodland_mansion"), TEXT("end_city"), TEXT("nether_fossil") };
		for (const TCHAR* Supported : Types)
		{
			if (Type == Supported)
			{
				return true;
			}
		}
		return false;
	}

	FMcStateId State(const FString& Text)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (const TOptional<FMcStateId> Found = Registry.ParseState(Text))
		{
			return *Found;
		}
		static FCriticalSection Lock;
		static TSet<FString> Seen;
		FScopeLock Guard(&Lock);
		if (!Seen.Contains(Text))
		{
			Seen.Add(Text);
			UE_LOG(LogMcGen, Warning, TEXT("Structure block %s is unknown"), *Text);
		}
		return Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
	}

	// The ground a structure stands on: heights read only in the start chunk, so every chunk that looks at the same
	// structure sees the same ground. Trees don't count.
	struct FGround
	{
		FMcGenRegion& Region;
		int32 SeaLevel;
		int32 MinY;

		int32 Floor(int32 X, int32 Z) const
		{
			const FMcBlockTags& Tags = FMcBlockTags::Get();
			static const int32 Logs = Tags.Find(TEXT("minecraft:logs"));
			static const int32 Leaves = Tags.Find(TEXT("minecraft:leaves"));
			int32 Height = Region.GetHeight(EMcHeightmap::OceanFloor, X, Z);
			while (Height > MinY + 1)
			{
				const FMcStateId Below = Region.GetBlock(FIntVector(X, Height - 1, Z));
				if (!Tags.Has(Logs, Below) && !Tags.Has(Leaves, Below))
				{
					break;
				}
				--Height;
			}
			return Height;
		}
		// The water's surface over the floor, if any.
		int32 Top(int32 X, int32 Z) const { return FMath::Max(Floor(X, Z), SeaLevel); }
		// The lowest floor over a grid of columns of the chunk, which starts at (BaseX, BaseZ).
		int32 LowestFloor(int32 BaseX, int32 BaseZ, int32 Inset = 1) const
		{
			int32 Lowest = MAX_int32;
			for (const int32 DZ : { Inset, 8, 15 - Inset })
			{
				for (const int32 DX : { Inset, 8, 15 - Inset })
				{
					Lowest = FMath::Min(Lowest, Floor(BaseX + DX, BaseZ + DZ));
				}
			}
			return Lowest;
		}
	};

	// What every structure builder needs.
	struct FCtx
	{
		FMcGenRegion& Region;
		FMcRandom& Random;
		FGround Ground;
		FIntPoint Start;       // the start chunk
		FIntPoint ClipMin;     // the chunk being decorated, inclusive
		FIntPoint ClipMax;
		int32 SeaLevel;
		int32 MinY;

		FIntVector Centre() const { return FIntVector(Start.X * 16 + 8, 0, Start.Y * 16 + 8); }
		// Where a swamp hut stood, set by building one: its box and the middle of its floor.
		TOptional<TPair<FIntVector, FIntVector>> HutBox;
		FVector HutFloor = FVector::ZeroVector;
		FMcTemplatePlaceSettings Settings() const
		{
			FMcTemplatePlaceSettings Result;
			Result.bClip = true;
			Result.ClipMin = ClipMin;
			Result.ClipMax = ClipMax;
			return Result;
		}
	};

	// A template turned about its middle and put with that middle over a column.
	int32 PlaceAround(FCtx& Context, const FMcStructureTemplate& Template, int32 X, int32 Y, int32 Z, FMcTemplatePlaceSettings Settings)
	{
		Settings.Pivot = FIntVector(Template.Size.X / 2, 0, Template.Size.Z / 2);
		return Template.Place(Context.Region, FIntVector(X, Y, Z) - Settings.Pivot, Settings, Context.Random);
	}

	// ---- Code built structures ----

	// Local coordinates, turned about a pivot, written only inside the clip rectangle.
	struct FBuilder
	{
		FCtx& Context;
		FIntVector Origin;
		FIntVector Pivot = FIntVector::ZeroValue;
		EMcRotation Rotation = EMcRotation::None;
		TMap<FString, FMcStateId> Cache;

		FMcStateId S(const TCHAR* Text)
		{
			const FString Key(Text);
			if (const FMcStateId* Found = Cache.Find(Key))
			{
				return *Found;
			}
			return Cache.Add(Key, State(Key));
		}

		void Set(int32 X, int32 Y, int32 Z, FMcStateId Block)
		{
			const FIntVector Pos = Origin + McTemplate::TransformPos(FIntVector(X, Y, Z), Rotation, EMcMirror::None, Pivot);
			if (Pos.X < Context.ClipMin.X || Pos.X > Context.ClipMax.X || Pos.Z < Context.ClipMin.Y || Pos.Z > Context.ClipMax.Y)
			{
				return;
			}
			Context.Region.SetBlock(Pos, McTemplate::TransformState(Block, Rotation, EMcMirror::None));
		}
		void Set(int32 X, int32 Y, int32 Z, const TCHAR* Text) { Set(X, Y, Z, S(Text)); }

		// A box, inclusive. With Alt each block is Alt instead with that chance (drawn for every block, so the
		// stream doesn't depend on the clip).
		void Fill(int32 X0, int32 Y0, int32 Z0, int32 X1, int32 Y1, int32 Z1, FMcStateId Block, FMcStateId Alt = McTemplate::SkipState, float AltChance = 0.0f)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 Z = Z0; Z <= Z1; ++Z)
				{
					for (int32 X = X0; X <= X1; ++X)
					{
						const bool bAlt = Alt != McTemplate::SkipState && Context.Random.NextFloat() < AltChance;
						Set(X, Y, Z, bAlt ? Alt : Block);
					}
				}
			}
		}
		void Fill(int32 X0, int32 Y0, int32 Z0, int32 X1, int32 Y1, int32 Z1, const TCHAR* Text) { Fill(X0, Y0, Z0, X1, Y1, Z1, S(Text)); }

		bool IsEmptyBelow(const FIntVector& Pos) const
		{
			const FMcStateId Block = Context.Region.GetBlock(Pos);
			return Context.Region.IsAir(Block) || FMcBlockRegistry::Get().GetBlockOf(Block).Name == FName("minecraft:water");
		}
		// Fills a column down from Y while the block is air or water, so a building doesn't hang.
		void Foundation(int32 X, int32 Z, int32 FromY, int32 Depth, FMcStateId Block, FMcStateId Alt = McTemplate::SkipState, float AltChance = 0.0f)
		{
			bool bStopped = false;
			for (int32 Y = FromY; Y > FromY - Depth; --Y)
			{
				const FIntVector Pos = Origin + McTemplate::TransformPos(FIntVector(X, Y, Z), Rotation, EMcMirror::None, Pivot);
				// Drawn for every block, so the stream doesn't depend on the ground.
				const bool bAlt = Alt != McTemplate::SkipState && Context.Random.NextFloat() < AltChance;
				bStopped = bStopped || !IsEmptyBelow(Pos);
				if (!bStopped)
				{
					Set(X, Y, Z, bAlt ? Alt : Block);
				}
			}
		}
	};

	// The desert pyramid: 21 blocks square, ten layers stepping in, a hall inside with a patterned floor and a
	// trapdoor-less shaft in its middle that drops to a pressure plate on TNT between four chests
	// (minecraft.wiki, "Desert pyramid"). Centred coordinates, the doorway on the north face.
	void BuildDesertPyramid(FCtx& Context)
	{
		const FIntVector Centre = Context.Centre();
		const int32 BaseY = Context.Ground.LowestFloor(Context.Start.X * 16, Context.Start.Y * 16, 2);
		FBuilder B{ Context, FIntVector(Centre.X, BaseY, Centre.Z) };
		B.Rotation = McTemplate::RandomRotation(Context.Random);
		const FMcStateId Sand = B.S(TEXT("minecraft:sandstone"));
		const FMcStateId Cut = B.S(TEXT("minecraft:cut_sandstone"));
		const FMcStateId Chiseled = B.S(TEXT("minecraft:chiseled_sandstone"));
		const FMcStateId Orange = B.S(TEXT("minecraft:orange_terracotta"));
		const FMcStateId Air = B.S(TEXT("minecraft:air"));

		for (int32 Z = -10; Z <= 10; ++Z)
		{
			for (int32 X = -10; X <= 10; ++X)
			{
				B.Foundation(X, Z, -1, 7, Sand);
			}
		}
		for (int32 Layer = 0; Layer < 10; ++Layer)
		{
			const int32 H = 10 - Layer;
			B.Fill(-H, Layer, -H, H, Layer, H, Sand);
			// A band of cut sandstone round the outside, and a chiselled stripe up each corner.
			for (int32 I = -H; I <= H; ++I)
			{
				const FMcStateId Edge = (Layer & 1) ? Cut : Sand;
				B.Set(I, Layer, -H, Edge);
				B.Set(I, Layer, H, Edge);
				B.Set(-H, Layer, I, Edge);
				B.Set(H, Layer, I, Edge);
			}
			B.Set(-H, Layer, -H, Chiseled);
			B.Set(H, Layer, -H, Chiseled);
			B.Set(-H, Layer, H, Chiseled);
			B.Set(H, Layer, H, Chiseled);
		}
		// The hall: a stepped hollow under the layers.
		for (int32 Layer = 1; Layer <= 4; ++Layer)
		{
			const int32 W = FMath::Min(7, 10 - Layer - 2);
			B.Fill(-W, Layer, -W, W, Layer, W, Air);
		}
		for (int32 Z = -7; Z <= 7; ++Z)
		{
			for (int32 X = -7; X <= 7; ++X)
			{
				const int32 Ring = FMath::Max(FMath::Abs(X), FMath::Abs(Z));
				B.Set(X, 0, Z, (Ring & 1) == 0 ? Orange : Cut);
			}
		}
		B.Set(0, 0, 0, TEXT("minecraft:blue_terracotta"));
		for (const int32 SX : { -3, 3 })
		{
			for (const int32 SZ : { -3, 3 })
			{
				B.Fill(SX, 1, SZ, SX, 4, SZ, Chiseled);
			}
		}
		// The doorway in the north face, with steps up to it.
		B.Fill(-1, 1, -10, 1, 2, -6, Air);
		for (int32 X = -1; X <= 1; ++X)
		{
			B.Set(X, 0, -11, TEXT("minecraft:sandstone_stairs[facing=south]"));
		}
		// The trap below the hall's middle.
		B.Fill(-3, -11, -3, 3, -5, 3, Sand);
		B.Fill(-2, -9, -2, 2, -6, 2, Air);
		B.Fill(-1, -10, -1, 1, -10, 1, TEXT("minecraft:tnt"));
		B.Set(0, -9, 0, TEXT("minecraft:stone_pressure_plate"));
		B.Fill(0, -5, 0, 0, -1, 0, Air);
		B.Set(2, -9, 0, TEXT("minecraft:chest[facing=west]"));
		B.Set(-2, -9, 0, TEXT("minecraft:chest[facing=east]"));
		B.Set(0, -9, 2, TEXT("minecraft:chest[facing=north]"));
		B.Set(0, -9, -2, TEXT("minecraft:chest[facing=south]"));
	}

	// The jungle temple: 12 by 15 blocks, two storeys of mossy cobblestone with a few chests and stairs up to the
	// front door (minecraft.wiki, "Jungle temple"; the puzzle and traps are left out). Doorway on the south face.
	void BuildJungleTemple(FCtx& Context)
	{
		const FIntVector Centre = Context.Centre();
		const int32 BaseY = Context.Ground.LowestFloor(Context.Start.X * 16, Context.Start.Y * 16, 2);
		FBuilder B{ Context, FIntVector(Centre.X - 6, BaseY, Centre.Z - 7) };
		B.Pivot = FIntVector(6, 0, 7);
		B.Rotation = McTemplate::RandomRotation(Context.Random);
		const FMcStateId Cobble = B.S(TEXT("minecraft:cobblestone"));
		const FMcStateId Mossy = B.S(TEXT("minecraft:mossy_cobblestone"));
		const FMcStateId Air = B.S(TEXT("minecraft:air"));

		for (int32 Z = 0; Z <= 14; ++Z)
		{
			for (int32 X = 0; X <= 11; ++X)
			{
				B.Foundation(X, Z, -1, 7, Cobble, Mossy, 0.4f);
			}
		}
		// The lower storey and the upper one on its roof.
		B.Fill(0, 0, 0, 11, 4, 14, Cobble, Mossy, 0.4f);
		B.Fill(1, 1, 1, 10, 3, 13, Air);
		B.Fill(1, 4, 1, 10, 8, 13, Cobble, Mossy, 0.4f);
		B.Fill(2, 5, 2, 9, 7, 12, Air);
		B.Fill(3, 8, 3, 8, 9, 11, Cobble, Mossy, 0.4f);
		// The way in, and the way up.
		B.Fill(5, 1, 14, 6, 2, 14, Air);
		B.Fill(5, 0, 14, 6, 0, 15, Mossy);
		B.Set(5, 0, 15, TEXT("minecraft:mossy_cobblestone_stairs[facing=south]"));
		B.Set(6, 0, 15, TEXT("minecraft:mossy_cobblestone_stairs[facing=south]"));
		for (int32 Step = 0; Step < 4; ++Step)
		{
			B.Set(9, 1 + Step, 2 + Step, TEXT("minecraft:mossy_cobblestone"));
		}
		B.Fill(9, 4, 3, 9, 4, 4, Air);
		B.Set(2, 1, 2, TEXT("minecraft:chest[facing=south]"));
		B.Set(9, 1, 12, TEXT("minecraft:chest[facing=north]"));
		B.Set(5, 5, 7, TEXT("minecraft:chest[facing=south]"));
		B.Set(6, 5, 7, TEXT("minecraft:chest[facing=south]"));
	}

	// The swamp hut: seven blocks square on stilts, spruce walls and a stepped roof, with a crafting table and a
	// cauldron inside (minecraft.wiki, "Swamp hut"). Doorway on the south face.
	void BuildSwampHut(FCtx& Context)
	{
		const FIntVector Centre = Context.Centre();
		const int32 BaseY = Context.Ground.Top(Centre.X, Centre.Z);
		FBuilder B{ Context, FIntVector(Centre.X - 3, BaseY, Centre.Z - 3) };
		B.Pivot = FIntVector(3, 0, 3);
		B.Rotation = McTemplate::RandomRotation(Context.Random);
		const FMcStateId Log = B.S(TEXT("minecraft:oak_log"));
		const FMcStateId Planks = B.S(TEXT("minecraft:spruce_planks"));
		const FMcStateId Air = B.S(TEXT("minecraft:air"));

		for (const int32 X : { 0, 6 })
		{
			for (const int32 Z : { 0, 6 })
			{
				B.Foundation(X, Z, 0, 12, Log);
				B.Fill(X, 1, Z, X, 3, Z, Log);
			}
		}
		B.Fill(0, 1, 0, 6, 1, 6, Planks);
		B.Fill(0, 2, 0, 6, 3, 6, Planks);
		B.Fill(1, 2, 1, 5, 3, 5, Air);
		for (const int32 X : { 0, 6 })
		{
			for (const int32 Z : { 0, 6 })
			{
				B.Fill(X, 2, Z, X, 3, Z, Log);
			}
		}
		// Windows, a doorway and a porch step.
		B.Set(0, 3, 3, Air);
		B.Set(6, 3, 3, Air);
		B.Set(3, 3, 0, Air);
		B.Fill(3, 2, 6, 3, 3, 6, Air);
		B.Set(3, 1, 7, TEXT("minecraft:spruce_planks"));
		// The roof: three steps narrowing.
		B.Fill(0, 4, 0, 6, 4, 6, Planks);
		B.Fill(1, 5, 1, 5, 5, 5, Planks);
		B.Fill(2, 6, 2, 4, 6, 4, B.S(TEXT("minecraft:spruce_slab")));
		B.Set(1, 2, 1, TEXT("minecraft:crafting_table"));
		B.Set(5, 2, 1, TEXT("minecraft:cauldron"));
		B.Set(5, 2, 5, TEXT("minecraft:potted_red_mushroom"));
		// Whoever lives there: a witch and her cat, standing in the middle of the room (the game's too).
		Context.HutBox = TPair<FIntVector, FIntVector>(FIntVector(Centre.X - 3, BaseY, Centre.Z - 3), FIntVector(Centre.X + 3, BaseY + 6, Centre.Z + 3));
		Context.HutFloor = FVector(Centre.X + 0.5, BaseY + 2, Centre.Z + 0.5);
	}

	void BuildBuriedTreasure(FCtx& Context)
	{
		const int32 X = Context.Start.X * 16 + 9;
		const int32 Z = Context.Start.Y * 16 + 9;
		if (X < Context.ClipMin.X || X > Context.ClipMax.X || Z < Context.ClipMin.Y || Z > Context.ClipMax.Y)
		{
			return;
		}
		const int32 Floor = Context.Ground.Floor(X, Z);
		// Two blocks under the surface, if that is solid.
		int32 Y = Floor - 2;
		if (Context.Region.IsAir(Context.Region.GetBlock(FIntVector(X, Y, Z))))
		{
			Y = Floor - 1;
		}
		Context.Region.SetBlock(FIntVector(X, Y, Z), State(TEXT("minecraft:chest[facing=north]")));
	}

	// ---- Template built structures ----

	// The igloo: a snow dome on the surface whose trapdoor leads down a ladder shaft to a basement with a chest
	// and a brewing stand, half the time (minecraft.wiki, "Igloo"). Every piece turns about the shaft.
	void BuildIgloo(FCtx& Context)
	{
		const TSharedPtr<const FMcStructureTemplate> Top = FMcStructureTemplate::Find(TEXT("igloo/top"));
		const TSharedPtr<const FMcStructureTemplate> Middle = FMcStructureTemplate::Find(TEXT("igloo/middle"));
		const TSharedPtr<const FMcStructureTemplate> Bottom = FMcStructureTemplate::Find(TEXT("igloo/bottom"));
		const EMcRotation Rotation = McTemplate::RandomRotation(Context.Random);
		const bool bBasement = Context.Random.NextBool();
		const int32 Middles = Context.Random.NextIntBetween(4, 11);
		if (!Top.IsValid())
		{
			return;
		}
		const FIntVector Shaft = Context.Centre();
		const int32 SurfaceY = Context.Ground.Floor(Shaft.X, Shaft.Z);
		// Where the top's trapdoor column (3, 5) lands on the shaft.
		const FIntVector TopOrigin(Shaft.X - 3, SurfaceY - 1, Shaft.Z - 5);
		FMcTemplatePlaceSettings Settings = Context.Settings();
		Settings.Rotation = Rotation;
		Settings.Pivot = FIntVector(3, 0, 5);
		Top->Place(Context.Region, TopOrigin, Settings, Context.Random);
		if (!bBasement || !Middle.IsValid() || !Bottom.IsValid())
		{
			return;
		}
		const int32 BottomY = TopOrigin.Y - 3 * Middles - 6;
		if (BottomY < Context.MinY + 2)
		{
			return;
		}
		Settings.Pivot = FIntVector(1, 0, 1);
		for (int32 Index = 1; Index <= Middles; ++Index)
		{
			Middle->Place(Context.Region, FIntVector(TopOrigin.X + 2, TopOrigin.Y - 3 * Index, TopOrigin.Z + 4), Settings, Context.Random);
		}
		Settings.Pivot = FIntVector(3, 0, 7);
		Bottom->Place(Context.Region, FIntVector(TopOrigin.X, BottomY, TopOrigin.Z - 2), Settings, Context.Random);
	}

	// A ruin on the sea floor, with a cluster of small ones round it most of the time.
	void BuildOceanRuin(FCtx& Context, float ClusterChance, float LargeChance,
		const TArray<FString>& Small, const TArray<FString>& Big)
	{
		FMcRandom& Random = Context.Random;
		const bool bLarge = Random.NextFloat() < LargeChance && !Big.IsEmpty();
		const TArray<FString>& Main = bLarge ? Big : Small;
		if (Main.IsEmpty())
		{
			return;
		}
		auto PlaceRuin = [&](const FString& Id, int32 X, int32 Z)
		{
			if (const TSharedPtr<const FMcStructureTemplate> Template = FMcStructureTemplate::Find(Id))
			{
				FMcTemplatePlaceSettings Settings = Context.Settings();
				Settings.Rotation = McTemplate::RandomRotation(Random);
				PlaceAround(Context, *Template, X, Context.Ground.Floor(X, Z) - 1, Z, Settings);
			}
		};
		const FIntVector Centre = Context.Centre();
		PlaceRuin(Main[Random.NextInt(Main.Num())], Centre.X, Centre.Z);
		if (Random.NextFloat() < ClusterChance)
		{
			const int32 Extra = Random.NextIntBetween(4, 8);
			for (int32 Index = 0; Index < Extra; ++Index)
			{
				const int32 X = Context.Start.X * 16 + Random.NextIntBetween(2, 13);
				const int32 Z = Context.Start.Y * 16 + Random.NextIntBetween(2, 13);
				PlaceRuin(Small[Random.NextInt(Small.Num())], X, Z);
			}
		}
	}

	void BuildShipwreck(FCtx& Context, bool bBeached, const TArray<FString>& Ships)
	{
		if (Ships.IsEmpty())
		{
			return;
		}
		const FString& Id = Ships[Context.Random.NextInt(Ships.Num())];
		const EMcRotation Rotation = McTemplate::RandomRotation(Context.Random);
		const TSharedPtr<const FMcStructureTemplate> Template = FMcStructureTemplate::Find(Id);
		if (!Template.IsValid())
		{
			return;
		}
		FMcTemplatePlaceSettings Settings = Context.Settings();
		Settings.Rotation = Rotation;
		const FIntVector Centre = Context.Centre();
		// Sunk into the sea floor under the hull (or the sand of the beach) so it doesn't hang.
		const int32 Low = Context.Ground.LowestFloor(Context.Start.X * 16, Context.Start.Y * 16, 3);
		PlaceAround(Context, *Template, Centre.X, Low - (bBeached ? 1 : 2), Centre.Z, Settings);
	}

	// Moss, crying obsidian and magma on a ruined portal's blocks (minecraft.wiki, "Ruined portal").
	struct FPortalProcessor : FMcProcessorList::FProcessor
	{
		float Mossiness = 0.2f;
		bool bCold = false;
		bool bBlackstone = false;

		virtual FMcStateId Process(FMcGenRegion&, FIntVector&, int32, FMcStateId Old, FMcStateId, FMcRandom& Random) const override
		{
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			const FString Name = Registry.GetBlockOf(Old).Name.ToString();
			FString Swap;
			if (Name == TEXT("minecraft:obsidian"))
			{
				if (Random.NextFloat() < 0.1f)
				{
					Swap = TEXT("minecraft:crying_obsidian");
				}
			}
			else if (Name == TEXT("minecraft:netherrack"))
			{
				if (Random.NextFloat() < 0.07f)
				{
					Swap = TEXT("minecraft:magma_block");
				}
				else if (bCold && Random.NextFloat() < 0.5f)
				{
					Swap = TEXT("minecraft:packed_ice");
				}
			}
			else if (Random.NextFloat() < Mossiness)
			{
				if (Name.Contains(TEXT("cobblestone")) && !Name.Contains(TEXT("mossy")))
				{
					Swap = Name.Replace(TEXT("cobblestone"), TEXT("mossy_cobblestone"));
				}
				else if (Name.Contains(TEXT("stone_brick")) && !Name.Contains(TEXT("mossy")) && !Name.Contains(TEXT("cracked")) && !Name.Contains(TEXT("chiseled")))
				{
					Swap = Name.Replace(TEXT("stone_brick"), TEXT("mossy_stone_brick"));
				}
			}
			if (Swap.IsEmpty())
			{
				return Old;
			}
			const FMcBlock* Block = Registry.FindBlock(FName(*Swap));
			if (!Block)
			{
				return Old;
			}
			TMap<FName, FName> Properties;
			for (const FMcBlockProperty& Property : Registry.GetBlockOf(Old).Properties)
			{
				Properties.Add(Property.Name, Registry.GetPropertyValue(Old, Property.Name));
			}
			return Registry.FindState(Block->Name, Properties).Get(Old);
		}
	};

	void BuildRuinedPortal(FCtx& Context, const FString& Placement, float Mossiness, bool bCold)
	{
		FMcRandom& Random = Context.Random;
		const bool bGiant = Random.NextFloat() < 0.05f;
		const FString Id = bGiant ? FString::Printf(TEXT("ruined_portal/giant_portal_%d"), Random.NextIntBetween(1, 3))
			: FString::Printf(TEXT("ruined_portal/portal_%d"), Random.NextIntBetween(1, 10));
		const EMcRotation Rotation = McTemplate::RandomRotation(Random);
		const EMcMirror Mirror = Random.NextFloat() < 0.5f ? EMcMirror::None : EMcMirror::FrontBack;
		const TSharedPtr<const FMcStructureTemplate> Template = FMcStructureTemplate::Find(Id);
		const int32 Depth = Random.NextInt(1 << 20);
		if (!Template.IsValid())
		{
			return;
		}
		FMcTemplatePlaceSettings Settings = Context.Settings();
		Settings.Rotation = Rotation;
		Settings.Mirror = Mirror;
		Settings.Pivot = FIntVector(Template->Size.X / 2, 0, Template->Size.Z / 2);
		TSharedRef<FMcProcessorList> Processors = MakeShared<FMcProcessorList>();
		TSharedRef<FPortalProcessor> Processor = MakeShared<FPortalProcessor>();
		Processor->Mossiness = Mossiness;
		Processor->bCold = bCold;
		Processors->Add(Processor);
		Settings.Processors = &Processors.Get();

		const int32 Low = Context.Ground.LowestFloor(Context.Start.X * 16, Context.Start.Y * 16, 3);
		int32 Y = Low - 1;
		if (Placement == TEXT("underground"))
		{
			const int32 Lowest = Context.MinY + 15;
			const int32 Highest = FMath::Max(Lowest + 1, Low - Template->Size.Y - 5);
			Y = Lowest + Depth % (Highest - Lowest);
		}
		else if (Placement == TEXT("on_ocean_floor"))
		{
			Y = Low - 1;
		}
		const FIntVector Centre = Context.Centre();
		const FIntVector Corner(Centre.X - Settings.Pivot.X, Y, Centre.Z - Settings.Pivot.Z);
		Template->Place(Context.Region, Corner, Settings, Random);
		if (Placement != TEXT("underground"))
		{
			// Netherrack under the bottom layer, down to the ground, so the portal's edge doesn't hang.
			const FMcStateId Rack = State(TEXT("minecraft:netherrack"));
			for (const FMcStructureTemplate::FBlock& Block : Template->Blocks)
			{
				const FMcStateId Old = Template->Palettes[0].IsValidIndex(Block.Palette) ? Template->Palettes[0][Block.Palette] : McTemplate::SkipState;
				if (Block.Pos.Y != 0 || Old == McTemplate::SkipState || Context.Region.IsAir(Old))
				{
					continue;
				}
				const FIntVector Pos = Template->ToWorld(Block.Pos, Corner, Settings);
				if (Pos.X < Context.ClipMin.X || Pos.X > Context.ClipMax.X || Pos.Z < Context.ClipMin.Y || Pos.Z > Context.ClipMax.Y)
				{
					continue;
				}
				for (int32 Down = 1; Down <= 6; ++Down)
				{
					const FIntVector Below = Pos - FIntVector(0, Down, 0);
					const FMcStateId Existing = Context.Region.GetBlock(Below);
					if (!Context.Region.IsAir(Existing) && FMcBlockRegistry::Get().GetBlockOf(Existing).Name != FName("minecraft:water"))
					{
						break;
					}
					Context.Region.SetBlock(Below, Rack);
				}
			}
		}
	}
}

FMcStructureLibrary::FMcStructureLibrary(int32 InSeaLevel, int32 InMinY)
	: SeaLevel(InSeaLevel)
	, MinY(InMinY)
{
}

bool FMcStructureLibrary::ResolveTag(const FString& Tag, TBitArray<>& Out, int32 Depth) const
{
	if (Depth > 8)
	{
		return false;
	}
	FString Path = Tag;
	Path.RemoveFromStart(TEXT("#"));
	Path.RemoveFromStart(TEXT("minecraft:"));
	const TSharedPtr<FJsonObject> Json = LoadJson(McCache::Data(TEXT("tags/worldgen/biome/") + Path + TEXT(".json")));
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("values"), Values))
	{
		return false;
	}
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Entry;
		if (Value->Type == EJson::Object)
		{
			Value->AsObject()->TryGetStringField(TEXT("id"), Entry);
		}
		else
		{
			Entry = Value->AsString();
		}
		if (Entry.StartsWith(TEXT("#")))
		{
			ResolveTag(Entry, Out, Depth + 1);
		}
		else if (const TOptional<FMcBiomeId> Id = Biomes.Find(FName(*Entry)))
		{
			Out[*Id] = true;
		}
	}
	return true;
}

bool FMcStructureLibrary::Load(FMcFeatureLibrary* Features)
{
	FeatureLibrary = Features;
	Jigsaws = MakeShared<FMcJigsawLibrary>();
	Layouts.Reset();
	LayoutOrder.Reset();
	Structures.Reset();
	Sets.Reset();
	Unsupported.Reset();
	if (!FMcBlockRegistry::Get().EnsureLoaded() || !FMcBiomeRegistry::Get().EnsureLoaded() || !FMcBlockTags::Get().EnsureLoaded())
	{
		return false;
	}
	const int32 NumBiomes = FMcBiomeRegistry::Get().NumBiomes();
	TMap<FName, int32> ByName;

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *McCache::Data(TEXT("worldgen/structure/*.json")), true, false);
	for (const FString& File : Files)
	{
		const TSharedPtr<FJsonObject> Json = LoadJson(McCache::Data(TEXT("worldgen/structure/") + File));
		if (!Json.IsValid())
		{
			continue;
		}
		TSharedRef<FStructure> Structure = MakeShared<FStructure>();
		Structure->Id = FName(*(TEXT("minecraft:") + FPaths::GetBaseFilename(File)));
		Structure->Type = Json->GetStringField(TEXT("type"));
		Structure->Type.RemoveFromStart(TEXT("minecraft:"));
		Structure->Biomes.Init(false, NumBiomes);
		const TSharedPtr<FJsonValue> Biomes = Json->TryGetField(TEXT("biomes"));
		if (Biomes.IsValid() && Biomes->Type == EJson::String)
		{
			ResolveTag(Biomes->AsString(), Structure->Biomes, 0);
		}
		else if (Biomes.IsValid() && Biomes->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Biomes->AsArray())
			{
				if (const TOptional<FMcBiomeId> Id = FMcBiomeRegistry::Get().Find(FName(*Entry->AsString())))
				{
					Structure->Biomes[*Id] = true;
				}
			}
		}
		const TSharedPtr<FJsonObject>* Overrides = nullptr;
		if (Json->TryGetObjectField(TEXT("spawn_overrides"), Overrides))
		{
			const auto ReadCategory = [&](const TCHAR* Category, TArray<FMcSpawnEntry>& Out, bool* bPiece)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				if (!(*Overrides)->TryGetObjectField(Category, Entry))
				{
					return;
				}
				FString Box;
				if (bPiece)
				{
					*bPiece = (*Entry)->TryGetStringField(TEXT("bounding_box"), Box) && Box == TEXT("piece");
				}
				const TArray<TSharedPtr<FJsonValue>>* Spawns = nullptr;
				if (!(*Entry)->TryGetArrayField(TEXT("spawns"), Spawns))
				{
					return;
				}
				for (const TSharedPtr<FJsonValue>& Value : *Spawns)
				{
					const TSharedPtr<FJsonObject> Object = Value->AsObject();
					FString Type;
					if (Object.IsValid() && Object->TryGetStringField(TEXT("type"), Type))
					{
						FMcSpawnEntry Spawn;
						Spawn.Kind = FName(*Type);
						double Weight = 1.0;
						Object->TryGetNumberField(TEXT("weight"), Weight);
						Spawn.Weight = static_cast<int32>(Weight);
						// The group size is a number, or a uniform range ({"min_inclusive", "max_inclusive"}).
						Spawn.MinCount = Spawn.MaxCount = 1;
						double Count = 1.0;
						const TSharedPtr<FJsonObject>* Range = nullptr;
						if (Object->TryGetNumberField(TEXT("count"), Count))
						{
							Spawn.MinCount = Spawn.MaxCount = static_cast<int32>(Count);
						}
						else if (Object->TryGetObjectField(TEXT("count"), Range))
						{
							double Low = 1.0;
							double High = 1.0;
							(*Range)->TryGetNumberField(TEXT("min_inclusive"), Low);
							(*Range)->TryGetNumberField(TEXT("max_inclusive"), High);
							Spawn.MinCount = static_cast<int32>(Low);
							Spawn.MaxCount = static_cast<int32>(High);
						}
						Out.Add(Spawn);
					}
				}
			};
			ReadCategory(TEXT("monster"), Structure->OverrideMonsters, &Structure->bMonstersPerPiece);
			ReadCategory(TEXT("creature"), Structure->OverrideCreatures, nullptr);
		}
		Json->TryGetBoolField(TEXT("is_beached"), Structure->bBeached);
		FString Temperature;
		Structure->bWarm = Json->TryGetStringField(TEXT("biome_temp"), Temperature) && Temperature == TEXT("warm");
		Json->TryGetNumberField(TEXT("cluster_probability"), Structure->Cluster);
		Json->TryGetNumberField(TEXT("large_probability"), Structure->Large);
		const TArray<TSharedPtr<FJsonValue>>* Setups = nullptr;
		if (Json->TryGetArrayField(TEXT("setups"), Setups))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Setups)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FStructure::FSetup& Setup = Structure->Setups.AddDefaulted_GetRef();
				Object->TryGetStringField(TEXT("placement"), Setup.Placement);
				Object->TryGetNumberField(TEXT("mossiness"), Setup.Mossiness);
				Object->TryGetNumberField(TEXT("air_pocket_probability"), Setup.AirPocket);
				Object->TryGetBoolField(TEXT("can_be_cold"), Setup.bCold);
				Object->TryGetBoolField(TEXT("overgrown"), Setup.bOvergrown);
				Object->TryGetBoolField(TEXT("vines"), Setup.bVines);
				Object->TryGetBoolField(TEXT("replace_with_blackstone"), Setup.bBlackstone);
				Object->TryGetNumberField(TEXT("weight"), Setup.Weight);
				Structure->SetupWeight += Setup.Weight;
			}
		}
		Json->TryGetStringField(TEXT("mineshaft_type"), Structure->Variant);
		Structure->bSupported = IsSupported(Structure->Type);
		if (Structure->Type == TEXT("jigsaw"))
		{
			Structure->StartPool = FName(*Json->GetStringField(TEXT("start_pool")));
			Structure->Size = static_cast<int32>(Json->GetNumberField(TEXT("size")));
			double Distance = 80.0;
			const TSharedPtr<FJsonObject>* DistanceObject = nullptr;
			if (Json->TryGetNumberField(TEXT("max_distance_from_center"), Distance))
			{
				Structure->MaxDistance = static_cast<int32>(Distance);
			}
			else if (Json->TryGetObjectField(TEXT("max_distance_from_center"), DistanceObject))
			{
				Structure->MaxDistance = static_cast<int32>((*DistanceObject)->GetNumberField(TEXT("horizontal")));
			}
			FString Anchor;
			if (Json->TryGetStringField(TEXT("start_jigsaw_name"), Anchor))
			{
				Structure->StartJigsaw = FName(*Anchor);
			}
			const TSharedPtr<FJsonObject>* Height = nullptr;
			if (Json->TryGetObjectField(TEXT("start_height"), Height))
			{
				double Absolute = 0.0;
				if ((*Height)->TryGetNumberField(TEXT("absolute"), Absolute))
				{
					Structure->StartHeightMin = Structure->StartHeightMax = static_cast<int32>(Absolute);
				}
				else
				{
					const TSharedPtr<FJsonObject>* Low = nullptr;
					const TSharedPtr<FJsonObject>* High = nullptr;
					if ((*Height)->TryGetObjectField(TEXT("min_inclusive"), Low) && (*Height)->TryGetObjectField(TEXT("max_inclusive"), High))
					{
						Structure->StartHeightMin = static_cast<int32>((*Low)->GetNumberField(TEXT("absolute")));
						Structure->StartHeightMax = static_cast<int32>((*High)->GetNumberField(TEXT("absolute")));
					}
				}
			}
			Structure->bProject = Json->HasField(TEXT("project_start_to_heightmap"));
			FString Adaptation;
			Structure->bBeard = Json->TryGetStringField(TEXT("terrain_adaptation"), Adaptation) && Adaptation == TEXT("beard_thin");
			const TArray<TSharedPtr<FJsonValue>>* Aliases = nullptr;
			if (Json->TryGetArrayField(TEXT("pool_aliases"), Aliases))
			{
				Structure->PoolAliases = *Aliases;
				TArray<FName> Targets;
				McPoolAliases::CollectTargets(*Aliases, Targets);
				for (const FName Target : Targets)
				{
					Jigsaws->LoadPool(Target, Features);
				}
			}
			Structure->bSupported = !Structure->StartPool.IsNone();
			if (Structure->bSupported)
			{
				Structure->bSupported = Jigsaws->LoadPool(Structure->StartPool, Features) && Jigsaws->GetPool(Structure->StartPool) && !Jigsaws->GetPool(Structure->StartPool)->Elements.IsEmpty();
			}
		}
		if (!Structure->bSupported)
		{
			++Unsupported.FindOrAdd(Structure->Type);
		}
		ByName.Add(Structure->Id, Structures.Num());
		Structures.Add(Structure);
	}

	Files.Reset();
	IFileManager::Get().FindFiles(Files, *McCache::Data(TEXT("worldgen/structure_set/*.json")), true, false);
	for (const FString& File : Files)
	{
		const TSharedPtr<FJsonObject> Json = LoadJson(McCache::Data(TEXT("worldgen/structure_set/") + File));
		const TSharedPtr<FJsonObject>* Placement = nullptr;
		if (!Json.IsValid() || !Json->TryGetObjectField(TEXT("placement"), Placement))
		{
			continue;
		}
		FString PlacementType = (*Placement)->GetStringField(TEXT("type"));
		PlacementType.RemoveFromStart(TEXT("minecraft:"));
		TSharedRef<FSet> Set = MakeShared<FSet>();
		Set->Id = FName(*(TEXT("minecraft:") + FPaths::GetBaseFilename(File)));
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (Json->TryGetArrayField(TEXT("structures"), Entries))
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Entries)
			{
				const TSharedPtr<FJsonObject> Object = Entry->AsObject();
				const int32* Index = ByName.Find(FName(*Object->GetStringField(TEXT("structure"))));
				if (Index && Structures[*Index]->bSupported)
				{
					const int32 Weight = static_cast<int32>(Object->GetNumberField(TEXT("weight")));
					Set->Entries.Emplace(*Index, Weight);
					Set->TotalWeight += Weight;
				}
			}
		}
		for (const TPair<int32, int32>& Entry : Set->Entries)
		{
			const FStructure& Member = *Structures[Entry.Key];
			if (Member.Type == TEXT("jigsaw"))
			{
				Set->Reach = FMath::Max(Set->Reach, (Member.MaxDistance + 8 + 15) / 16);
			}
			else if (Member.Type == TEXT("mineshaft"))
			{
				Set->Reach = FMath::Max(Set->Reach, McProcedural::MineshaftReach);
			}
			else if (Member.Type == TEXT("stronghold"))
			{
				Set->Reach = FMath::Max(Set->Reach, McProcedural::StrongholdReach);
			}
			else if (Member.Type == TEXT("ocean_monument"))
			{
				Set->Reach = FMath::Max(Set->Reach, McProcedural::MonumentReach);
			}
			else if (Member.Type == TEXT("fortress"))
			{
				Set->Reach = FMath::Max(Set->Reach, McProcedural::FortressReach);
			}
			else if (Member.Type == TEXT("woodland_mansion"))
			{
				Set->Reach = FMath::Max(Set->Reach, McProcedural::MansionReach);
			}
			else if (Member.Type == TEXT("end_city"))
			{
				Set->Reach = FMath::Max(Set->Reach, McProcedural::EndCityReach);
			}
		}
		if (Set->Entries.IsEmpty())
		{
			continue;
		}
		if (PlacementType == TEXT("concentric_rings"))
		{
			Set->bRings = true;
			Set->RingCount = static_cast<int32>((*Placement)->GetNumberField(TEXT("count")));
			Set->RingDistance = static_cast<int32>((*Placement)->GetNumberField(TEXT("distance")));
			Set->RingSpread = static_cast<int32>((*Placement)->GetNumberField(TEXT("spread")));
			Set->Salt = static_cast<int32>((*Placement)->GetNumberField(TEXT("salt")));
			Sets.Add(Set);
			continue;
		}
		if (PlacementType != TEXT("random_spread"))
		{
			UE_LOG(LogMcGen, Log, TEXT("Structure set %s uses %s placement, left out"), *Set->Id.ToString(), *PlacementType);
			continue;
		}
		Set->Spacing = FMath::Max(1, static_cast<int32>((*Placement)->GetNumberField(TEXT("spacing"))));
		Set->Separation = static_cast<int32>((*Placement)->GetNumberField(TEXT("separation")));
		Set->Salt = static_cast<int32>((*Placement)->GetNumberField(TEXT("salt")));
		FString Spread;
		Set->bTriangular = (*Placement)->TryGetStringField(TEXT("spread_type"), Spread) && Spread.EndsWith(TEXT("triangular"));
		(*Placement)->TryGetNumberField(TEXT("frequency"), Set->Frequency);
		const TSharedPtr<FJsonObject>* Exclusion = nullptr;
		if ((*Placement)->TryGetObjectField(TEXT("exclusion_zone"), Exclusion))
		{
			Set->ExclusionSet = FName(*(*Exclusion)->GetStringField(TEXT("other_set")));
			Set->ExclusionChunks = static_cast<int32>((*Exclusion)->GetNumberField(TEXT("chunk_count")));
		}
		Sets.Add(Set);
	}

	// The template ids for ocean ruins and shipwrecks.
	TArray<FString> Ids;
	FMcStructureTemplate::ListIds(Ids);
	for (const FString& Id : Ids)
	{
		if (Id.StartsWith(TEXT("underwater_ruin/")))
		{
			const bool bBig = Id.Contains(TEXT("/big_"));
			const bool bWarm = Id.Contains(TEXT("warm"));
			(bWarm ? (bBig ? WarmBigRuins : WarmRuins) : (bBig ? ColdBigRuins : ColdRuins)).Add(Id);
		}
		else if (Id.StartsWith(TEXT("shipwreck/")))
		{
			Shipwrecks.Add(Id);
		}
	}

	if (!Unsupported.IsEmpty())
	{
		FString List;
		for (const TPair<FString, int32>& Pair : Unsupported)
		{
			List += FString::Printf(TEXT(" %s(%d)"), *Pair.Key, Pair.Value);
		}
		UE_LOG(LogMcGen, Log, TEXT("Structure types not built yet, skipped:%s"), *List);
	}
	return !Sets.IsEmpty();
}

bool FMcStructureLibrary::IsStartChunk(const FSet& Set, uint64 Seed, FIntPoint Chunk) const
{
	if (Set.bRings)
	{
		return RingChunks(Seed, Set.RingCount, Set.RingDistance, Set.RingSpread).Contains(Chunk);
	}
	const int32 CellX = FloorDiv(Chunk.X, Set.Spacing);
	const int32 CellZ = FloorDiv(Chunk.Y, Set.Spacing);
	FMcRandom Random = FMcRandom(Seed).Fork(CellX, Set.Salt, CellZ);
	const int32 Range = Set.Spacing - Set.Separation;
	auto Offset = [&]()
	{
		if (Range <= 1)
		{
			return 0;
		}
		return Set.bTriangular ? (Random.NextInt(Range) + Random.NextInt(Range)) / 2 : Random.NextInt(Range);
	};
	const int32 OffsetX = Offset();
	const int32 OffsetZ = Offset();
	if (Chunk.X != CellX * Set.Spacing + OffsetX || Chunk.Y != CellZ * Set.Spacing + OffsetZ)
	{
		return false;
	}
	if (Set.Frequency < 1.0f && FMcRandom(Seed).Fork(Chunk.X, Set.Salt ^ 0x5bd1e9, Chunk.Y).NextFloat() >= Set.Frequency)
	{
		return false;
	}
	return !IsExcluded(Set, Seed, Chunk);
}

// Whether another set has a start within some chunks, which keeps this one away (outposts keep out of villages).
bool FMcStructureLibrary::IsExcluded(const FSet& Set, uint64 Seed, FIntPoint Chunk) const
{
	if (Set.ExclusionSet.IsNone())
	{
		return false;
	}
	for (const TSharedPtr<FSet>& Other : Sets)
	{
		if (Other->Id != Set.ExclusionSet)
		{
			continue;
		}
		for (int32 DZ = -Set.ExclusionChunks; DZ <= Set.ExclusionChunks; ++DZ)
		{
			for (int32 DX = -Set.ExclusionChunks; DX <= Set.ExclusionChunks; ++DX)
			{
				if (IsStartChunk(*Other, Seed, FIntPoint(Chunk.X + DX, Chunk.Y + DZ)))
				{
					return true;
				}
			}
		}
	}
	return false;
}

const FMcStructureLibrary::FStructure* FMcStructureLibrary::Choose(const FSet& Set, uint64 Seed, FIntPoint Chunk) const
{
	FMcRandom Random = FMcRandom(Seed).Fork(Chunk.X, Set.Salt + 1, Chunk.Y);
	int32 Pick = Random.NextInt(FMath::Max(1, Set.TotalWeight));
	for (const TPair<int32, int32>& Entry : Set.Entries)
	{
		if (Pick < Entry.Value)
		{
			return Structures[Entry.Key].Get();
		}
		Pick -= Entry.Value;
	}
	return Structures[Set.Entries.Last().Key].Get();
}

void FMcStructureLibrary::GetCandidates(uint64 Seed, FIntPoint Chunk, TArray<FName>& Out) const
{
	for (const TSharedPtr<FSet>& Set : Sets)
	{
		if (IsStartChunk(*Set, Seed, Chunk))
		{
			Out.Add(Choose(*Set, Seed, Chunk)->Id);
		}
	}
}

bool FMcStructureLibrary::BiomeFits(FMcGenRegion& Region, const FStructure& Structure, FIntPoint Chunk, bool bInRegion) const
{
	const int32 X = Chunk.X * 16 + 8;
	const int32 Z = Chunk.Y * 16 + 8;
	FMcBiomeId Biome = 0;
	if (BiomeSource && HeightSource)
	{
		// The same answer from every chunk that looks.
		Biome = BiomeSource(X, HeightSource(X, Z) - 1, Z);
	}
	else if (bInRegion)
	{
		const FGround Ground{ Region, SeaLevel, MinY };
		Biome = Region.GetBiome(FIntVector(X, Ground.Floor(X, Z), Z));
	}
	else
	{
		return false;
	}
	return Structure.Biomes.IsValidIndex(Biome) && Structure.Biomes[Biome];
}

// A village wants dry ground without much of a slope round its centre.
bool FMcStructureLibrary::IsFlatEnough(FIntPoint Start) const
{
	if (!HeightSource)
	{
		return true;
	}
	const int32 CentreX = Start.X * 16 + 8;
	const int32 CentreZ = Start.Y * 16 + 8;
	int32 Low = MAX_int32;
	int32 High = MIN_int32;
	for (int32 DZ = -24; DZ <= 24; DZ += 12)
	{
		for (int32 DX = -24; DX <= 24; DX += 12)
		{
			const int32 Height = HeightSource(CentreX + DX, CentreZ + DZ);
			Low = FMath::Min(Low, Height);
			High = FMath::Max(High, Height);
		}
	}
	return HeightSource(CentreX, CentreZ) > SeaLevel && Low > SeaLevel && High - Low <= 10;
}

void FMcStructureLibrary::GetStarts(FMcGenRegion& Region, uint64 Seed, FIntPoint Chunk, TArray<FName>& Out) const
{
	for (const TSharedPtr<FSet>& Set : Sets)
	{
		if (IsStartChunk(*Set, Seed, Chunk))
		{
			const FStructure* Structure = Choose(*Set, Seed, Chunk);
			if (BiomeFits(Region, *Structure, Chunk, true))
			{
				Out.Add(Structure->Id);
			}
		}
	}
}

void FMcStructureLibrary::Place(FMcGenRegion& Region, uint64 Seed) const
{
	const FIntPoint Centre = Region.GetCentre();
	for (const TSharedPtr<FSet>& Set : Sets)
	{
		// A set's structures reach as far as its widest one; the biome source finds starts beyond the region.
		const int32 Reach = BiomeSource ? Set->Reach : 1;
		for (int32 DZ = -Reach; DZ <= Reach; ++DZ)
		{
			for (int32 DX = -Reach; DX <= Reach; ++DX)
			{
				const FIntPoint Start(Centre.X + DX, Centre.Y + DZ);
				if (IsStartChunk(*Set, Seed, Start))
				{
					const FStructure* Structure = Choose(*Set, Seed, Start);
					if (BiomeFits(Region, *Structure, Start, FMath::Abs(DX) <= 1 && FMath::Abs(DZ) <= 1))
					{
						Build(Region, *Structure, Seed, Start, Set->Salt);
					}
				}
			}
		}
	}
}

void FMcStructureLibrary::Build(FMcGenRegion& Region, const FStructure& Structure, uint64 Seed, FIntPoint Start, int32 Salt) const
{
	const FIntPoint Centre = Region.GetCentre();
	FMcRandom Random = FMcRandom(Seed).Fork(Start.X, Salt + 2, Start.Y);
	FCtx Context{ Region, Random, FGround{ Region, SeaLevel, MinY }, Start, FIntPoint(Centre.X * 16, Centre.Y * 16), FIntPoint(Centre.X * 16 + 15, Centre.Y * 16 + 15), SeaLevel, MinY };
	const FString& Type = Structure.Type;
	if (Type == TEXT("igloo"))
	{
		BuildIgloo(Context);
	}
	else if (Type == TEXT("desert_pyramid"))
	{
		BuildDesertPyramid(Context);
	}
	else if (Type == TEXT("jungle_temple"))
	{
		BuildJungleTemple(Context);
	}
	else if (Type == TEXT("swamp_hut"))
	{
		BuildSwampHut(Context);
		if (Context.HutBox.IsSet())
		{
			FMcStructureZone Zone;
			Zone.Structure = Structure.Id;
			Zone.Min = Context.HutBox->Key;
			Zone.Max = Context.HutBox->Value;
			Zone.Monsters = Structure.OverrideMonsters;
			Zone.Creatures = Structure.OverrideCreatures;
			FMcStructureSpawns::Get().AddZone(Zone);
			// The first of each kind the structure names lives there from the start.
			const FString Key = FString::Printf(TEXT("%llu/%d/%d/%s"), Seed, Start.X, Start.Y, *Structure.Id.ToString());
			for (const TArray<FMcSpawnEntry>* List : { &Structure.OverrideMonsters, &Structure.OverrideCreatures })
			{
				if (!List->IsEmpty())
				{
					FMcStructureMob Mob;
					Mob.Kind = (*List)[0].Kind;
					Mob.Position = Context.HutFloor;
					FMcStructureSpawns::Get().AddMob(Key, Mob);
				}
			}
		}
	}
	else if (Type == TEXT("buried_treasure"))
	{
		BuildBuriedTreasure(Context);
	}
	else if (Type == TEXT("shipwreck"))
	{
		BuildShipwreck(Context, Structure.bBeached, Shipwrecks);
	}
	else if (Type == TEXT("ocean_ruin"))
	{
		BuildOceanRuin(Context, Structure.Cluster, Structure.Large, Structure.bWarm ? WarmRuins : ColdRuins, Structure.bWarm ? WarmBigRuins : ColdBigRuins);
	}
	else if (Type == TEXT("jigsaw"))
	{
		const int32 CentreX = Start.X * 16 + 8;
		const int32 CentreZ = Start.Y * 16 + 8;
		const FString Key = FString::Printf(TEXT("%llu/%d/%d/%s"), Seed, Start.X, Start.Y, *Structure.Id.ToString());
		TSharedPtr<FMcJigsawLibrary::FLayout> Layout;
		{
			FScopeLock Guard(&LayoutLock);
			if (const TSharedPtr<FMcJigsawLibrary::FLayout>* Found = Layouts.Find(Key))
			{
				Layout = *Found;
			}
		}
		if (!Layout.IsValid())
		{
			const bool bRejected = Structure.Id.ToString().StartsWith(TEXT("minecraft:village_")) && !IsFlatEnough(Start);
			const int32 StartGround = HeightSource ? HeightSource(CentreX, CentreZ) : Context.Ground.Floor(CentreX, CentreZ);
			FMcJigsawLibrary::FStart Params;
			Params.Pool = Structure.StartPool;
			Params.MaxDepth = Structure.Size;
			Params.MaxDistance = Structure.MaxDistance;
			Params.StartJigsaw = Structure.StartJigsaw;
			McPoolAliases::Resolve(Structure.PoolAliases, Random, Params.Aliases);
			Params.CentreX = CentreX;
			Params.CentreZ = CentreZ;
			Params.Y = Structure.StartHeightMin + (Structure.StartHeightMax > Structure.StartHeightMin ? Random.NextInt(Structure.StartHeightMax - Structure.StartHeightMin + 1) : 0) + (Structure.bProject ? StartGround : 0);
			Params.MinY = Region.GetMinY();
			Params.MaxY = Region.GetMaxY();
			auto Height = [&](int32 X, int32 Z) { return HeightSource ? HeightSource(X, Z) : StartGround; };
			Layout = bRejected ? nullptr : Jigsaws->Assemble(Params, Random, Height);
			if (!Layout.IsValid())
			{
				Layout = MakeShared<FMcJigsawLibrary::FLayout>();
			}
			FScopeLock Guard(&LayoutLock);
			Layouts.Add(Key, Layout);
			LayoutOrder.Add(Key);
			while (LayoutOrder.Num() > 256)
			{
				Layouts.Remove(LayoutOrder[0]);
				LayoutOrder.RemoveAt(0);
			}
		}
		const FMcRandom PlaceBase = FMcRandom(Seed).Fork(Start.X, Salt + 3, Start.Y);
		Jigsaws->Place(Region, *Layout, Context.ClipMin, Context.ClipMax, PlaceBase, Structure.bBeard, FeatureLibrary, SeaLevel);
		// Its own mobs spawn through the whole of its box, or inside each piece (the bounding_box setting).
		if (!Layout->Pieces.IsEmpty() && (!Structure.OverrideMonsters.IsEmpty() || !Structure.OverrideCreatures.IsEmpty()))
		{
			FMcStructureZone Zone;
			Zone.Structure = Structure.Id;
			Zone.Monsters = Structure.OverrideMonsters;
			Zone.Creatures = Structure.OverrideCreatures;
			Zone.Min = Layout->Pieces[0].Min;
			Zone.Max = Layout->Pieces[0].Max;
			for (const FMcJigsawLibrary::FPiece& Piece : Layout->Pieces)
			{
				if (Structure.bMonstersPerPiece)
				{
					Zone.Min = Piece.Min;
					Zone.Max = Piece.Max;
					FMcStructureSpawns::Get().AddZone(Zone);
				}
				else
				{
					Zone.Min = FIntVector(FMath::Min(Zone.Min.X, Piece.Min.X), FMath::Min(Zone.Min.Y, Piece.Min.Y), FMath::Min(Zone.Min.Z, Piece.Min.Z));
					Zone.Max = FIntVector(FMath::Max(Zone.Max.X, Piece.Max.X), FMath::Max(Zone.Max.Y, Piece.Max.Y), FMath::Max(Zone.Max.Z, Piece.Max.Z));
				}
			}
			if (!Structure.bMonstersPerPiece)
			{
				FMcStructureSpawns::Get().AddZone(Zone);
			}
		}
	}
	else if (Type == TEXT("mineshaft") || Type == TEXT("stronghold") || Type == TEXT("ocean_monument") || Type == TEXT("fortress") || Type == TEXT("woodland_mansion")
		|| Type == TEXT("end_city") || Type == TEXT("nether_fossil"))
	{
		McProcedural::FEnv Env{ Region, Start, Context.ClipMin, Context.ClipMax, MinY, Region.GetMaxY(), SeaLevel,
			[this, &Context](int32 X, int32 Z)
			{
				if (!HeightSource)
				{
					return Context.Ground.Floor(X, Z);
				}
				const int64 Key = (static_cast<int64>(X) << 32) ^ static_cast<uint32>(Z);
				{
					FScopeLock Guard(&GroundLock);
					if (const int32* Found = GroundCache.Find(Key))
					{
						return *Found;
					}
				}
				const int32 Height = HeightSource(X, Z);
				FScopeLock Guard(&GroundLock);
				if (GroundCache.Num() > 4000000)
				{
					GroundCache.Reset();
				}
				GroundCache.Add(Key, Height);
				return Height;
			} };
		Env.LootSeed = Random.Fork(TEXT("loot")).NextLong();
		if (Type == TEXT("mineshaft"))
		{
			McProcedural::BuildMineshaft(Env, Random, Structure.Variant == TEXT("mesa"));
		}
		else if (Type == TEXT("stronghold"))
		{
			McProcedural::BuildStronghold(Env, Random);
		}
		else if (Type == TEXT("ocean_monument"))
		{
			McProcedural::BuildMonument(Env, Random);
		}
		else if (Type == TEXT("fortress"))
		{
			McProcedural::BuildFortress(Env, Random);
		}
		else if (Type == TEXT("woodland_mansion"))
		{
			McProcedural::BuildMansion(Env, Random);
		}
		else if (Type == TEXT("end_city"))
		{
			McProcedural::BuildEndCity(Env, Random);
		}
		else
		{
			McProcedural::BuildNetherFossil(Env, Random);
		}
	}
	else if (Type == TEXT("ruined_portal"))
	{
		if (Structure.SetupWeight > 0)
		{
			int32 Pick = Random.NextInt(Structure.SetupWeight);
			for (const FStructure::FSetup& Setup : Structure.Setups)
			{
				if (Pick < Setup.Weight)
				{
					BuildRuinedPortal(Context, Setup.Placement, Setup.Mossiness, Setup.bCold && Random.NextFloat() < 0.2f);
					break;
				}
				Pick -= Setup.Weight;
			}
		}
	}
}

TSharedPtr<FMcJigsawLibrary::FLayout> FMcStructureLibrary::FindLayout(FName Structure, uint64 Seed, FIntPoint Start) const
{
	FScopeLock Guard(&LayoutLock);
	const TSharedPtr<FMcJigsawLibrary::FLayout>* Found = Layouts.Find(FString::Printf(TEXT("%llu/%d/%d/%s"), Seed, Start.X, Start.Y, *Structure.ToString()));
	return Found ? *Found : nullptr;
}

bool FMcStructureLibrary::PlaceStructure(FMcGenRegion& Region, FName Id, uint64 Seed, FIntPoint Start) const
{
	for (const TSharedPtr<FStructure>& Structure : Structures)
	{
		if (Structure->Id == Id && Structure->bSupported)
		{
			Build(Region, *Structure, Seed, Start, 0);
			return true;
		}
	}
	return false;
}

bool FMcStructureLibrary::BiomeAllows(FName Id, FMcBiomeId Biome) const
{
	for (const TSharedPtr<FStructure>& Structure : Structures)
	{
		if (Structure->Id == Id)
		{
			return Structure->Biomes.IsValidIndex(Biome) && Structure->Biomes[Biome];
		}
	}
	return false;
}
