#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McJigsaw.h"
#include "McNoiseGenerator.h"
#include "McRandom.h"
#include "McStructures.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TArray<FMcStateId> SnapshotChunk(const FMcChunk& Chunk)
	{
		TArray<FMcStateId> Blocks;
		for (int32 Y = Chunk.GetMinY(); Y <= Chunk.GetMaxY(); ++Y)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					Blocks.Add(Chunk.GetBlock(X, Y, Z));
				}
			}
		}
		return Blocks;
	}

	// Dry land that doesn't rise or fall much round a column, so a village has somewhere to stand.
	bool IsFlatLand(const FMcNoiseGenerator& Generator, int32 X, int32 Z, int32 SeaLevel)
	{
		int32 Low = MAX_int32;
		int32 High = MIN_int32;
		for (int32 DZ = -48; DZ <= 48; DZ += 24)
		{
			for (int32 DX = -48; DX <= 48; DX += 24)
			{
				const int32 Y = Generator.FindSurfaceY(X + DX, Z + DZ);
				Low = FMath::Min(Low, Y);
				High = FMath::Max(High, Y);
			}
		}
		return Low > SeaLevel + 1 && High - Low <= 12;
	}

	bool BoxesTouch(const FMcJigsawLibrary::FPiece& A, const FMcJigsawLibrary::FPiece& B)
	{
		return A.Min.X <= B.Max.X && B.Min.X <= A.Max.X && A.Min.Y <= B.Max.Y && B.Min.Y <= A.Max.Y && A.Min.Z <= B.Max.Z && B.Min.Z <= A.Max.Z;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJigsawTest, "McClient.McGen.Jigsaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJigsawTest::RunTest(const FString& Parameters)
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

	// ---- Every jigsaw structure's pools read and assemble, the same way twice ----
	struct FCase
	{
		const TCHAR* Pool;
		const TCHAR* Anchor;
		int32 Size;
		int32 Distance;
		int32 Y;
	};
	const FCase Cases[] = {
		{ TEXT("minecraft:village/plains/town_centers"), TEXT(""), 6, 80, 64 },
		{ TEXT("minecraft:village/desert/town_centers"), TEXT(""), 6, 80, 64 },
		{ TEXT("minecraft:village/savanna/town_centers"), TEXT(""), 6, 80, 64 },
		{ TEXT("minecraft:village/snowy/town_centers"), TEXT(""), 6, 80, 64 },
		{ TEXT("minecraft:village/taiga/town_centers"), TEXT(""), 6, 80, 64 },
		{ TEXT("minecraft:pillager_outpost/base_plates"), TEXT(""), 7, 80, 64 },
		{ TEXT("minecraft:ancient_city/city_center"), TEXT("minecraft:city_anchor"), 7, 116, -27 },
		{ TEXT("minecraft:trail_ruins/tower"), TEXT(""), 7, 80, 60 },
		{ TEXT("minecraft:bastion/starts"), TEXT(""), 6, 80, 33 },
	};
	const double TestStart = FPlatformTime::Seconds();
	FMcJigsawLibrary Library;
	for (const FCase& Case : Cases)
	{
		if (!TestTrue(FString::Printf(TEXT("pool %s loads"), Case.Pool), Library.LoadPool(FName(Case.Pool), nullptr)))
		{
			continue;
		}
		int32 MostPieces = 0;
		for (int32 Trial = 0; Trial < 6; ++Trial)
		{
			FMcJigsawLibrary::FStart Start;
			Start.Pool = FName(Case.Pool);
			Start.StartJigsaw = FName(Case.Anchor);
			Start.MaxDepth = Case.Size;
			Start.MaxDistance = Case.Distance;
			Start.CentreX = 8 + 1600 * Trial;
			Start.CentreZ = 8;
			Start.Y = Case.Y;
			auto Flat = [](int32, int32) { return 64; };
			FMcRandom RandomA(1000 + Trial);
			FMcRandom RandomB(1000 + Trial);
			const TSharedPtr<FMcJigsawLibrary::FLayout> A = Library.Assemble(Start, RandomA, Flat);
			const TSharedPtr<FMcJigsawLibrary::FLayout> B = Library.Assemble(Start, RandomB, Flat);
			if (!A.IsValid() || !B.IsValid() || A->Pieces.IsEmpty())
			{
				AddError(FString::Printf(TEXT("%s: nothing assembled (trial %d)"), Case.Pool, Trial));
				continue;
			}
			bool bSame = A->Pieces.Num() == B->Pieces.Num();
			for (int32 Index = 0; bSame && Index < A->Pieces.Num(); ++Index)
			{
				bSame = A->Pieces[Index].Origin == B->Pieces[Index].Origin && A->Pieces[Index].Rotation == B->Pieces[Index].Rotation && A->Pieces[Index].Element == B->Pieces[Index].Element;
			}
			TestTrue(FString::Printf(TEXT("%s assembles the same twice"), Case.Pool), bSame);
			int32 Overlaps = 0;
			int32 Outside = 0;
			for (int32 I = 0; I < A->Pieces.Num(); ++I)
			{
				const FMcJigsawLibrary::FPiece& Piece = A->Pieces[I];
				if (FMath::Abs(Piece.Min.X - Start.CentreX) > Case.Distance || FMath::Abs(Piece.Max.X - Start.CentreX) > Case.Distance
					|| FMath::Abs(Piece.Min.Z - Start.CentreZ) > Case.Distance || FMath::Abs(Piece.Max.Z - Start.CentreZ) > Case.Distance)
				{
					++Outside;
				}
				for (int32 J = I + 1; J < A->Pieces.Num(); ++J)
				{
					// A piece may sit over the margin of the piece it grows from.
					const int32 ParentOfJ = A->Pieces[J].Parent;
						const bool bGrand = (ParentOfJ >= 0 && A->Pieces[ParentOfJ].Container == I) || (Piece.Parent >= 0 && A->Pieces[Piece.Parent].Container == J);
						Overlaps += (A->Pieces[J].Parent != I && Piece.Parent != J && !bGrand && BoxesTouch(Piece, A->Pieces[J])) ? 1 : 0;
				}
			}
			TestEqual(FString::Printf(TEXT("%s pieces within the distance"), Case.Pool), Outside, 0);
			TestEqual(FString::Printf(TEXT("%s pieces apart"), Case.Pool), Overlaps, 0);
			MostPieces = FMath::Max(MostPieces, A->Pieces.Num());
		}
		AddInfo(FString::Printf(TEXT("%s: up to %d pieces, %d pools read"), Case.Pool, MostPieces, Library.NumPools()));
		TestTrue(FString::Printf(TEXT("%s grows past its first piece"), Case.Pool), MostPieces > 1);
	}
	if (!Library.GetMissing().IsEmpty())
	{
		for (const FString& Id : Library.GetMissing())
		{
			// The jar itself names a template it lacks (ancient_city intact_horizontal_wall_stairs_5); the game skips such pieces.
			AddInfo(FString::Printf(TEXT("pool names template %s, which isn't in the jar"), *Id));
		}
	}

	// ---- Zombie villages come from the start pool's own zombie pieces ----
	{
		int32 Zombie = 0;
		for (int32 Trial = 0; Trial < 400; ++Trial)
		{
			FMcJigsawLibrary::FStart Start;
			Start.Pool = TEXT("minecraft:village/plains/town_centers");
			Start.MaxDepth = 1;
			Start.Y = 64;
			FMcRandom Random(5000 + Trial);
			const TSharedPtr<FMcJigsawLibrary::FLayout> Layout = Library.Assemble(Start, Random, [](int32, int32) { return 64; });
			Zombie += (Layout.IsValid() && !Layout->Pieces.IsEmpty() && Layout->Pieces[0].Element->Location.Contains(TEXT("/zombie/"))) ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("%d of 400 plains villages start as zombie villages"), Zombie));
		TestTrue(TEXT("some plains villages are zombie villages"), Zombie > 0 && Zombie < 100);
	}

	AddInfo(FString::Printf(TEXT("pool trials took %.1f s"), FPlatformTime::Seconds() - TestStart));

	// ---- The sets and structures ----
	FMcStructureLibrary Structures(Settings.SeaLevel, Settings.MinY);
	if (!TestTrue(TEXT("structure sets load"), Structures.Load()))
	{
		return false;
	}
	FString Unsupported;
	for (const TPair<FString, int32>& Pair : Structures.GetUnsupported())
	{
		Unsupported += FString::Printf(TEXT(" %s x%d"), *Pair.Key, Pair.Value);
	}
	AddInfo(FString::Printf(TEXT("not built:%s"), *Unsupported));
	TestFalse(TEXT("jigsaw structures are built now"), Structures.GetUnsupported().Contains(TEXT("jigsaw")) && Structures.GetUnsupported()[TEXT("jigsaw")] > 1);

	// ---- A village in a generated world, put down chunk by chunk ----
	// 55 is the first of these seeds with a village on flat land near the origin; the others are only fallbacks.
	for (const int32 Seed : { 55, 11, 22, 33, 44, 66, 77, 88 })
	{
		const FMcNoiseGenerator Generator(Settings, Seed, TEXT("minecraft:overworld"));
		TOptional<FIntPoint> Found;
		FName FoundId;
		int64 BestDistance = MAX_int64;
		for (int32 Z = -120; Z <= 120; ++Z)
		{
			for (int32 X = -120; X <= 120; ++X)
			{
				TArray<FName> Here;
				Structures.GetCandidates(Seed, FIntPoint(X, Z), Here);
				for (const FName Name : Here)
				{
					const int64 Distance = static_cast<int64>(X) * X + static_cast<int64>(Z) * Z;
					if (Name.ToString().StartsWith(TEXT("minecraft:village_")) && Distance < BestDistance && IsFlatLand(Generator, X * 16 + 8, Z * 16 + 8, Settings.SeaLevel)
						&& Structures.BiomeAllows(Name, Generator.GetBiome(X * 16 + 8, Settings.SeaLevel + 4, Z * 16 + 8)))
					{
						Found = FIntPoint(X, Z);
						FoundId = Name;
						BestDistance = Distance;
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("seed %d: start search done at %.1f s"), Seed, FPlatformTime::Seconds() - TestStart));
		if (!Found)
		{
			continue;
		}
		const FIntPoint Start = *Found;
		Structures.SetHeightSource([&Generator](int32 X, int32 Z) { return Generator.FindSurfaceY(X, Z) + 1; });
		Structures.SetBiomeSource([&Generator](int32 X, int32 Y, int32 Z) { return Generator.GetBiome(X, Y, Z); });
		constexpr int32 Radius = 6;
		constexpr int32 Size = Radius * 2 + 1;
		const FIntPoint Corner = Start - FIntPoint(Radius, Radius);
		TArray<TUniquePtr<FMcChunk>> Chunks;
		for (int32 Index = 0; Index < Size * Size; ++Index)
		{
			Chunks.Add(MakeUnique<FMcChunk>(Corner + FIntPoint(Index % Size, Index / Size), Settings.MinY, Settings.Height / 16));
			Generator.Generate(*Chunks.Last());
		}
		{
			// The height source has to agree with the chunks the generator makes.
			FMcChunk* Middle[9];
			for (int32 Slot = 0; Slot < 9; ++Slot)
			{
				Middle[Slot] = Chunks[(Radius - 1 + Slot / 3) * Size + Radius - 1 + Slot % 3].Get();
			}
			FMcGenRegion Probe(Start, Middle);
			int32 Differ = 0;
			int32 Worst = 0;
			for (int32 Z = Start.Y * 16 - 16; Z < Start.Y * 16 + 32; ++Z)
			{
				for (int32 X = Start.X * 16 - 16; X < Start.X * 16 + 32; ++X)
				{
					const int32 Difference = FMath::Abs(Probe.GetHeight(EMcHeightmap::WorldSurfaceWG, X, Z) - (Generator.FindSurfaceY(X, Z) + 1));
					Differ += Difference > 0 ? 1 : 0;
					Worst = FMath::Max(Worst, Difference);
				}
			}
			AddInfo(FString::Printf(TEXT("height source vs generated chunks: %d of 2304 columns differ, worst %d"), Differ, Worst));
		}
		{
			FMcJigsawLibrary Debug;
			Debug.LoadPool(TEXT("minecraft:village/desert/town_centers"), nullptr);
			FMcJigsawLibrary::FStart Params;
			Params.Pool = TEXT("minecraft:village/desert/town_centers");
			Params.CentreX = Start.X * 16 + 8;
			Params.CentreZ = Start.Y * 16 + 8;
			Params.Y = Generator.FindSurfaceY(Params.CentreX, Params.CentreZ) + 1;
			FMcRandom DebugRandom(5);
			const TSharedPtr<FMcJigsawLibrary::FLayout> Layout = Debug.Assemble(Params, DebugRandom, [&Generator](int32 X, int32 Z) { return Generator.FindSurfaceY(X, Z) + 1; });
			for (const FMcJigsawLibrary::FPiece& Piece : Layout->Pieces)
			{
				const int32 Ground = Generator.FindSurfaceY((Piece.Min.X + Piece.Max.X) / 2, (Piece.Min.Z + Piece.Max.Z) / 2) + 1;
				AddInfo(FString::Printf(TEXT("piece depth %d size %d,%d,%d match %d y %d..%d ground %d"), Piece.Depth, Piece.Element->Size.X, Piece.Element->Size.Y, Piece.Element->Size.Z, Piece.Element->bTerrainMatching ? 1 : 0, Piece.Min.Y, Piece.Max.Y, Ground));
			}
		}
		AddInfo(FString::Printf(TEXT("chunks generated and probed at %.1f s"), FPlatformTime::Seconds() - TestStart));
		TArray<TArray<FMcStateId>> Before;
		for (const TUniquePtr<FMcChunk>& Chunk : Chunks)
		{
			Before.Add(SnapshotChunk(*Chunk));
		}
		int32 Touched = 0;
		TSet<FString> Names;
		int32 LowestY = MAX_int32;
		int32 HighestY = MIN_int32;
		int32 Above = 0;
		int32 Blocks = 0;
		TSet<FMcStateId> Seen;
		TMap<int64, int32> Surface;  // the noise's ground per column, worked out once
		TArray<FMcStateId> CentreAfter;
		for (int32 CZ = 1; CZ < Size - 1; ++CZ)
		{
			for (int32 CX = 1; CX < Size - 1; ++CX)
			{
				FMcChunk* Around[9];
				for (int32 DZ = -1; DZ <= 1; ++DZ)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						Around[(DZ + 1) * 3 + DX + 1] = Chunks[(CZ + DZ) * Size + CX + DX].Get();
					}
				}
				FMcGenRegion Region(Corner + FIntPoint(CX, CZ), Around);
				Structures.Place(Region, Seed);
			}
		}
		AddInfo(FString::Printf(TEXT("placed at %.1f s"), FPlatformTime::Seconds() - TestStart));
		for (int32 Index = 0; Index < Chunks.Num(); ++Index)
		{
			const TArray<FMcStateId> After = SnapshotChunk(*Chunks[Index]);
			int32 Changed = 0;
			for (int32 B = 0; B < After.Num(); ++B)
			{
				if (After[B] != Before[Index][B])
				{
					++Changed;
					const int32 BlockY = Chunks[Index]->GetMinY() + B / 256;
					LowestY = FMath::Min(LowestY, BlockY);
					HighestY = FMath::Max(HighestY, BlockY);
					if (!FMcBlockRegistry::Get().IsAir(After[B]))
					{
						const int32 BlockX = Chunks[Index]->GetPos().X * 16 + (B % 16);
						const int32 BlockZ = Chunks[Index]->GetPos().Y * 16 + (B / 16) % 16;
						const int64 Key = (static_cast<int64>(BlockX) << 32) ^ static_cast<uint32>(BlockZ);
						int32* Ground = Surface.Find(Key);
						Above += BlockY > (Ground ? *Ground : Surface.Add(Key, Generator.FindSurfaceY(BlockX, BlockZ) + 1)) ? 1 : 0;
					}
					if (!Seen.Contains(After[B]))
					{
						Seen.Add(After[B]);
						Names.Add(FMcBlockRegistry::Get().Describe(After[B]));
					}
				}
			}
			Touched += Changed > 0 ? 1 : 0;
			Blocks += Changed;
		}
		AddInfo(FString::Printf(TEXT("seed %d: %s starts in chunk (%d, %d), %d blocks changed in %d chunks"), Seed, *FoundId.ToString(), Start.X, Start.Y, Blocks, Touched));
		if (const TSharedPtr<FMcJigsawLibrary::FLayout> Layout = Structures.FindLayout(FoundId, Seed, Start))
		{
			for (const FMcJigsawLibrary::FPiece& Piece : Layout->Pieces)
			{
				AddInfo(FString::Printf(TEXT("layout piece %s depth %d match %d box %d,%d,%d..%d,%d,%d ground %d"), *Piece.Element->Location, Piece.Depth, Piece.Element->bTerrainMatching ? 1 : 0,
					Piece.Min.X, Piece.Min.Y, Piece.Min.Z, Piece.Max.X, Piece.Max.Y, Piece.Max.Z, Generator.FindSurfaceY((Piece.Min.X + Piece.Max.X) / 2, (Piece.Min.Z + Piece.Max.Z) / 2) + 1));
			}
		}
		AddInfo(FString::Printf(TEXT("changed y %d..%d, %d solid blocks above the ground"), LowestY, HighestY, Above));
		AddInfo(FString::Printf(TEXT("blocks written: %s"), *FString::Join(Names, TEXT(", "))));
		TestTrue(TEXT("a village reaches over many chunks"), Touched > 20);

		AddInfo(FString::Printf(TEXT("compared at %.1f s"), FPlatformTime::Seconds() - TestStart));
		// A second library puts the same blocks in one chunk.
		FMcStructureLibrary Again(Settings.SeaLevel, Settings.MinY);
		Again.Load();
		Again.SetHeightSource([&Generator](int32 X, int32 Z) { return Generator.FindSurfaceY(X, Z) + 1; });
		Again.SetBiomeSource([&Generator](int32 X, int32 Y, int32 Z) { return Generator.GetBiome(X, Y, Z); });
		const int32 CentreIndex = Radius * Size + Radius;
		// Only the 3 x 3 round the start chunk: placing one chunk needs no more.
		TArray<TUniquePtr<FMcChunk>> Fresh;
		FMcChunk* Around[9];
		for (int32 DZ = -1; DZ <= 1; ++DZ)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				Fresh.Add(MakeUnique<FMcChunk>(Start + FIntPoint(DX, DZ), Settings.MinY, Settings.Height / 16));
				Generator.Generate(*Fresh.Last());
				Around[(DZ + 1) * 3 + DX + 1] = Fresh.Last().Get();
			}
		}
		FMcGenRegion Region(Start, Around);
		Again.Place(Region, Seed);
		TestTrue(TEXT("the same chunk comes out the same"), SnapshotChunk(*Fresh[4]) == SnapshotChunk(*Chunks[CentreIndex]));
		return true;
	}
	AddWarning(TEXT("no village start found near the origin in these seeds"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJigsawPieceCountTest, "McClient.McGen.JigsawPieceCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// Piece counts of villages on flat ground, 300 assemblies per kind, to set beside the reference's "mcref piece" lines.
bool FMcJigsawPieceCountTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const TCHAR* Pools[] = { TEXT("plains"), TEXT("desert"), TEXT("savanna"), TEXT("snowy"), TEXT("taiga") };
	FMcFeatureLibrary Features;
	if (!TestTrue(TEXT("features load"), Features.LoadBiomes()))
	{
		return false;
	}
	FMcJigsawLibrary Library;
	for (const TCHAR* Kind : { TEXT("desert"), TEXT("plains") })
	{
		FMcJigsawLibrary HouseLibrary;
		HouseLibrary.LoadPool(FName(*FString::Printf(TEXT("minecraft:village/%s/town_centers"), Kind)), &Features);
		for (const TCHAR* PoolName : { TEXT("houses"), TEXT("town_centers") })
		{
			if (const FMcJigsawLibrary::FPool* HousePool = HouseLibrary.GetPool(FName(*FString::Printf(TEXT("minecraft:village/%s/%s"), Kind, PoolName))))
			{
				for (const TPair<TSharedPtr<const FMcJigsawLibrary::FElement>, int32>& Entry : HousePool->Elements)
				{
					for (const FMcJigsawLibrary::FJigsaw& J : Entry.Key->Jigsaws)
					{
						if (J.Pool.ToString().Contains(TEXT("villagers")))
						{
							AddInfo(FString::Printf(TEXT("HOUSEJ %s %s size %d,%d,%d pos %d,%d,%d front %d top %d roll %d target %s"), Kind, *Entry.Key->Location, Entry.Key->Size.X, Entry.Key->Size.Y, Entry.Key->Size.Z, J.Pos.X, J.Pos.Y, J.Pos.Z, J.Front, J.Top, J.bRollable ? 1 : 0, *J.Target.ToString()));
						}
					}
				}
			}
		}
		if (const FMcJigsawLibrary::FPool* VillagerPool = HouseLibrary.GetPool(FName(*FString::Printf(TEXT("minecraft:village/%s/villagers"), Kind))))
		{
			for (const TPair<TSharedPtr<const FMcJigsawLibrary::FElement>, int32>& Entry : VillagerPool->Elements)
			{
				for (const FMcJigsawLibrary::FJigsaw& J : Entry.Key->Jigsaws)
				{
					AddInfo(FString::Printf(TEXT("VILLJ %s %s size %d,%d,%d pos %d,%d,%d front %d top %d name %s"), Kind, *Entry.Key->Location, Entry.Key->Size.X, Entry.Key->Size.Y, Entry.Key->Size.Z, J.Pos.X, J.Pos.Y, J.Pos.Z, J.Front, J.Top, *J.Name.ToString()));
				}
			}
		}
	}
	{
		FMcJigsawLibrary DecorLibrary;
		DecorLibrary.LoadPool(FName(TEXT("minecraft:village/plains/town_centers")), &Features);
		if (const FMcJigsawLibrary::FPool* StreetPool = DecorLibrary.GetPool(FName(TEXT("minecraft:village/plains/streets"))))
		{
			for (const TPair<TSharedPtr<const FMcJigsawLibrary::FElement>, int32>& Entry : StreetPool->Elements)
			{
				for (const FMcJigsawLibrary::FJigsaw& J : Entry.Key->Jigsaws)
				{
					AddInfo(FString::Printf(TEXT("STREETJ %s name %s target %s pool %s front %d top %d roll %d"), *Entry.Key->Location, *J.Name.ToString(), *J.Target.ToString(), *J.Pool.ToString(), J.Front, J.Top, J.bRollable ? 1 : 0));
				}
			}
		}
		for (const TCHAR* Id : { TEXT("minecraft:village/plains/decor"), TEXT("minecraft:village/plains/trees") })
		{
			if (const FMcJigsawLibrary::FPool* DecorPool = DecorLibrary.GetPool(FName(Id)))
			{
				for (const TPair<TSharedPtr<const FMcJigsawLibrary::FElement>, int32>& Entry : DecorPool->Elements)
				{
					for (const FMcJigsawLibrary::FJigsaw& J : Entry.Key->Jigsaws)
					{
						AddInfo(FString::Printf(TEXT("DECORJ %s name %s target %s pool %s front %d top %d roll %d"), *Entry.Key->Location, *J.Name.ToString(), *J.Target.ToString(), *J.Pool.ToString(), J.Front, J.Top, J.bRollable ? 1 : 0));
					}
					AddInfo(FString::Printf(TEXT("DECOR %s kind %d weight %d jigsaws %d %s"), Id, int32(Entry.Key->Kind), Entry.Value, Entry.Key->Jigsaws.Num(), *Entry.Key->Location));
				}
			}
			else
			{
				AddInfo(FString::Printf(TEXT("DECOR %s pool missing"), Id));
			}
		}
	}
	for (const TCHAR* Kind : Pools)
	{
		const FString Pool = FString::Printf(TEXT("minecraft:village/%s/town_centers"), Kind);
		if (!Library.LoadPool(FName(*Pool), &Features))
		{
			AddError(Pool);
			continue;
		}
		for (const int32 Depth : { 6, 7, 8, 9, 10 })
		{
			TArray<int32> Counts;
			int64 Singles = 0;
			int64 Volume = 0;
			int64 AllPieces = 0;
			TMap<FString, int32> ByName;
		TMap<FString, int32> VillagersByParent;
		TMap<FString, int32> ParentCount;
			for (int32 Trial = 0; Trial < 300; ++Trial)
			{
				FMcJigsawLibrary::FStart Start;
				Start.Pool = FName(*Pool);
				Start.MaxDepth = Depth;
				Start.MaxDistance = 80;
				Start.CentreX = 8;
				Start.CentreZ = 8;
				Start.Y = 64;
				FMcRandom Random(5000 + Trial);
				const TSharedPtr<FMcJigsawLibrary::FLayout> Layout = Library.Assemble(Start, Random, [](int32, int32) { return 64; });
				Counts.Add(Layout.IsValid() ? Layout->Pieces.Num() : 0);
				if (Layout.IsValid())
				{
					for (const FMcJigsawLibrary::FPiece& Piece : Layout->Pieces)
					{
						const int64 V = int64(Piece.Max.X - Piece.Min.X + 1) * (Piece.Max.Y - Piece.Min.Y + 1) * (Piece.Max.Z - Piece.Min.Z + 1);
						FString Name = Piece.Element->Kind == FMcJigsawLibrary::FElement::EKind::Single ? Piece.Element->Location : (Piece.Element->Kind == FMcJigsawLibrary::FElement::EKind::Feature ? FString(TEXT("feature")) : FString(TEXT("list:")) + (Piece.Element->Parts.IsEmpty() ? FString() : Piece.Element->Parts[0]->Location));
						++ByName.FindOrAdd(Name);
					if (Name.Contains(TEXT("/villagers/")) && Piece.Parent >= 0)
					{
						++VillagersByParent.FindOrAdd(Layout->Pieces[Piece.Parent].Element->Location);
					}
					if (Name.Contains(TEXT("/houses/")))
					{
						++ParentCount.FindOrAdd(Name);
					}
						Singles += V == 1;
						Volume += V;
						++AllPieces;
					}
				}
			}
			for (const TPair<FString, int32>& Pair : ByName)
			{
				AddInfo(FString::Printf(TEXT("PT %s d%d %s %.2f"), Kind, Depth, *Pair.Key, double(Pair.Value) / 300.0));
			}
			for (const TPair<FString, int32>& Pair : ParentCount)
			{
				AddInfo(FString::Printf(TEXT("VP %s d%d %s houses %.2f villagers %.2f"), Kind, Depth, *Pair.Key, double(Pair.Value) / 300.0, double(VillagersByParent.FindRef(Pair.Key)) / 300.0));
			}
			Counts.Sort();
			int64 Sum = 0;
			for (int32 C : Counts) { Sum += C; }
			AddInfo(FString::Printf(TEXT("PIECES %s d%d singles per start %.1f mean volume %.0f"), Kind, Depth, double(Singles) / Counts.Num(), double(Volume) / FMath::Max<int64>(AllPieces, 1)));
			AddInfo(FString::Printf(TEXT("PIECES %s d%d mean %.1f min %d p25 %d median %d p75 %d max %d"), Kind, Depth, double(Sum) / Counts.Num(), Counts[0], Counts[75], Counts[150], Counts[225], Counts.Last()));
		}
	}
	// Diagnostic: how the plains mean piece count moves with the depth and distance limits.
	{
		const FString Pool = TEXT("minecraft:village/plains/town_centers");
		Library.LoadPool(FName(*Pool), &Features);
		const int32 Limits[][2] = { { 6, 80 }, { 6, 128 }, { 8, 80 }, { 8, 128 }, { 10, 200 } };
		for (const int32(&Limit)[2] : Limits)
		{
			int64 Sum = 0;
			for (int32 Trial = 0; Trial < 150; ++Trial)
			{
				FMcJigsawLibrary::FStart Start;
				Start.Pool = FName(*Pool);
				Start.MaxDepth = Limit[0];
				Start.MaxDistance = Limit[1];
				Start.CentreX = 8;
				Start.CentreZ = 8;
				Start.Y = 64;
				FMcRandom Random(9000 + Trial);
				const TSharedPtr<FMcJigsawLibrary::FLayout> Layout = Library.Assemble(Start, Random, [](int32, int32) { return 64; });
				Sum += Layout.IsValid() ? Layout->Pieces.Num() : 0;
			}
			AddInfo(FString::Printf(TEXT("PLIM depth %d distance %d mean %.1f"), Limit[0], Limit[1], double(Sum) / 150.0));
		}
	}
	return true;
}

#endif
