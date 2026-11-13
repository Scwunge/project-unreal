// Chunks decorate on worker threads in the game. Two separate questions, and this test keeps them apart:
//   Races: the same order of work, run on one thread or on many at once, has to give the same blocks. Any difference
//     is threads disturbing each other, and fails the test.
//   Order: two overlapping chunks decorated the other way round can give different blocks, on one thread as well, and
//     workers finish in whatever order they finish. How much that changes is measured and reported, not failed.
#include "Async/ParallelFor.h"
#include "Containers/Queue.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McNoiseGenerator.h"
#include "McStructures.h"
#include "Misc/AutomationTest.h"
#include "Tasks/Task.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDecorateThreadsTest, "McClient.McGen.DecorateThreads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	constexpr int32 Across = 11;
	constexpr uint64 WorldSeed = 12;

	using FPatch = TArray<TUniquePtr<FMcChunk>>;

	struct FWork
	{
		const FPatch* Generated = nullptr;
		FIntPoint Corner = FIntPoint::ZeroValue;
		const FMcNoiseSettings* Settings = nullptr;
		const FMcNoiseGenerator* Generator = nullptr;

		// As the game makes it, so structures find their ground and biomes from far off.
		TSharedRef<FMcFeatureLibrary> NewLibrary() const
		{
			TSharedRef<FMcFeatureLibrary> Library = MakeShared<FMcFeatureLibrary>(Settings->SeaLevel, Settings->MinY, Settings->Height);
			Library->LoadBiomes();
			const FMcNoiseGenerator* Noise = Generator;
			Library->SetHeightSource([Noise](int32 X, int32 Z) { return Noise->FindSurfaceY(X, Z) + 1; });
			Library->SetBiomeSource([Noise](int32 X, int32 Y, int32 Z) { return Noise->GetBiome(X, Y, Z); });
			return Library;
		}

		FPatch Copy() const
		{
			FPatch Out;
			for (const TUniquePtr<FMcChunk>& Chunk : *Generated)
			{
				Out.Add(MakeUnique<FMcChunk>(*Chunk));
			}
			return Out;
		}

		void DecorateOne(const FMcFeatureLibrary& Library, FPatch& Patch, FIntPoint Pos) const
		{
			FMcChunk* Around[9];
			for (int32 Index = 0; Index < 9; ++Index)
			{
				const FIntPoint At = Pos - Corner + FIntPoint(Index % 3 - 1, Index / 3 - 1);
				Around[Index] = Patch[At.Y * Across + At.X].Get();
			}
			FMcGenRegion Region(Pos, Around);
			Library.Decorate(Region, WorldSeed);
		}

		FPatch Serial(const FMcFeatureLibrary& Library, const TArray<FIntPoint>& Order) const
		{
			FPatch Patch = Copy();
			for (const FIntPoint& Pos : Order)
			{
				DecorateOne(Library, Patch, Pos);
			}
			return Patch;
		}

		// As many at once as can be while every overlapping pair still goes in the given order.
		FPatch TogetherInOrder(const FMcFeatureLibrary& Library, const TArray<FIntPoint>& Order) const
		{
			FPatch Patch = Copy();
			TArray<UE::Tasks::FTask> Tasks;
			for (int32 Job = 0; Job < Order.Num(); ++Job)
			{
				TArray<UE::Tasks::FTask> Before;
				for (int32 Earlier = 0; Earlier < Job; ++Earlier)
				{
					if (FMath::Abs(Order[Earlier].X - Order[Job].X) <= 2 && FMath::Abs(Order[Earlier].Y - Order[Job].Y) <= 2)
					{
						Before.Add(Tasks[Earlier]);
					}
				}
				Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, &Library, &Patch, Pos = Order[Job]]() { DecorateOne(Library, Patch, Pos); }, Before));
			}
			UE::Tasks::Wait(Tasks);
			return Patch;
		}

		// The game's scheduling: up to Jobs at once, the first waiting chunk free of other jobs next, whenever one finishes.
		FPatch FreeRunning(const FMcFeatureLibrary& Library, const TArray<FIntPoint>& Order, int32 Jobs) const
		{
			FPatch Patch = Copy();
			TSet<FIntPoint> Busy;
			TSet<FIntPoint> Started;
			TQueue<FIntPoint, EQueueMode::Mpsc> Back;
			TArray<UE::Tasks::FTask> Tasks;
			int32 Running = 0;
			int32 Finished = 0;
			while (Finished < Order.Num())
			{
				FIntPoint DonePos;
				while (Back.Dequeue(DonePos))
				{
					for (int32 Index = 0; Index < 9; ++Index)
					{
						Busy.Remove(DonePos + FIntPoint(Index % 3 - 1, Index / 3 - 1));
					}
					--Running;
					++Finished;
				}
				for (const FIntPoint& Pos : Order)
				{
					if (Running >= Jobs)
					{
						break;
					}
					if (Started.Contains(Pos))
					{
						continue;
					}
					bool bFree = true;
					for (int32 Index = 0; Index < 9 && bFree; ++Index)
					{
						bFree = !Busy.Contains(Pos + FIntPoint(Index % 3 - 1, Index / 3 - 1));
					}
					if (!bFree)
					{
						continue;
					}
					for (int32 Index = 0; Index < 9; ++Index)
					{
						Busy.Add(Pos + FIntPoint(Index % 3 - 1, Index / 3 - 1));
					}
					Started.Add(Pos);
					++Running;
					Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, &Library, &Patch, &Back, Pos]()
					{
						DecorateOne(Library, Patch, Pos);
						Back.Enqueue(Pos);
					}));
				}
				FPlatformProcess::Sleep(0.0002f);
			}
			UE::Tasks::Wait(Tasks);
			return Patch;
		}
	};

	struct FDifference
	{
		int32 Chunks = 0;
		int64 Blocks = 0;
		int32 EntityChunks = 0;
		bool IsNone() const { return Chunks == 0 && EntityChunks == 0; }
		FString Describe() const { return FString::Printf(TEXT("%d chunks and %lld blocks differ, block entities differ in %d chunks"), Chunks, Blocks, EntityChunks); }
	};

	FDifference Compare(const FPatch& A, const FPatch& B)
	{
		FDifference Out;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			const FMcChunk& First = *A[Index];
			const FMcChunk& Second = *B[Index];
			int64 Here = 0;
			for (int32 Y = First.GetMinY(); Y <= First.GetMaxY(); ++Y)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 X = 0; X < 16; ++X)
					{
						Here += First.GetBlock(X, Y, Z) != Second.GetBlock(X, Y, Z) ? 1 : 0;
					}
				}
			}
			Out.Chunks += Here > 0 ? 1 : 0;
			Out.Blocks += Here;
			if (First.GetBlockEntities().Num() != Second.GetBlockEntities().Num() || First.EncodeBlockEntities() != Second.EncodeBlockEntities())
			{
				++Out.EntityChunks;
			}
		}
		return Out;
	}
}

