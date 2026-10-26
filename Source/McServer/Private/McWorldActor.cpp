#include "McWorldActor.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McFlatGenerator.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McNoiseGenerator.h"
#include "McServer.h"
#include "Misc/CommandLine.h"
#include "Tasks/Task.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

AMcWorldActor::AMcWorldActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// Clients get one too, holding the chunks streamed to them.
	bReplicates = true;
	bAlwaysRelevant = true;
}

AMcWorldActor::~AMcWorldActor() = default;

void AMcWorldActor::BeginPlay()
{
	Super::BeginPlay();

	FMcDimensionType DimensionType;
	if (!HasAuthority())
	{
		// A client: an empty world that McNet fills with what the server sends.
		if (FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcDimensionType::Load(Dimension, DimensionType))
		{
			BlockWorld = MakeUnique<FMcBlockWorld>(DimensionType);
		}
		else
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't join: cache/ is missing or incomplete. Run tools/fetch_cache.py."));
		}
		return;
	}
	if (!FMcBlockRegistry::Get().EnsureLoaded() || !FMcBiomeRegistry::Get().EnsureLoaded() || !FMcDimensionType::Load(Dimension, DimensionType))
	{
		UE_LOG(LogMcServer, Error, TEXT("Can't start the world: cache/ is missing or incomplete. Run tools/fetch_cache.py."));
		return;
	}

	const TCHAR* CommandLine = FCommandLine::Get();
	FString Scene;
	bFlat |= FParse::Param(CommandLine, TEXT("McFlat")) || FParse::Value(CommandLine, TEXT("-McScene="), Scene);
	FParse::Value(CommandLine, TEXT("-McSeed="), Seed);
	if (Seed == 0)
	{
		Seed = static_cast<int64>(FPlatformTime::Cycles64() ^ (static_cast<uint64>(FDateTime::Now().GetTicks()) << 7));
	}

	FString Kind;
	if (bFlat)
	{
		FMcFlatSettings Settings;
		if (!FMcFlatSettings::LoadPreset(FlatPreset, Settings))
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't start the world: flat preset %s didn't load."), *FlatPreset);
			return;
		}
		Generator = MakeShared<FMcFlatGenerator, ESPMode::ThreadSafe>(Settings, DimensionType.MinY);
		Kind = TEXT("flat preset ") + FlatPreset;
	}
	else
	{
		FMcNoiseSettings Settings;
		TSharedPtr<FMcNoiseGenerator, ESPMode::ThreadSafe> Noise;
		if (FMcNoiseSettings::Load(NoiseSettings, Settings))
		{
			Noise = MakeShared<FMcNoiseGenerator, ESPMode::ThreadSafe>(Settings, static_cast<uint64>(Seed), NoiseSettings);
		}
		if (!Noise || !Noise->IsValid())
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't start the world: noise settings %s didn't load."), *NoiseSettings);
			return;
		}
		Generator = Noise;
		Features = MakeShared<FMcFeatureLibrary>(Settings.SeaLevel, Settings.MinY, Settings.Height);
		if (!FMcBlockShapes::Get().EnsureLoaded() || !FMcBlockTags::Get().EnsureLoaded() || !Features->LoadBiomes())
		{
			UE_LOG(LogMcServer, Warning, TEXT("Some biome features didn't load; those are left out."));
		}
		Kind = FString::Printf(TEXT("%s terrain, seed %lld"), *NoiseSettings, Seed);
	}

	BlockWorld = MakeUnique<FMcBlockWorld>(DimensionType);
	Finished = MakeShared<TQueue<TUniquePtr<FMcChunk>, EQueueMode::Mpsc>, ESPMode::ThreadSafe>();
	SurfaceY = Generator->FindSurfaceY(0, 0);
	UE_LOG(LogMcServer, Log, TEXT("World ready: %s, %s, %d sections from y=%d, surface at spawn y=%d"),
		*Dimension, *Kind, DimensionType.NumSections(), DimensionType.MinY, SurfaceY);
}

