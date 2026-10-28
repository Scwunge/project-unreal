// Buckets (minecraft.wiki, "Bucket", "Water Bucket", "Lava Bucket"): an empty one takes a water or lava source, a
// full one pours its source into a block that can be replaced. Water poured in the Nether boils away. Players use
// them through the pawn's item-on-block request; dispensers use them too. Server only.

#pragma once

#include "CoreMinimal.h"

class AMcPlayerPawn;
class AMcWorldActor;

namespace McBuckets
{
	// Takes the water or lava source at Pos: the filled bucket's item, or None if there is no source there.
	FName Fill(AMcWorldActor& World, const FIntVector& Pos);
	// Pours a water or lava bucket into Pos if the block there can be replaced; true if it was emptied.
	bool Pour(AMcWorldActor& World, FName Bucket, const FIntVector& Pos);

	// A player's held bucket used on the block at Pos (Beside is the cell next to the face clicked): fills from a
	// source at Pos, or pours into Pos if replaceable, else into Beside. The held stack changes, except in creative.
	// False if the held item isn't a bucket or nothing happened.
	bool Use(AMcWorldActor& World, AMcPlayerPawn& User, const FIntVector& Pos, const FIntVector& Beside);
}
