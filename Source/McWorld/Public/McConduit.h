// Conduits (minecraft.wiki, "Conduit"): a conduit wakes when it sits in water, with water in all 26 blocks round it,
// inside a frame of prismarine blocks (prismarine, prismarine bricks, dark prismarine, sea lanterns). The frame is
// three square rings five across through the conduit, one in each plane, 42 places in all, and at least 16 must be
// filled.

#pragma once

#include "CoreMinimal.h"
#include "McBlockRegistry.h"

class FMcBlockWorld;

namespace McConduit
{
	// The fewest frame blocks that wake a conduit.
	constexpr int32 MinFrame = 16;

	MCWORLD_API bool IsConduit(FMcStateId State);
	// The 42 places a frame block can go, relative to the conduit.
	MCWORLD_API const TArray<FIntVector>& GetFramePlaces();
	// How many of them hold a frame block.
	MCWORLD_API int32 CountFrame(const FMcBlockWorld& World, const FIntVector& Pos);
	// Whether the conduit at Pos is awake: waterlogged, water all round, and enough frame.
	MCWORLD_API bool IsActive(const FMcBlockWorld& World, const FIntVector& Pos);
}
