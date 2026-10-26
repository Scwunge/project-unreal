// The loaded chunks of one dimension.

#pragma once

#include "CoreMinimal.h"
#include "McChunk.h"
#include "McDimensionType.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FMcOnChunkEvent, FIntPoint /*ChunkPos*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FMcOnBlockChanged, const FIntVector& /*Pos*/, FMcStateId /*State*/);

// Game thread only. Work on other threads (meshing) takes copies of the blocks it needs, so chunks can
// change or unload while that work runs.
class MCWORLD_API FMcBlockWorld
{
public:
	explicit FMcBlockWorld(const FMcDimensionType& InDimension);
	// Owns its chunks; also stops MSVC generating a copy for the DLL export, which TUniquePtr can't do.
	UE_NONCOPYABLE(FMcBlockWorld);

	const FMcDimensionType& GetDimension() const { return Dimension; }

	// A new empty (all air) chunk at this position, sized for the dimension. Not yet part of the world.
	TUniquePtr<FMcChunk> MakeChunk(FIntPoint Pos) const;

	// Takes ownership and broadcasts OnChunkAdded. Replaces any chunk already at that position.
	FMcChunk& AddChunk(TUniquePtr<FMcChunk> Chunk);
	void RemoveChunk(FIntPoint Pos);

	FMcChunk* FindChunk(FIntPoint Pos);
	const FMcChunk* FindChunk(FIntPoint Pos) const;
	bool HasChunk(FIntPoint Pos) const { return Chunks.Contains(Pos); }
	int32 NumChunks() const { return Chunks.Num(); }
	const TMap<FIntPoint, TUniquePtr<FMcChunk>>& GetChunks() const { return Chunks; }

	// World block coordinates (x east, y up, z south). Unloaded chunks read as air and ignore writes.
	FMcStateId GetBlock(const FIntVector& Pos) const;
	FMcStateId SetBlock(const FIntVector& Pos, FMcStateId State);

	FMcOnChunkEvent OnChunkAdded;
	FMcOnChunkEvent OnChunkRemoved;
	// Broadcast for the chunk a block changed in.
	FMcOnChunkEvent OnChunkChanged;
	// Broadcast by SetBlock for each block that changed, before OnChunkChanged.
	FMcOnBlockChanged OnBlockChanged;

private:
	FMcDimensionType Dimension;
	TMap<FIntPoint, TUniquePtr<FMcChunk>> Chunks;
};
