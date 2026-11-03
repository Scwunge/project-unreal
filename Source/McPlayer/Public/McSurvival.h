// The rules of staying alive (minecraft.wiki: "Damage", "Armor", "Hunger", "Health", "Drowning", "Fall damage",
// "Experience"). Pure functions on FMcPlayerStats and the player's armour, ticked by the server 20 times a second;
// nothing here knows about the network or the world beyond the few facts FEnvironment carries.

#pragma once

#include "CoreMinimal.h"
#include "McPlayerData.h"

struct FMcGameRules;
class FMcBlockWorld;
class FMcEntityManager;
class FMcXpOrb;
struct FMcFoodData;

namespace McSurvival
{
	enum class EDifficulty : uint8 { Peaceful, Easy, Normal, Hard };

	// Where damage comes from, as far as the rules tell them apart. Armour does not reduce Generic, Fall, Drown,
	// Starve, OnFire or Void (the jar's damage_type/bypasses_armor tag), and Void and Generic get through the
	// hurt immunity.
	enum class EDamage : uint8
	{
		Generic,
		Fall,
		Drown,
		// Standing in a fire block.
		InFire,
		// The burning that goes on after.
		OnFire,
		Lava,
		Starve,
		// A mob's bite or blow.
		Mob,
		Void,
		// Poison, instant damage and the like, and wither; neither is turned aside by armour.
		Magic,
		Wither,
		// A blast; armour turns some of it aside.
		Explosion,
	};

	// What the blocks around a player are doing to them this tick.
	struct FEnvironment
	{
		bool bHeadInWater = false;
		bool bFeetInWater = false;
		bool bInLava = false;
		bool bInFire = false;
		// Ladders and vines, which break a fall.
		bool bClimbing = false;
		bool bInCobweb = false;
	};

	// The facts for a position, read from the world: Feet is the block position of the player's feet and EyeHeight how
	// high their eyes are above them.
	MCPLAYER_API FEnvironment SampleEnvironment(const FMcBlockWorld& World, const FVector& Feet, float EyeHeight = 1.62f);

	// ---- Armour ----
	// The points and toughness of what is worn (head, chest, legs, feet).
	MCPLAYER_API void GetArmorValues(const FMcContainer& Worn, float& OutArmor, float& OutToughness);
	// The damage left after armour of this strength: Damage * (1 - min(20, max(A / 5, A - D / (2 + T / 4))) / 25).
	MCPLAYER_API float ReduceByArmor(float Damage, float ArmorPoints, float Toughness);
	MCPLAYER_API bool BypassesArmor(EDamage Kind);
	// The hunger a hit of this kind costs the player to take.
	MCPLAYER_API float GetHurtExhaustion(EDamage Kind);

	// ---- Damage ----
	// Hurts the player: the immunity after a hit, armour (which wears: each piece loses max(1, Damage / 4) durability),
	// then absorption, then health. Worn may be null. Returns the health lost.
	// Rules may be null; with them, a kind of damage the game rules turn off (fallDamage, fireDamage, drowningDamage) does nothing.
	MCPLAYER_API float Hurt(FMcPlayerStats& Stats, float Amount, EDamage Kind, FMcContainer* Worn = nullptr, const FMcGameRules* Rules = nullptr);
	// Whether the game rules let this kind of damage through: fall, drowning, and fire (fire, burning and lava).
	MCPLAYER_API bool IsDamageEnabled(EDamage Kind, const FMcGameRules& Rules);
	MCPLAYER_API void Heal(FMcPlayerStats& Stats, float Amount);
	inline bool IsDead(const FMcPlayerStats& Stats) { return Stats.Health <= 0.0f; }

	// The damage of a fall of this many blocks: the distance past 3, rounded up.
	MCPLAYER_API int32 GetFallDamage(float Distance);

	// How far the player has fallen, from the positions they report.
	struct MCPLAYER_API FFallTracker
	{
		float Distance = 0.0f;
		// Called each tick with the feet's old and new height. On the ground (or in water, on a ladder or in a web) the
		// fall ends: the damage it did is returned and the distance cleared. Moving up takes distance off.
		int32 Update(float PreviousY, float NewY, bool bOnGround, bool bBreaksFall);
	};