bool FMcDecorateThreadsTest::RunTest(const FString& Parameters)
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
	const FMcNoiseGenerator Generator(Settings, WorldSeed, TEXT("minecraft:overworld"));

	// Round the world's start, then round the nearest starts of structures that reach across chunks and use the shared caches.
	TArray<TPair<FString, FIntPoint>> Places;
	Places.Add(TPair<FString, FIntPoint>(TEXT("start"), FIntPoint(-Across / 2, -Across / 2)));
	{
		FMcFeatureLibrary Features(Settings.SeaLevel, Settings.MinY, Settings.Height);
		Features.LoadBiomes();
		FMcStructureLibrary Structures(Settings.SeaLevel, Settings.MinY);
		Structures.Load(&Features);
		const TCHAR* Wanted[] = { TEXT("minecraft:village_plains"), TEXT("minecraft:mineshaft"), TEXT("minecraft:desert_pyramid") };
		TArray<FName> Here;
		for (const TCHAR* Id : Wanted)
		{
			bool bFound = false;
			for (int32 Ring = 1; Ring < 200 && !bFound; ++Ring)
			{
				for (int32 Z = -Ring; Z <= Ring && !bFound; ++Z)
				{
					for (int32 X = -Ring; X <= Ring && !bFound; ++X)
					{
						if (FMath::Max(FMath::Abs(X), FMath::Abs(Z)) != Ring)
						{
							continue;
						}
						Here.Reset();
						Structures.GetCandidates(WorldSeed, FIntPoint(X, Z), Here);
						if (Here.Contains(FName(Id)))
						{
							bFound = true;
							Places.Add(TPair<FString, FIntPoint>(Id, FIntPoint(X - Across / 2, Z - Across / 2)));
						}
					}
				}
			}
		}
	}

	int64 EntitiesPlaced = 0;
	for (const TPair<FString, FIntPoint>& Place : Places)
	{
		FPatch Generated;
		Generated.SetNum(Across * Across);
		ParallelFor(Across * Across, [&](int32 Index)
		{
			Generated[Index] = MakeUnique<FMcChunk>(Place.Value + FIntPoint(Index % Across, Index / Across), Settings.MinY, Settings.Height / 16);
			Generator.Generate(*Generated[Index]);
		});
		FWork Work;
		Work.Generated = &Generated;
		Work.Corner = Place.Value;
		Work.Settings = &Settings;
		Work.Generator = &Generator;

		TArray<FIntPoint> Raster;
		for (int32 Z = 1; Z < Across - 1; ++Z)
		{
			for (int32 X = 1; X < Across - 1; ++X)
			{
				Raster.Add(Place.Value + FIntPoint(X, Z));
			}
		}
		TArray<FIntPoint> Shuffled = Raster;
		FRandomStream Shuffle(4242);
		for (int32 Index = Shuffled.Num() - 1; Index > 0; --Index)
		{
			Shuffled.Swap(Index, Shuffle.RandRange(0, Index));
		}

		const double Began = FPlatformTime::Seconds();
		const FPatch Reference = Work.Serial(*Work.NewLibrary(), Shuffled);
		const double OneThread = FPlatformTime::Seconds() - Began;
		for (const TUniquePtr<FMcChunk>& Chunk : Reference)
		{
			EntitiesPlaced += Chunk->GetBlockEntities().Num();
		}

		// Races and leftovers: nothing here may differ at all.
		const FDifference Again = Compare(Reference, Work.Serial(*Work.NewLibrary(), Shuffled));
		TestTrue(FString::Printf(TEXT("%s: the same order on one thread twice (%s)"), *Place.Key, *Again.Describe()), Again.IsNone());
		// New caches, so the threads are the ones to fill them.
		TSharedRef<FMcFeatureLibrary> Shared = Work.NewLibrary();
		const double TogetherBegan = FPlatformTime::Seconds();
		const FDifference Cold = Compare(Reference, Work.TogetherInOrder(*Shared, Shuffled));
		const double Together = FPlatformTime::Seconds() - TogetherBegan;
		TestTrue(FString::Printf(TEXT("%s: the same order on many threads, new caches (%s)"), *Place.Key, *Cold.Describe()), Cold.IsNone());
		for (int32 Round = 0; Round < 2; ++Round)
		{
			const FDifference Warm = Compare(Reference, Work.TogetherInOrder(*Shared, Shuffled));
			TestTrue(FString::Printf(TEXT("%s: the same order on many threads, round %d (%s)"), *Place.Key, Round, *Warm.Describe()), Warm.IsNone());
		}

		// Order: measured.
		const FPatch InRaster = Work.Serial(*Work.NewLibrary(), Raster);
		const FPatch Free1 = Work.FreeRunning(*Work.NewLibrary(), Raster, 3);
		const FPatch Free2 = Work.FreeRunning(*Work.NewLibrary(), Raster, 3);
		const FString Line = FString::Printf(TEXT("DECORATE_ORDER %s at %d,%d: %.1f s one thread, %.1f s together; another order on one thread: %s; game's scheduling vs one thread: %s; game's scheduling twice: %s"),
			*Place.Key, Place.Value.X, Place.Value.Y, OneThread, Together,
			*Compare(Reference, InRaster).Describe(), *Compare(InRaster, Free1).Describe(), *Compare(Free1, Free2).Describe());
		AddInfo(Line);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
	}
	const FString Summary = FString::Printf(TEXT("DECORATE_ORDER places %d, block entities placed %lld"), Places.Num(), EntitiesPlaced);
	AddInfo(Summary);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Summary);
	TestTrue(TEXT("structures were among what got decorated"), EntitiesPlaced > 0);
	return true;
}

#endif
