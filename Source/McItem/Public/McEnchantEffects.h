// What an enchantment does, read from its own data file (data/minecraft/enchantment/<name>.json "effects"), not coded one by
// one: each effect component ("minecraft:damage", "minecraft:damage_protection", "minecraft:knockback", "minecraft:item_damage"...)
// holds a list of effects, each with an optional "requirements" condition, and its value rises with the enchantment's level
// ("linear": base + per_level_above_first * (level - 1), "fraction", "constant", ...).
//
// Components are summed over the stacks that carry the enchantments (a sword; the four armour pieces). A requirement the
// context can't answer fails, so an effect never fires on a guess.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

namespace McEnchantEffects
{
	// What a requirement can ask about the event.
	struct FContext
	{
		// The damage source's tags without "#" ("minecraft:is_fall", "minecraft:is_fire"...), for damage_source_properties.
		TSet<FName> DamageTags;
		// A blow struck by hand (a sword), not a thrown or shot thing.
		bool bDirect = true;
		// The mob the effect's "this" is the victim of, e.g. "minecraft:zombie"; none otherwise.
		FName VictimType;
		// The thing that did the damage itself when it was not the attacker's own hand ("minecraft:arrow" for a shot), for
		// "direct_attacker"; none for a blow struck by hand, where the direct attacker is the attacker.
		FName DirectAttackerType;
		// The attacker is a player (looting asks).
		bool bAttackerIsPlayer = false;
	};

	// The tags (without "#", e.g. "minecraft:is_fire") of a damage type ("minecraft:on_fire"), read from data/minecraft/tags/damage_type.
	MCITEM_API TSet<FName> GetDamageTags(FName DamageType);

	// Whether the enchantment has any effect under that component.
	MCITEM_API bool HasEffect(FName Enchantment, const FString& Component);

	// The value of a level-based number: a number, or {type linear|constant|fraction|exponent|clamped|lookup|levels_squared}.
	MCITEM_API float EvalLevelBased(const TSharedPtr<FJsonValue>& Value, int32 Level);

	// The sum of the "add" effects of that component over the stack's enchantments that pass their requirements.
	MCITEM_API float SumAdd(const FMcItemStack& Stack, const FString& Component, const FContext& Context);

	// The product of the "multiply" effects' factors (1 for none), e.g. Mending's 2 under "minecraft:repair_with_xp".
	MCITEM_API float GetFactor(const FMcItemStack& Stack, const FString& Component, const FContext& Context);

	// How much durability the stack really loses for Amount points of wear: each point is saved with Unbreaking's
	// "remove_binomial" chance (half a tool's at level 1, a fifth of armour's).
	MCITEM_API int32 GetItemDamage(const FMcItemStack& Stack, int32 Amount, FRandomStream& Random);

	// Whether a requirement (all_of, any_of, inverted, match_tool, damage_source_properties, entity_properties, random_chance)
	// is met, for an effect at the given level.
	MCITEM_API bool Check(const TSharedPtr<FJsonValue>& Requirement, const FContext& Context, const FMcItemStack& Stack, int32 Level, FRandomStream& Random);

	// Whether an entity type is named by a reference ("minecraft:zombie", a "#tag" of entity types, or a list of them).
	MCITEM_API bool EntityMatches(FName Type, const TSharedPtr<FJsonValue>& Reference);
}
