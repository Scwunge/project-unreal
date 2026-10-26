#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
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
					Overlaps += (A->Pieces[J].Parent != I && Piece.Parent != J && BoxesTouch(Piece, A->Pieces[J])) ? 1 : 0;
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
	for (const int32 Seed : { 11, 22, 33, 44, 55, 66, 77, 88 })
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
		if (!Found)
		{
			continue;
		}
		const FIntPoint Start = *Found;
		Structures.SetHeightSource([&Generator](int32 X, int32 Z) { return Generator.FindSurfaceY(X, Z) + 1; });
		Structures.SetBiomeSource([&Generator](int32 X, int32 Y, int32 Z) { return Generator.GetBiome(X, Y, Z); });
		constexpr int32 Radius = 7;
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
					Above += (BlockY > Generator.FindSurfaceY(Chunks[Index]->GetPos().X * 16 + (B % 16), Chunks[Index]->GetPos().Y * 16 + (B / 16) % 16) + 1 && !FMcBlockRegistry::Get().IsAir(After[B])) ? 1 : 0;
					Names.Add(FMcBlockRegistry::Get().Describe(After[B]));
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

		// A second library puts the same blocks in one chunk.
		FMcStructureLibrary Again(Settings.SeaLevel, Settings.MinY);
		Again.Load();
		Again.SetHeightSource([&Generator](int32 X, int32 Z) { return Generator.FindSurfaceY(X, Z) + 1; });
		Again.SetBiomeSource([&Generator](int32 X, int32 Y, int32 Z) { return Generator.GetBiome(X, Y, Z); });
		const int32 CentreIndex = Radius * Size + Radius;
		TArray<TUniquePtr<FMcChunk>> Fresh;
		for (int32 Index = 0; Index < Size * Size; ++Index)
		{
			Fresh.Add(MakeUnique<FMcChunk>(Corner + FIntPoint(Index % Size, Index / Size), Settings.MinY, Settings.Height / 16));
			Generator.Generate(*Fresh.Last());
		}
		FMcChunk* Around[9];
		for (int32 DZ = -1; DZ <= 1; ++DZ)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				Around[(DZ + 1) * 3 + DX + 1] = Fresh[(Radius + DZ) * Size + Radius + DX].Get();
			}
		}
		FMcGenRegion Region(Start, Around);
		Again.Place(Region, Seed);
		TestTrue(TEXT("the same chunk comes out the same"), SnapshotChunk(*Fresh[CentreIndex]) == SnapshotChunk(*Chunks[CentreIndex]));
		return true;
	}
	AddWarning(TEXT("no village start found near the origin in these seeds"));
	return true;
}

#endif
