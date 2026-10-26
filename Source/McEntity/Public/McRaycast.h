// Finding the block the crosshair is on: a ray walked through the grid, cell by cell, tested against each
// block's outline. Minecraft axes and block units, like McPhysics.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;

struct MCENTITY_API FMcBlockHit
{
	FIntVector Block = FIntVector::ZeroValue;
	// The face the ray went in through, in Minecraft's order: down, up, north, south, west, east.
	int32 Face = 0;
	// Where the ray met the outline, in world block coordinates.
	FVector Point = FVector::ZeroVector;
	FMcStateId State = 0;

	// The block beside the hit face, where a placed block goes.
	FIntVector GetAdjacent() const;
};

namespace McRaycast
{
	// Players reach 4.5 blocks in survival and 5 in creative.
	constexpr double SurvivalReach = 4.5;
	constexpr double CreativeReach = 5.0;

	// The boxes the crosshair can hit. Collision boxes where a block has them; blocks you walk through (plants,
	// torches, rails) are hit on their whole cell until their real outlines are known. Fluids and air: none.
	MCENTITY_API void GetOutline(FMcStateId State, TArray<FBox3f, TInlineAllocator<8>>& Out);

	// The first block outline along the ray from Origin in Direction (need not be normalised), within MaxDistance.
	MCENTITY_API TOptional<FMcBlockHit> Trace(const FMcBlockWorld& World, const FVector& Origin, const FVector& Direction, double MaxDistance);

	// Whether a block of this state may be replaced by placing another one into it: air, fluids, grass, snow
	// one layer deep and the like.
	MCENTITY_API bool IsReplaceable(FMcStateId State);

	// Minecraft's direction for a face index: down, up, north (-z), south (+z), west (-x), east (+x).
	MCENTITY_API FIntVector FaceDirection(int32 Face);
}
