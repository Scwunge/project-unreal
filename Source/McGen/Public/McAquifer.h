// Aquifers: what fills open space underground, from the noise settings' "aquifers" functions
// (minecraft.wiki, "Aquifer").
//
// The world is split into 16x12x16 cells (offset 5 blocks on x and z, 1 on y), each with a random centre
// and a fluid status: the global sea (water to sea level, lava under y -54), a local level of water or
// lava, or dry. An open block takes the status of its nearest centre; where two centres with different
// statuses are nearly equally near, a barrier pressure is added to the terrain density, and the block
// turns to stone if that makes it solid.
//
// Where the wiki leaves the details open, the pressure shapes, the noise sampling of the barrier band and
// the cell lookup are our own choices.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FJsonValue;
class FMcDensityContext;
class FMcDensityGraph;

class MCGEN_API FMcAquifer
{
public:
	struct FStatus
	{
		bool bGlobal = false;
		int32 Level = MIN_int32;  // fluid below this y; MIN_int32 when dry
		FMcStateId Fluid = 0;
	};

	// Scratch state for one chunk: cell centres and statuses worked out so far.
	struct FCache
	{
		TMap<FIntVector, FIntVector> Centres;
		TMap<FIntVector, FStatus> Statuses;
		TMap<FIntPoint, int32> Surfaces;
		TMap<FIntPoint, int32> ExactSurfaces;
	};

	// Reads the aquifers object's functions into Graph. False when there is none (aquifers off).
	bool Load(const TSharedPtr<FJsonValue>& Json, int32 SurfaceLevelNode, FMcDensityGraph& InGraph, uint64 Seed, int32 InSeaLevel,
		FMcStateId InWater, FMcStateId InLava, FMcStateId InAir, FMcStateId InStone);

	// The block for an open position (Density <= 0): fluid, air, or stone for a barrier. Context must not be in a
	// chunk; the aquifer evaluates far from the block.
	FMcStateId Compute(int32 X, int32 Y, int32 Z, double Density, FMcDensityContext& Context, FCache& Cache) const;

	// The global fluid rule alone: lava under y -54, water up to sea level.
	FMcStateId GlobalAt(int32 Y) const;

	// For tests.
	FStatus GetStatus(const FIntVector& Cell, FMcDensityContext& Context, FCache& Cache) const;
	FIntVector GetCentre(const FIntVector& Cell, FCache& Cache) const;
	static FIntVector CellOf(int32 X, int32 Y, int32 Z);

private:
	int32 SurfaceAt(int32 ChunkX, int32 ChunkZ, FMcDensityContext& Context, FCache& Cache) const;
	FMcStateId FluidAt(const FStatus& Status, int32 Y) const;
	double Pressure(int32 Y, const FStatus& A, const FStatus& B, double BarrierNoise) const;

	const FMcDensityGraph* Graph = nullptr;
	uint64 CentreSeed = 0;
	int32 SeaLevel = 63;
	int32 Barrier = INDEX_NONE;
	int32 Floodedness = INDEX_NONE;
	int32 Spread = INDEX_NONE;
	int32 LavaNoise = INDEX_NONE;
	int32 Exclusion = INDEX_NONE;
	int32 SurfaceLevel = INDEX_NONE;
	FMcStateId Water = 0;
	FMcStateId Lava = 0;
	FMcStateId Air = 0;
	FMcStateId Stone = 0;
};