	// ---- The tick ----
	// One game tick of everything the world does to a player: the hurt flash and immunity counting down, breath,
	// burning, lava, fire and hunger. Returns true if they died.
	MCPLAYER_API bool Tick(FMcPlayerStats& Stats, const FEnvironment& Environment, EDifficulty Difficulty, FMcContainer* Worn = nullptr, const FMcGameRules* Rules = nullptr);

	// ---- Hunger ----
	// What the player's work costs: it builds up in Exhaustion, and every 4 takes saturation, then food.
	MCPLAYER_API void AddExhaustion(FMcPlayerStats& Stats, float Amount);
	// Spending exhaustion, healing when well fed (food 18 or more: 1 health every 80 ticks; food 20 with saturation:
	// every 10 ticks, up to 1 health, and it costs hunger) and starving (every 80 ticks, 1 damage, down to 10 health
	// on Easy and 1 on Normal; Hard kills). One game tick.
	// With the naturalRegeneration rule off (in Rules) nothing heals by itself, but hunger still runs.
	MCPLAYER_API void TickNeeds(FMcPlayerStats& Stats, EDifficulty Difficulty, FMcContainer* Worn = nullptr, const FMcGameRules* Rules = nullptr);
	// Food can be eaten when not full, or if the food says it can always be eaten.
	MCPLAYER_API bool CanEat(const FMcPlayerStats& Stats, const FMcFoodData& Food);
	// Eating a food: its nutrition to food and its saturation to saturation, which never goes past food.
	MCPLAYER_API void Eat(FMcPlayerStats& Stats, const FMcFoodData& Food);
	// The exhaustion of the things a player does, per block walked or each time.
	constexpr float SprintExhaustionPerBlock = 0.1f;
	constexpr float SwimExhaustionPerBlock = 0.015f;
	constexpr float JumpExhaustion = 0.05f;
	constexpr float SprintJumpExhaustion = 0.2f;
	constexpr float BreakBlockExhaustion = 0.005f;
	constexpr float AttackExhaustion = 0.1f;

	// ---- Experience ----
	// Points to go from this level to the next: 2L + 7 up to level 15, 5L - 38 to 30, then 9L - 158.
	MCPLAYER_API int32 GetXpToNextLevel(int32 Level);
	// Points in all that a level starts at.
	MCPLAYER_API int32 GetTotalXpForLevel(int32 Level);
	MCPLAYER_API int32 GetTotalXp(const FMcPlayerStats& Stats);
	// Adds points (or, negative, takes them), keeping the level and the fraction of the next.
	MCPLAYER_API void AddXp(FMcPlayerStats& Stats, int32 Points);
	// The points dropped on death: 7 per level, at most 100.
	MCPLAYER_API int32 GetDeathXp(const FMcPlayerStats& Stats);

	// ---- Items ----
	// Moves what the stack holds into the container, merging first. Returns what did not fit.
	MCPLAYER_API FMcItemStack GiveItem(FMcContainer& Inventory, const FMcItemStack& Stack);

	// The items lying within reach of a player whose hitbox is PlayerBox go into the inventory (those still in their pickup
	// delay stay); what does not fit stays on the ground. Returns how many items were taken.
	MCPLAYER_API int32 CollectItems(FMcEntityManager& Entities, const FBox& PlayerBox, FMcContainer& Inventory);
	// Arrows a player shot that lie stuck in a block within reach go back into the inventory, one each. Returns how many.
	MCPLAYER_API int32 CollectArrows(FMcEntityManager& Entities, const FBox& PlayerBox, FMcContainer& Inventory);
	// The experience orbs within reach of the player are taken into their experience. Returns the points.
	MCPLAYER_API int32 CollectXp(FMcEntityManager& Entities, const FBox& PlayerBox, FMcPlayerStats& Stats);
}
