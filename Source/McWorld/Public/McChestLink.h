// Double chests (minecraft.wiki, "Chest"): two chests side by side with the same facing are one container.
//
// A chest's `type` is single, left or right, as seen by someone facing its front. A right chest's other half is
// the block clockwise of its facing, a left chest's the one counter-clockwise. The right half's slots come first.

#pragma once

#include "CoreMinimal.h"
#include "McBlockRegistry.h"

class FMcBlockWorld;

namespace McChestLink
{
	// A chest that can pair: the chest and the trapped chest.
	MCWORLD_API bool IsLinkable(FName BlockName);

	// From a chest of this facing and type to its other half; zero for a single chest or a facing that isn't a side.
	MCWORLD_API FIntVector GetPartnerOffset(FName Facing, FName Type);

	// The other half of the double chest at Pos, if there is one and it matches (same block and facing, the
	// complementary type).
	MCWORLD_API TOptional<FIntVector> FindPartner(const FMcBlockWorld& World, const FIntVector& Pos);

	// What a chest of the given block and facing placed at Pos becomes beside a single chest it can pair with:
	// its type, and the position of the chest it joins. Prefers the neighbour at Preferred (the block clicked).
	MCWORLD_API TOptional<TPair<FName, FIntVector>> ChooseJoin(const FMcBlockWorld& World, const FIntVector& Pos, FName BlockName, FName Facing, const FIntVector& Preferred);

	// Keeps the halves in step after the block at Pos changed from Old to State: a chest that became half of a
	// pair makes its single neighbour the other half, and one that stopped being half of a pair lets its partner
	// go back to single. Changes go through SetBlock.
	MCWORLD_API void Sync(const FMcBlockWorld& World, const FIntVector& Pos, FMcStateId Old, FMcStateId State, const TFunction<void(const FIntVector&, FMcStateId)>& SetBlock);
}
