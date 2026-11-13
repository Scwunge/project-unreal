// The saved game rules and difficulty (minecraft.wiki, "Game rule"), read into plain fields so the systems that obey
// them don't parse text each tick. The level keeps rules as name to text ("true", "3"); a rule that isn't there has
// its default. /gamerule and /difficulty change the level's own copy, and the world pushes a fresh FMcGameRules to
// the host, the simulation and the players when they do.

#pragma once

#include "CoreMinimal.h"

struct MCCORE_API FMcGameRules
{
	// 0 peaceful, 1 easy, 2 normal, 3 hard.
	int32 Difficulty = 2;
	bool bDoMobSpawning = true;
	// Off, a shrieker at warning level 4 calls no warden.
	bool bDoWardenSpawning = true;
	bool bDoMobLoot = true;
	bool bDoTileDrops = true;
	bool bMobGriefing = true;
	bool bKeepInventory = false;
	bool bNaturalRegeneration = true;
	bool bFallDamage = true;
	bool bFireDamage = true;
	bool bDrowningDamage = true;
	bool bDoWeatherCycle = true;
	int32 RandomTickSpeed = 3;
	// Off, spawners stand still: their wait holds and nothing comes out (the mob inside still turns on the client).
	bool bSpawnerBlocksWork = true;
	// On, a recipe can be crafted only once it is in the player's recipe book.
	bool bLimitedCrafting = false;

	bool IsPeaceful() const { return Difficulty <= 0; }

	// The rules a level's text map holds, with the defaults for the rest.
	static FMcGameRules FromLevel(const TMap<FString, FString>& Rules, int32 InDifficulty);

	// One rule's value as a switch or a number, or the default when the level has no such rule or its text isn't one.
	static bool GetBool(const TMap<FString, FString>& Rules, const FString& Name, bool bDefault);
	static int32 GetInt(const TMap<FString, FString>& Rules, const FString& Name, int32 Default);
};
