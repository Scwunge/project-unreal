#include "McWorldActor.h"
#include "McBlockRegistry.h"
#include "McServer.h"
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
	FMcFlatSettings Settings;
	if (!HasAuthority())
	{
		// A client: an empty world that McNet fills with what the server sends.
		if (FMcBlockRegistry::Get().EnsureLoaded() && FMcDimensionType::Load(Dimension, DimensionType))
		{
			BlockWorld = MakeUnique<FMcBlockWorld>(DimensionType);
		}
		else
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't join: cache/ is missing or incomplete. Run tools/fetch_cache.py."));
		}
		return;
	}
	if (!FMcBlockRegistry::Get().EnsureLoaded()
		|| !FMcDimensionType::Load(Dimension, DimensionType)
		|| !FMcFlatSettings::LoadPreset(FlatPreset, Settings))
	{
		UE_LOG(LogMcServer, Error, TEXT("Can't start the world: cache/ is missing or incomplete. Run tools/fetch_cache.py."));
		return;
	}

	BlockWorld = MakeUnique<FMcBlockWorld>(DimensionType);
	Generator = MakeUnique<FMcFlatGenerator>(Settings);
	SurfaceY = Generator->GetSurfaceY(DimensionType.MinY);
	UE_LOG(LogMcServer, Log, TEXT("World ready: %s, flat preset %s, %d sections from y=%d, surface at y=%d"),
		*Dimension, *FlatPreset, DimensionType.NumSections(), DimensionType.MinY, SurfaceY);
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
	const int32 LoadRadius = ViewDistance + 1;
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
				if (!BlockWorld->HasChunk(Pos))
				{
					Missing.Add({ DX * DX + DZ * DZ, Pos });
				}
			}
		}
	}
	Missing.Sort([](const TPair<int32, FIntPoint>& A, const TPair<int32, FIntPoint>& B) { return A.Key < B.Key; });

	int32 Generated = 0;
	for (const TPair<int32, FIntPoint>& Entry : Missing)
	{
		if (Generated >= MaxChunksGeneratedPerTick)
		{
			break;
		}
		// Two players can want the same chunk.
		if (BlockWorld->HasChunk(Entry.Value))
		{
			continue;
		}
		TUniquePtr<FMcChunk> Chunk = BlockWorld->MakeChunk(Entry.Value);
		Generator->Generate(*Chunk);
		if (const TMap<FIntVector, FMcStateId>* ChunkEdits = Edits.Find(Entry.Value))
		{
			for (const TPair<FIntVector, FMcStateId>& Edit : *ChunkEdits)
			{
				Chunk->SetBlock(McCoords::BlockToLocal(Edit.Key.X), Edit.Key.Y, McCoords::BlockToLocal(Edit.Key.Z), Edit.Value);
			}
		}
		BlockWorld->AddChunk(MoveTemp(Chunk));
		++Generated;
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
}
