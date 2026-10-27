// Explosions (minecraft.wiki, "Explosion"): rays go out from the centre, each wearing down on the blocks in its way
// by their blast resistance, and every block a ray still has strength for when it gets there is destroyed. Entities
// take damage by how near they are and how much of them the blocks leave open. FMcEntityManager::Explode puts these
// together; the pieces are here so they can be tested on their own.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;

struct FMcDestroyedBlock
{
	FIntVector Pos;
	// What the block was, for working out what it drops.
	FMcStateId State = 0;
};

namespace McExplosion
{
	// The block's blast resistance. The jar has no such data, so it comes from a table of the common blocks and rules
	// for the families (planks, logs, leaves, ores...) from the wiki's block pages; blocks it doesn't know get 2 if
	// solid and 0 if not. Fluids are 100 and the unbreakable blocks (bedrock, barriers, command blocks) 3600000.
	MCENTITY_API float GetBlastResistance(FMcStateId State);

	// The blocks a blast of this power at Center would destroy (nothing is changed). 1352 rays leave the middle of a
	// 16x16x16 grid's surface, each starting at Power * (0.7 to 1.3), losing (resistance + 0.3) * 0.3 in each block
	// and 0.225 at every 0.3 of a block. Rays stop at chunks that aren't loaded.
	MCENTITY_API void Collect(const FMcBlockWorld& World, const FVector& Center, float Power, FRandomStream& Random, TArray<FIntVector>& Out);

	// What share of Box (0..1) the explosion at Center can see, by sampling a grid of points in the box and
	// checking whether a block with collision stands between each one and the centre.
	MCENTITY_API float GetExposure(const FMcBlockWorld& World, const FVector& Center, const FBox& Box);

	// The damage to something Distance blocks from a blast of this power (0 beyond twice the power) of which
	// Exposure is open to it: 43 for a creeper's blast point blank (power 3).
	MCENTITY_API float GetDamage(float Power, float Distance, float Exposure);
}
