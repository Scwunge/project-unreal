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
	// Exposure is open to it: 43 for a creeper's blast point blank (power 3). Not rounded: a player 7 blocks from a
	// power 4 blast at normal difficulty lost 4.94 health, 6.265 blocks 8.39, 5.725 blocks 11.23, 5.324 blocks 13.5 and 5.167 blocks
	// 14.43 in the reference client (the five fit 1 + 56 * (I*I + I) / 2, I = 1 - Distance / 8, to 0.001).
	MCENTITY_API float GetDamage(float Power, float Distance, float Exposure);

	// The pitch of the blast's sound from two rolls in [0, 1): 537 blasts of the reference client (scripts tnt3.txt,
	// tnt4.txt, tnt5.txt; `log sounds`), all at volume 4, ran 0.5776 to 0.8298 with mean 0.6992 and standard deviation
	// 0.0538, peaking in the middle and thinning to both ends: a triangle (the difference of two flat rolls), centre
	// 0.699, half-width 0.131 from the standard deviation (0.0538 * sqrt(6)); the edges that gives, 0.568 and 0.830,
	// lie where the 537 samples put them (the sample minimum 0.5776 plus the expected edge gap, 0.008).
	MCENTITY_API float GetBlastPitch(float RollA, float RollB);

	// The push on something at Position whose eye (or the point the blast pushes from) is at Eye: along the line from
	// the centre to the eye, as strong as the impact (1 - Distance / (2 * Power)) * Exposure, in blocks a tick before
	// friction. The same five reference blasts read x speeds of 0.0666, 0.1149, 0.1498, 0.1752 and 0.1851 a tick later,
	// which is this times the ground's friction (0.546).
	MCENTITY_API FVector GetKnockback(float Power, const FVector& Center, const FVector& Position, const FVector& Eye, float Exposure);
}
