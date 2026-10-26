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
					Overlaps += BoxesTouch(Piece, A->Pieces[J]) ? 1 : 0;
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
		constexpr int32 Radius = 7;
		constexpr int32 Size = Radius * 2 + 1;
		const FIntPoint Corner = Start - FIntPoint(Radius, Radius);
		TArray<TUniquePtr<FMcChunk>> Chunks;
		for (int32 Index = 0; Index < Size * Size; ++Index)
		{
			Chunks.Add(MakeUnique<FMcChunk>(Corner + FIntPoint(Index % Size, Index / Size), Settings.MinY, Settings.Height / 16));
			Generator.Generate(*Chunks.Last());
		}
		TArray<TArray<FMcStateId>> Before;
		for (const TUniquePtr<FMcChunk>& Chunk : Chunks)
		{
			Before.Add(SnapshotChunk(*Chunk));
		}
		int32 Touched = 0;
		TSet<FString> Names;
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
					Names.Add(FMcBlockRegistry::Get().Describe(After[B]));
				}
			}
			Touched += Changed > 0 ? 1 : 0;
			Blocks += Changed;
		}
		AddInfo(FString::Printf(TEXT("seed %d: %s starts in chunk (%d, %d), %d blocks changed in %d chunks"), Seed, *FoundId.ToString(), Start.X, Start.Y, Blocks, Touched));
		AddInfo(FString::Printf(TEXT("blocks written: %s"), *FString::Join(Names, TEXT(", "))));
		TestTrue(TEXT("a village reaches over several chunks"), Touched > 4);

		// A second library puts the same blocks in one chunk.
		FMcStructureLibrary Again(Settings.SeaLevel, Settings.MinY);
		Again.Load();
		Again.SetHeightSource([&Generator](int32 X, int32 Z) { return Generator.FindSurfaceY(X, Z) + 1; });
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
