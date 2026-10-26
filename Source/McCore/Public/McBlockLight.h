// How much light each block state gives off and how much it dims light passing through, from the wiki's
// "Light" article rather than from any game code.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "McCore.h"

// Loaded once on the game thread, then read only, so light and meshing threads may read it freely. Lives in
// McCore (not McAssets) because the dedicated server's light engine needs it too.
class MCCORE_API FMcBlockLight
{
public:
	static FMcBlockLight& Get();

	// Builds the tables for every state in the block registry (loading it first). Does nothing if already built.
	bool EnsureLoaded();
	bool IsLoaded() const { return Emission.Num() > 0; }

	// Block light the state gives off, 0..15.
	uint8 GetEmission(FMcStateId Id) const { return Emission[Id]; }

	// How much the state dims light passing through it: 15 for opaque blocks, 1 for water, ice, leaves and the
	// other filtering blocks the wiki lists (this only matters for sky light travelling straight down: a step
	// of light costs at least 1 anyway), 0 for everything else.
	uint8 GetDampening(FMcStateId Id) const { return Dampening[Id]; }

	// True if sky light at full strength carries straight down through the state unchanged: dampening 0 and
	// no partial shape. Glass, plants, fences and torches do; leaves, water and slabs don't.
	bool PropagatesSkylightDown(FMcStateId Id) const { return SkylightDown[Id]; }

	// True for the blocks whose partial shape decides which faces light can cross (slabs, stairs, snow layers,
	// farmland, dirt paths, pistons, lecterns, stonecutters, daylight detectors, enchanting tables, end portal
	// frames, shelves). Their dampening is the plain value above; a light engine that wants the vanilla look
	// has to look at the collision shape on top of it.
	bool UsesShapeForLight(FMcStateId Id) const { return ShapeBased[Id]; }

private:
	TArray<uint8> Emission;
	TArray<uint8> Dampening;
	TBitArray<> SkylightDown;
	TBitArray<> ShapeBased;
};
