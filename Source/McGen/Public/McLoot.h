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
#include "McCore.h"

class FMcNbt;
class FMcRandom;

namespace McLoot
{
	// Rolls a table (e.g. "minecraft:chests/simple_dungeon") into stacks, in roll order. False if there is no such table.
	MCGEN_API bool Roll(const FString& Table, FMcRandom& Random, TArray<FMcItemStack>& Out);

	// Applies an item modifier (a loot function, or a list of them: enchanting, potions, a filter) to a stack, as a villager's
	// trade does to what it gives.
	MCGEN_API void ApplyModifier(const TSharedPtr<FJsonValue>& Modifier, FMcRandom& Random, FMcItemStack& Stack);

	// Rolls a mob's table, for a kill with a weapon of that Looting level; bCaptain answers the raider predicate (is_captain) that decides the ominous bottle.
	MCGEN_API bool RollEntity(const FString& Table, bool bCaptain, FMcRandom& Random, TArray<FMcItemStack>& Out, int32 Looting = 0);

	// What an entity's loot table can ask about the mob and how it died (killed_by_player, entity_properties of the mob,
	// its killer and its vehicle, damage_source_properties). A roll made while a scope is alive uses it; with none, those
	// conditions can't be told and pass, as before.
	struct FMobContext
	{
		// A player killed it, or hurt it in the last five seconds.
		bool bKilledByPlayer = false;
		// Burning when it died (smelts what it drops, with furnace_smelt).
		bool bOnFire = false;
		// The killing weapon's enchantments smelt loot (Fire Aspect does; nothing here holds one yet).
		bool bWeaponSmelts = false;
		// Slime and magma cube: the size (1 smallest), 0 for any other mob.
		int32 CubeSize = 0;
		// A sheep's wool dye in the game's order (-1 for none) and whether it was sheared.
		int32 DyeColor = -1;
		bool bSheared = false;
		// The entity it rode, None for none.
		FName Vehicle;
		// What killed it ("minecraft:player", "minecraft:skeleton"), None for no entity, and what struck the blow
		// (the arrow or fireball that was shot), None if the killer struck it.
		FName Killer;
		FName DirectKiller;
		bool bProjectile = false;
		bool bLightning = false;
	};
	// Holds a mob's context for the rolls made on this thread while it lives.
	class MCGEN_API FMobScope
	{
	public:
		explicit FMobScope(const FMobContext& Context);
		~FMobScope();
	private:
		const FMobContext* Previous;
	};

	// What a block's loot table can ask about its being broken: the block state (match_block), and the power of the
	// blast that broke it, if one did (survives_explosion and explosion_decay each keep an item with a chance of 1
	// over the power). Nobody holds a tool: the silk touch and shears conditions fail, and fortune counts as level 0.
	struct FBlockContext
	{
		FMcStateId State = 0;
		// 0 when the block was not blown up.
		float ExplosionRadius = 0.0f;
	};

	// Rolls the loot table of the block with that state ("blocks/<name>") into stacks. False if the block has none
	// (it drops nothing then, as in the game).
	MCGEN_API bool RollBlock(const FBlockContext& Context, FMcRandom& Random, TArray<FMcItemStack>& Out);

	// What the fishing table can ask about a catch: whether the bobber lies in open water (treasure needs it), the
	// angler's luck (Luck of the Sea's level plus the Luck effect, which moves each entry's weight by its quality times
	// the luck), and the biome the bobber is in (bamboo is only caught in jungles; unset passes).
	struct FFishingContext
	{
		bool bOpenWater = true;
		int32 Luck = 0;
		FName Biome;
	};

	// Rolls "gameplay/fishing" for a catch.
	MCGEN_API bool RollFishing(const FFishingContext& Context, FMcRandom& Random, TArray<FMcItemStack>& Out);

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