void AMcWorldActor::SetBlock(const FIntVector& Pos, FMcStateId State)
{
	check(BlockWorld);
	const FIntPoint ChunkPos(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z));
	Edits.FindOrAdd(ChunkPos).Add(Pos, State);
	BlockWorld->SetBlock(Pos, State);
}

void AMcWorldActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (BlockWorld && Generator)
	{
		StreamChunks();
	}
}

void AMcWorldActor::StreamChunks()
{
	const double ChunkUnits = McCoords::SectionSize * McCoords::UnitsPerBlock;

	TArray<FIntPoint> Centres;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (Pawn)
		{
			const FVector Location = Pawn->GetActorLocation();
			Centres.Add(FIntPoint(FMath::FloorToInt32(Location.X / ChunkUnits), FMath::FloorToInt32(Location.Y / ChunkUnits)));
		}
	}

	// Load one ring past the view distance: the renderer only meshes chunks whose neighbours are all loaded.
	// Decorating needs two more rings generated around that.
	const int32 LoadRadius = ViewDistance + 1 + (Features ? 2 : 0);
	// Keep a margin before unloading so flying back and forth over a border doesn't churn.
	const int32 KeepRadius = ViewDistance + 3;

	TArray<TPair<int32, FIntPoint>> Missing;
	for (const FIntPoint& Centre : Centres)
	{
		for (int32 DZ = -LoadRadius; DZ <= LoadRadius; ++DZ)
		{
			for (int32 DX = -LoadRadius; DX <= LoadRadius; ++DX)
			{
				const FIntPoint Pos = Centre + FIntPoint(DX, DZ);
				if (!BlockWorld->HasChunk(Pos) && !Pending.Contains(Pos))
				{
					Missing.Add({ DX * DX + DZ * DZ, Pos });
				}
			}
		}
	}
	Missing.Sort([](const TPair<int32, FIntPoint>& A, const TPair<int32, FIntPoint>& B) { return A.Key < B.Key; });

	AddGeneratedChunks();
	DecorateChunks();

	for (const TPair<int32, FIntPoint>& Entry : Missing)
	{
		if (InFlight.Num() >= MaxChunksInFlight)
		{
			break;
		}
		// Two players can want the same chunk.
		if (InFlight.Contains(Entry.Value) || BlockWorld->HasChunk(Entry.Value) || Pending.Contains(Entry.Value))
		{
			continue;
		}
		InFlight.Add(Entry.Value);
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [Generator = Generator, Finished = Finished, Chunk = BlockWorld->MakeChunk(Entry.Value)]() mutable
		{
			Generator->Generate(*Chunk);
			Finished->Enqueue(MoveTemp(Chunk));
		});
	}

	TArray<FIntPoint> Far;
	for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Chunk : BlockWorld->GetChunks())
	{
		const bool bNearAnyone = Centres.ContainsByPredicate([&Chunk, KeepRadius](const FIntPoint& Centre)
		{
			return FMath::Abs(Chunk.Key.X - Centre.X) <= KeepRadius && FMath::Abs(Chunk.Key.Y - Centre.Y) <= KeepRadius;
		});
		if (!bNearAnyone)
		{
			Far.Add(Chunk.Key);
		}
	}
	for (const FIntPoint& Pos : Far)
	{
		BlockWorld->RemoveChunk(Pos);
	}
	for (auto It = Pending.CreateIterator(); It; ++It)
	{
		const FIntPoint Pos = It.Key();
		const bool bNearAnyone = Centres.ContainsByPredicate([&Pos, KeepRadius](const FIntPoint& Centre)
		{
			return FMath::Abs(Pos.X - Centre.X) <= KeepRadius + 2 && FMath::Abs(Pos.Y - Centre.Y) <= KeepRadius + 2;
		});
		if (!bNearAnyone)
		{
			Decorated.Remove(Pos);
			It.RemoveCurrent();
		}
	}
}

