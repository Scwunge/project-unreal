// The blocks a feature may touch while decorating one chunk: that chunk and its eight neighbours, like the
// game's world generation region. Features start inside the centre chunk and may spill up to a chunk
// beyond it; writes further out are dropped.
//
// Heightmaps (minecraft.wiki, "Heightmap") are kept per column as they are needed and follow every change.
// A height is one above the top block that counts:
// - WORLD_SURFACE(_WG): any block that isn't air.
// - OCEAN_FLOOR(_WG): blocks that block motion (the #blocks_motion_in_heightmap tag).
// - MOTION_BLOCKING: those, or anything holding a fluid.
// - MOTION_BLOCKING_NO_LEAVES: the same without leaves.

#pragma once

#include "CoreMinimal.h"
#include "McBiomeRegistry.h"
#include "McCore.h"

class FMcChunk;

enum class EMcHeightmap : uint8
{
	WorldSurfaceWG,
	WorldSurface,
	OceanFloorWG,
	OceanFloor,
	MotionBlocking,
	MotionBlockingNoLeaves,
	Count,
};

class MCGEN_API FMcGenRegion
{
public:
	// Chunks are the 3x3 around Centre, row by row from the north-west (z - 1, x - 1); null for missing ones.
	// Needs the block tags and shapes loaded.
	FMcGenRegion(FIntPoint InCentre, TArrayView<FMcChunk* const> InChunks);

	// Features may read a chunk (Index as in the constructor) but not change it: one already sent to players.
	void SetReadOnly(int32 Index) { ReadOnly[Index] = true; }

	static TOptional<EMcHeightmap> ParseHeightmap(const FString& Name);

	FIntPoint GetCentre() const { return Centre; }
	int32 GetMinY() const { return MinY; }
	int32 GetMaxY() const { return MaxY; }

	// Whether a column is in a chunk of the region.
	bool Contains(int32 X, int32 Z) const;
	// Air outside the region or the world's height.
	FMcStateId GetBlock(const FIntVector& Pos) const;
	// False (and nothing changes) outside the region, the world's height or a read-only chunk.
	bool SetBlock(const FIntVector& Pos, FMcStateId State);

	// One above the top block that counts; the world's bottom for an empty column or one outside the region.
	int32 GetHeight(EMcHeightmap Heightmap, int32 X, int32 Z);
	FMcBiomeId GetBiome(const FIntVector& Pos) const;

	bool IsAir(FMcStateId State) const;
	// Whether a state counts for a heightmap.
	bool Counts(EMcHeightmap Heightmap, FMcStateId State) const;

private:
	FMcChunk* ChunkAt(int32 X, int32 Z) const;
	int32 ColumnIndex(int32 X, int32 Z) const;

	FIntPoint Centre;
	FMcChunk* Chunks[9] = {};
	bool ReadOnly[9] = {};
	int32 MinY = 0;
	int32 MaxY = -1;
	// 48x48 columns per heightmap; MIN_int32 until worked out.
	TArray<int32> Heights[static_cast<int32>(EMcHeightmap::Count)];
	int32 MotionTag = INDEX_NONE;
	int32 MotionNoLeavesTag = INDEX_NONE;
};
