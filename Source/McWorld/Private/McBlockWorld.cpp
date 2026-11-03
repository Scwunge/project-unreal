#include "McBlockWorld.h"

FMcBlockWorld::FMcBlockWorld(const FMcDimensionType& InDimension)
	: Dimension(InDimension)
{
}

TUniquePtr<FMcChunk> FMcBlockWorld::MakeChunk(FIntPoint Pos) const
{
	return MakeUnique<FMcChunk>(Pos, Dimension.MinY, Dimension.NumSections());
}

FMcChunk& FMcBlockWorld::AddChunk(TUniquePtr<FMcChunk> Chunk)
{
	check(IsInGameThread());
	check(Chunk.IsValid());
	const FIntPoint Pos = Chunk->GetPos();
	FMcChunk& Added = *Chunks.Add(Pos, MoveTemp(Chunk));
	OnChunkAdded.Broadcast(Pos);
	return Added;
}

void FMcBlockWorld::RemoveChunk(FIntPoint Pos)
{
	check(IsInGameThread());
	if (Chunks.Remove(Pos) > 0)
	{
		OnChunkRemoved.Broadcast(Pos);
	}
}

FMcChunk* FMcBlockWorld::FindChunk(FIntPoint Pos)
{
	TUniquePtr<FMcChunk>* Chunk = Chunks.Find(Pos);
	return Chunk ? Chunk->Get() : nullptr;
}

const FMcChunk* FMcBlockWorld::FindChunk(FIntPoint Pos) const
{
	const TUniquePtr<FMcChunk>* Chunk = Chunks.Find(Pos);
	return Chunk ? Chunk->Get() : nullptr;
}

FMcStateId FMcBlockWorld::GetBlock(const FIntVector& Pos) const
{
	const FMcChunk* Chunk = FindChunk(FIntPoint(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z)));
	return Chunk ? Chunk->GetBlock(McCoords::BlockToLocal(Pos.X), Pos.Y, McCoords::BlockToLocal(Pos.Z)) : 0;
}

FMcStateId FMcBlockWorld::SetBlock(const FIntVector& Pos, FMcStateId State)
{
	check(IsInGameThread());
	const FIntPoint ChunkPos(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z));
	FMcChunk* Chunk = FindChunk(ChunkPos);
	if (!Chunk || !Chunk->IsInHeight(Pos.Y))
	{
		return 0;
	}
	const FMcStateId Previous = Chunk->SetBlock(McCoords::BlockToLocal(Pos.X), Pos.Y, McCoords::BlockToLocal(Pos.Z), State);
	if (Previous != State)
	{
		CollisionOverrides.Remove(Pos);
		OnBlockChanged.Broadcast(Pos, State);
		OnChunkChanged.Broadcast(ChunkPos);
	}
	return Previous;
}