void AMcWorldActor::AddGeneratedChunks()
{
	TUniquePtr<FMcChunk> Chunk;
	while (Finished->Dequeue(Chunk))
	{
		const FIntPoint Pos = Chunk->GetPos();
		InFlight.Remove(Pos);
		if (BlockWorld->HasChunk(Pos) || Pending.Contains(Pos))
		{
			continue;
		}
		if (Features)
		{
			Pending.Add(Pos, MoveTemp(Chunk));
			continue;
		}
		if (const TMap<FIntVector, FMcStateId>* ChunkEdits = Edits.Find(Pos))
		{
			for (const TPair<FIntVector, FMcStateId>& Edit : *ChunkEdits)
			{
				Chunk->SetBlock(McCoords::BlockToLocal(Edit.Key.X), Edit.Key.Y, McCoords::BlockToLocal(Edit.Key.Z), Edit.Value);
			}
		}
		BlockWorld->AddChunk(MoveTemp(Chunk));
	}
}

void AMcWorldActor::DecorateChunks()
{
	if (!Features || Pending.IsEmpty())
	{
		return;
	}
	// The chunks around Pos, row by row from the north-west, and which of them players already have.
	const auto Gather = [this](FIntPoint Pos, FMcChunk** Out, bool* bLive)
	{
		bool bAll = true;
		for (int32 Index = 0; Index < 9; ++Index)
		{
			const FIntPoint At = Pos + FIntPoint(Index % 3 - 1, Index / 3 - 1);
			const TUniquePtr<FMcChunk>* Waiting = Pending.Find(At);
			Out[Index] = Waiting ? Waiting->Get() : BlockWorld->FindChunk(At);
			bLive[Index] = !Waiting && Out[Index];
			bAll &= Out[Index] != nullptr;
		}
		return bAll;
	};

	const double Deadline = FPlatformTime::Seconds() + 0.006;
	for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Entry : Pending)
	{
		if (FPlatformTime::Seconds() > Deadline)
		{
			break;
		}
		if (Decorated.Contains(Entry.Key))
		{
			continue;
		}
		FMcChunk* Around[9];
		bool bLive[9];
		if (!Gather(Entry.Key, Around, bLive))
		{
			continue;
		}
		FMcGenRegion Region(Entry.Key, Around);
		for (int32 Index = 0; Index < 9; ++Index)
		{
			if (bLive[Index])
			{
				Region.SetReadOnly(Index);
			}
		}
		Features->Decorate(Region, static_cast<uint64>(Seed));
		Decorated.Add(Entry.Key);
	}

	// Decorated chunks whose neighbours are all decorated (or already in the world) are finished.
	TArray<FIntPoint> Ready;
	for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Entry : Pending)
	{
		bool bDone = Decorated.Contains(Entry.Key);
		for (int32 Index = 0; Index < 9 && bDone; ++Index)
		{
			const FIntPoint At = Entry.Key + FIntPoint(Index % 3 - 1, Index / 3 - 1);
			bDone = Decorated.Contains(At) || BlockWorld->HasChunk(At);
		}
		if (bDone)
		{
			Ready.Add(Entry.Key);
		}
	}
	for (const FIntPoint& Pos : Ready)
	{
		TUniquePtr<FMcChunk> Chunk = MoveTemp(Pending.FindChecked(Pos));
		Pending.Remove(Pos);
		Decorated.Remove(Pos);
		if (const TMap<FIntVector, FMcStateId>* ChunkEdits = Edits.Find(Pos))
		{
			for (const TPair<FIntVector, FMcStateId>& Edit : *ChunkEdits)
			{
				Chunk->SetBlock(McCoords::BlockToLocal(Edit.Key.X), Edit.Key.Y, McCoords::BlockToLocal(Edit.Key.Z), Edit.Value);
			}
		}
		BlockWorld->AddChunk(MoveTemp(Chunk));
	}
}
