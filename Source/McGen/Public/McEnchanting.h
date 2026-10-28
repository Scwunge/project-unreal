// Enchanting, as the enchanting table and the loot functions do it (minecraft.wiki, "Enchanting mechanics"):
//
// - The table offers three costs, worked out from the number of bookshelves around it and the player's enchantment
//   seed, so they stay the same until the player enchants something.
// - The enchantments a cost buys are picked by weight from those whose cost range takes the "power" (the cost
//   pushed up by the item's enchantability and a random spread). Further ones follow while a roll stays under the
//   power, which halves each time, and each must be compatible with those already picked.
// - Books take any of the enchantments and become enchanted books that store them.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

class FMcRandom;

namespace McEnchanting
{
	struct FEnchant
	{
		FName Name;
		int32 Level = 0;
		bool operator==(const FEnchant& Other) const { return Name == Other.Name && Level == Other.Level; }
	};

	MCGEN_API bool IsBook(const FMcItemStack& Stack);
	// What the stack has on it (or, for an enchanted book, stores).
	MCGEN_API TMap<FName, int32> GetEnchantments(const FMcItemStack& Stack);
	// Adds one (a book becomes an enchanted book storing it).
	MCGEN_API void AddEnchantment(FMcItemStack& Stack, FName Enchantment, int32 Level);

	// What enchanting Item with Levels gives, from Options. With bTable only enchantments the table offers for the
	// item count (its primary items, else all it supports); otherwise all it supports. Books take any. Empty if the
	// item can't be enchanted or nothing fits.
	MCGEN_API TArray<FEnchant> Select(FName Item, int32 Levels, const TArray<FName>& Options, FMcRandom& Random, bool bTable);

	// The table's enchantments: those in the "minecraft:in_enchanting_table" tag, in a fixed order.
	MCGEN_API TArray<FName> GetTableOptions();
	// Bookshelves that count around a table, at most 15. A bookshelf counts two blocks out from the table (in the
	// 5 x 5 ring around it, on its level or one up) when the block between it and the table is clear.
	MCGEN_API int32 CountBookshelves(TFunctionRef<bool(const FIntVector&)> IsBookshelf, TFunctionRef<bool(const FIntVector&)> IsClear);

	// What the table shows for an item: the three costs in levels (0 for none) and, for each, one of the enchantments
	// it would give as a clue.
	struct FOffers
	{
		int32 Costs[3] = { 0, 0, 0 };
		FEnchant Clues[3];
	};
	MCGEN_API FOffers GetOffers(FName Item, int32 Bookshelves, int32 Seed);
	// What the table's option Slot (0 to 2) at Cost puts on Item for that seed: the same every time until the seed changes.
	MCGEN_API TArray<FEnchant> GetTableEnchantments(FName Item, int32 Seed, int32 Slot, int32 Cost);
}
