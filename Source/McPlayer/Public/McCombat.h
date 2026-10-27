// What a swing does (minecraft.wiki: "Damage", "Attribute#Attack speed", "Knockback", "Critical hit"): the damage and
// speed come from the held item's attribute modifiers, and a swing before the attack has recharged is weaker.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

namespace McCombat
{
	struct FAttack
	{
		// Attack damage with the item in hand (the player's own 1 included).
		float Damage = 1.0f;
		// Swings a second at full charge (the player's own 4 included).
		float Speed = 4.0f;
	};

	// From the item's attack_damage and attack_speed modifiers; an empty stack, or an item with none, is the bare hand.
	MCPLAYER_API FAttack GetAttack(const FMcItemStack& Held);
	// How charged the attack is, 0 to 1, this many ticks after the last swing: (ticks + 0.5) / (20 / Speed).
	MCPLAYER_API float GetStrength(int32 TicksSinceAttack, float Speed);
	// Damage at that charge: the full damage scaled by 0.2 + 0.8 * Strength^2, and 1.5 times that for a critical hit
	// (falling, with the attack more than 90% charged).
	MCPLAYER_API float GetDamage(float BaseDamage, float Strength, bool bCritical);
	// Durability the swing costs the held item: a sword 1... tools 2, anything else 0 (they are not made to hit with).
	MCPLAYER_API int32 GetWear(const FMcItemStack& Held);

	// The push a hit gives: velocity (blocks a tick) for a victim at Victim hit from Attacker, on top of what it had.
	MCPLAYER_API FVector GetKnockback(const FVector& Victim, const FVector& Attacker, const FVector& OldVelocity, float Strength = 0.4f);
}
