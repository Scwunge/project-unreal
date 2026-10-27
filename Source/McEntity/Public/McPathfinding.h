// A* over the block grid, for mobs walking to a player or a spot. A node is the block a mob's feet are in; it can
// be stood in when the block under it is solid and safe and the blocks the body fills are free. From a node a mob
// walks to the four sides and (round a corner that is clear) the diagonals, steps up one block, or drops up to three
// to ground it can stand on. Water, lava, fire, cactus and the like are never walked through or onto.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;

struct FMcPathOptions
{
	// Blocks of headroom the body needs: its height rounded up.
	int32 Clearance = 2;
	// How far it climbs in one step (a mob jumps one block) and how far it will fall to get somewhere.
	int32 MaxStepUp = 1;
	int32 MaxDrop = 3;
	// Nodes looked at before giving up; the path then leads to the closest one reached.
	int32 MaxNodes = 300;
	// The goal counts as reached within this many blocks sideways (and one up or down).
	int32 GoalRadius = 0;
};

namespace McPath
{
	// Blocks a mob never walks through or onto besides the ones with fluids in them: fire, cactus, magma and so on.
	MCENTITY_API bool IsHazard(FMcStateId State);

	// Whether the body fits with its feet in this block: the Clearance blocks from here up are free of collision,
	// fluid and hazards, and the chunk is loaded.
	MCENTITY_API bool IsOpen(const FMcBlockWorld& World, const FIntVector& Feet, int32 Clearance);

	// Open, and with a safe solid block under it that is no taller than a block (a fence post is no floor).
	MCENTITY_API bool IsStandable(const FMcBlockWorld& World, const FIntVector& Feet, int32 Clearance);

	// Finds a way from Start to Goal (blocks the feet are in). Path gets the blocks to walk through, not counting
	// Start. True if it reaches the goal. If not, Path leads to the node that came closest (empty if none did).
	MCENTITY_API bool Find(const FMcBlockWorld& World, const FIntVector& Start, const FIntVector& Goal, const FMcPathOptions& Options, TArray<FIntVector>& Path);
}
