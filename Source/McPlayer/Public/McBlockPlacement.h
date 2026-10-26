// Which state a block takes when a player places it, from the face clicked and the way they face: logs line up
// with the face, stairs and slabs go top or bottom by where the face was hit, stairs and doors face away from
// the player, furnaces and chests face them, torches stick to walls. Rules from minecraft.wiki's block pages.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "McRaycast.h"

struct FMcPlacedBlock
{
	FIntVector Pos;
	FMcStateId State;
};

namespace McBlockPlacement
{
	// What placing Block (any of its states) against Hit puts where. Empty when it can't go there: the cell
	// is taken, a torch would hang from a ceiling, or a door's upper half has no room. Doesn't check whether
	// players are in the way. Yaw and pitch are Minecraft's.
	MCPLAYER_API TArray<FMcPlacedBlock> GetPlacement(const FMcBlockWorld& World, FMcStateId Block, const FMcBlockHit& Hit, float Yaw, float Pitch);
}
