// Breaking blocks as a survival player does (minecraft.wiki: "Breaking", "Hardness", "Tools", "Loot table").
//
// How long a block takes depends on its hardness, the tool in hand and whether it is the right one, and the
// player standing on the ground or not and having their head under water. What it drops comes from the jar's
// own loot table for the block, asked with the tool as context (silk touch, shears, fortune), since the world
// generator's loot code (McLoot) can't tell tools apart.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "McItemStack.h"

namespace McBlockBreak
{
	// The block's hardness; negative for what survival players can't break (bedrock, water, portals...).
	MCPLAYER_API float GetHardness(FMcStateId State);
	MCPLAYER_API bool IsBreakable(FMcStateId State);

	// Whether the block drops nothing unless the tool is the right sort (stone needs a pickaxe, ores a better one).
	MCPLAYER_API bool RequiresTool(FMcStateId State);
	// Whether breaking it with this tool (an empty stack is the bare hand) gives its drops.
	MCPLAYER_API bool CanHarvest(const FMcItemStack& Tool, FMcStateId State);

	// How fast the tool digs the block: the tool's speed (1 for a hand or the wrong tool), plus level^2 + 1 for
	// efficiency on a tool that is faster than a hand, divided by 5 under water without aqua affinity and by 5
	// again off the ground.
	MCPLAYER_API float GetDigSpeed(const FMcItemStack& Tool, FMcStateId State, bool bOnGround, bool bHeadInWater);
	// How much of the block is dug each tick; 1 breaks it. Zero for unbreakable blocks.
	MCPLAYER_API float GetProgressPerTick(const FMcItemStack& Tool, FMcStateId State, bool bOnGround, bool bHeadInWater);
	// Ticks to dig it, at least 1; 0 for unbreakable blocks.
	MCPLAYER_API int32 GetBreakTicks(const FMcItemStack& Tool, FMcStateId State, bool bOnGround, bool bHeadInWater);

	// Durability the tool loses for one block (0 if it isn't a tool, or the block is dug in an instant).
	MCPLAYER_API int32 GetToolWear(const FMcItemStack& Tool, FMcStateId State);

	// What the block drops when this tool breaks it. With a wrong tool on a block that needs one: nothing.
	MCPLAYER_API void GetDrops(FMcStateId State, const FMcItemStack& Tool, FRandomStream& Random, TArray<FMcItemStack>& Out);

	// The experience breaking the block pays out (ores and spawners), rolled with Random; 0 for anything else.
	MCPLAYER_API int32 GetXpDropped(FMcStateId State, FRandomStream& Random);

	// The item that places the block, e.g. torch for wall_torch; None if there is none.
	MCPLAYER_API FName GetItemForBlock(FMcStateId State);
	// The state a block item places by default, or unset if the item places no block.
	MCPLAYER_API TOptional<FMcStateId> GetBlockForItem(FName Item);
	// Whether a state is one that placing this item makes (the item's block, whatever the properties).
	MCPLAYER_API bool IsBlockOfItem(FMcStateId State, FName Item);
}
