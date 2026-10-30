// What villagers and wandering traders sell (data/minecraft/trade_set, villager_trade and the tags of villager_trade): each
// profession has, for each level, a set that picks a number of trades from a tag; each trade wants one or two stacks
// and gives one, with the number of times it can be made before the trader runs out, the experience it earns the
// trader, and the discount a good reputation gives.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

class FMcRandom;

namespace McTrading
{
	struct MCGEN_API FOffer
	{
		FMcItemStack Wants;
		// The second thing wanted, if any.
		FMcItemStack Extra;
		FMcItemStack Gives;
		int32 MaxUses = 12;
		int32 Uses = 0;
		// What the trader earns from it.
		int32 Xp = 1;
		float Discount = 0.0f;

		bool IsOutOfStock() const { return Uses >= MaxUses; }
		// Reads or writes, by whether the archive is loading. False if a loading archive is malformed.
		bool Serialize(FArchive& Ar);
	};

	// The trades of one set (a profession's level, "farmer/level_1", or a wandering trader's, "wandering_trader/common"),
	// picked at random without repeats from its tag; the villager's biome type decides which of the trades that ask for it
	// can be picked. False if there is no such set.
	MCGEN_API bool RollSet(const FString& Set, FName VillagerVariant, FMcRandom& Random, TArray<FOffer>& Out);

	// The trades a profession ("minecraft:farmer") offers on reaching Level, none for a profession that has none.
	MCGEN_API bool RollLevel(FName Profession, int32 Level, FName VillagerVariant, FMcRandom& Random, TArray<FOffer>& Out);

	// What a player with Reputation (the trader's opinion of them, from -a lot to +a lot) pays for the stack the trade
	// wants: its count less the discount (at least 1 below the count, never below 1).
	MCGEN_API int32 GetCost(const FOffer& Offer, int32 Reputation);
}
