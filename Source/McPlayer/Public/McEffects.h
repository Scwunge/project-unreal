// Status effects on a player (minecraft.wiki, "Effect"). An effect has an amplifier (0 for level I) and the ticks
// it has left; a new one replaces the same effect only if it is stronger, or as strong and lasting longer.
//
// What they do here: regeneration, poison, wither, hunger, saturation, instant health and damage, absorption and
// health boost work on the player's stats each tick; speed and slowness, jump boost, haste and mining fatigue,
// strength and weakness, resistance and fire resistance are read where the player moves, digs, hits or is hurt.
// The rest (night vision, invisibility and so on) are kept and shown but change nothing yet.

#pragma once

#include "CoreMinimal.h"

struct FMcPlayerStats;
struct FMcStatusEffect;

namespace McEffects
{
	// Every effect there is, by name without the namespace ("speed").
	MCPLAYER_API const TArray<FName>& GetAll();
	// Whether the name (with or without "minecraft:") is one; gives the name without it.
	MCPLAYER_API bool Parse(const FString& Text, FName& OutId);
	// Instant effects act once when given and are never kept.
	MCPLAYER_API bool IsInstant(FName Id);
	// Whether it is one the game counts as harmful (the HUD tints these).
	MCPLAYER_API bool IsHarmful(FName Id);

	// Gives an effect; Duration is in ticks (an instant one ignores it). False if the player has a stronger one
	// already or is dead.
	MCPLAYER_API bool Add(FMcPlayerStats& Stats, FName Id, int32 Amplifier, int32 Duration);
	// Takes one off (all of them if Id is none); how many were removed.
	MCPLAYER_API int32 Remove(FMcPlayerStats& Stats, FName Id);
	MCPLAYER_API const FMcStatusEffect* Find(const FMcPlayerStats& Stats, FName Id);

	// One tick of what the effects do to the stats.
	MCPLAYER_API void Tick(FMcPlayerStats& Stats);

	// ---- What the rest of the game reads ----
	// The walking speed factor: speed +20% and slowness -15% a level.
	MCPLAYER_API float GetSpeedFactor(const FMcPlayerStats& Stats);
	// Added to the jump's upward speed: 0.1 a level of jump boost.
	MCPLAYER_API float GetJumpBonus(const FMcPlayerStats& Stats);
	// How much faster blocks break: haste +20% a level, mining fatigue 0.3 to the power of its level (4 at most).
	MCPLAYER_API float GetDigFactor(const FMcPlayerStats& Stats);
	// Added to a melee hit: strength +3 a level, weakness -4.
	MCPLAYER_API float GetAttackBonus(const FMcPlayerStats& Stats);
	// The share of damage resistance takes off, 20% a level; 1 from level V.
	MCPLAYER_API float GetResistance(const FMcPlayerStats& Stats);
	MCPLAYER_API bool IsFireImmune(const FMcPlayerStats& Stats);
	// The Roman numeral for a level, as the HUD shows after the name (none for I).
	MCPLAYER_API FString GetLevelText(int32 Amplifier);
}
