// Block entities (minecraft.wiki, "Block entity"): the data a few blocks carry beyond their state. Here just
// the data, nothing that ticks: what a chest, barrel or dispenser holds, and which mob a spawner spawns.
//
// A chunk keeps them by block position and saves them next to its blocks (M9). Structure chests also keep the
// loot table they were filled from, and the seed it was rolled with.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

class FArchive;

enum class EMcBlockEntityKind : uint8
{
	// Chests, barrels, dispensers and the like: Slots, in the game's slot order.
	Container,
	// A mob spawner: EntityId.
	Spawner,
	// A furnace, blast furnace or smoker: Data is FMcFurnace::Serialize's bytes (the module that knows them is McItem).
	Furnace,
};

struct MCWORLD_API FMcBlockEntity
{
	EMcBlockEntityKind Kind = EMcBlockEntityKind::Container;
	TArray<FMcItemStack> Slots;
	// The loot table a container was filled from, e.g. "minecraft:chests/simple_dungeon", and the seed it was
	// rolled with. None for a container that was filled by hand.
	FName LootTable;
	int64 LootSeed = 0;
	// The entity a spawner spawns, e.g. "minecraft:zombie".
	FName EntityId;
	// A furnace's saved state.
	TArray<uint8> Data;

	static FMcBlockEntity MakeContainer(int32 NumSlots);
	static FMcBlockEntity MakeSpawner(FName InEntityId);

	// Reads or writes, by whether the archive is loading. Names are saved as text, so a save doesn't depend on
	// the name table. False if a loading archive is malformed.
	bool Serialize(FArchive& Ar);

	bool operator==(const FMcBlockEntity& Other) const;
};
