// Loot tables (minecraft.wiki, "Loot table") and the block entities that structures give their chests.
//
// Only what decides which items and how many: pools with their rolls and conditions, weighted entries, nested
// tables, alternatives, and set_count. What the game does to an item beyond that (enchantments, potions, damage,
// maps, names) is data the stacks here don't have, so those functions are skipped. Conditions about the
// world (a biome, a tool) can't be asked at generation and pass.
//
// Rolls draw from the generator's own random stream, so a table gives the same items for the same seed.

#pragma once

#include "CoreMinimal.h"
#include "McBlockEntity.h"

class FMcNbt;
class FMcRandom;

namespace McLoot
{
	// Rolls a table (e.g. "minecraft:chests/simple_dungeon") into stacks, in roll order. False if there is no such table.
	MCGEN_API bool Roll(const FString& Table, FMcRandom& Random, TArray<FMcItemStack>& Out);

	// Rolls a table into the container's slots, the stacks in random free slots (more than fit are dropped), and
	// remembers the table and seed on it. False if there is no such table; the container is left alone then.
	MCGEN_API bool Fill(const FString& Table, uint64 Seed, FMcBlockEntity& Container);
}

namespace McBlockEntities
{
	// How many slots a block's container has (a chest, trapped chest or barrel 27, a dispenser or dropper 9, a
	// hopper 5); 0 for a block that holds nothing.
	MCGEN_API int32 ContainerSlots(FName Block);

	// A container for the block, filled from a loot table with a seed from Random.
	MCGEN_API FMcBlockEntity MakeLootContainer(FName Block, const FString& Table, FMcRandom& Random);

	// What a template block's NBT says the block holds: a loot table (rolled here), listed items, or a spawner's
	// mob. Empty for any other block.
	MCGEN_API TOptional<FMcBlockEntity> FromTemplate(const FMcNbt& Nbt, FName Block, FMcRandom& Random);
}
