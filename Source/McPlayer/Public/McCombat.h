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
	// The power of a bow drawn for this many ticks, 0 to 1: (t * t + t * 2) / 3 for t seconds. Under 0.1 the arrow stays.
	MCPLAYER_API float GetBowPower(int32 DrawTicks);
	// The scale of the field of view (the angle) while a bow is drawn for this many seconds: 1 - 0.15 * t^2, t up to 1, so the
	// angle closes by 15 percent at the full draw (a second). Measured from the reference client (fov zoom, 5 to 25 ticks of draw).
	MCPLAYER_API float GetBowFovScale(float DrawSeconds);
	// The aim of a shot arrow before it is scaled by the speed: the look direction with a Gaussian of 0.0085 on each
	// component (measured: 50 full draws, the spread of the launch direction 0.0090 across, 0.0082 up). The arrow starts 0.1 block
	// under the eye, on the eye's line.
	MCPLAYER_API FVector GetBowAim(const FVector& Look, FRandomStream& Random);
	MCPLAYER_API double GetBowLaunchDrop();
	// The pitch of the shot's sound for a random U in 0 to 1 and the draw's power (volume 1): 0.62 + 0.21 U + power / 2. 70 shots of
	// the reference client: 40 full draws, pitch 1.126 to 1.327 (mean 1.212), and 30 draws held 8 ticks, 0.815 to 1.001 (mean
	// 0.902), each consistent with a uniform range (KS p 0.23 and 0.53). Only two powers were measured, so the linear power
	// term (0.5, from the 0.32 shift of the midrange between them) and the shape of the distribution are not fitted.
	MCPLAYER_API float GetBowShootPitch(float U, float Power);
	// How far the view rolls after a hit, in degrees, for the hurt time left in ticks (counting down from 10, with the fraction of
	// the tick): 41.1 * (t / 10)^4.08, with t at most 8 (the first hurt time the client ever shows is 8: 16.5 degrees). The shape (the
	// power 4.08) is fitted to shots of the reference client at hurt times 8 to 1 (rms 0.15 degrees of the measure); the size is scaled
	// so that our own shot at 7 reads the same roll by the same measure as the reference's (11.6 degrees from a hit at its side); the
	// roll is that times the side the hit came from, +1
	// for a hit from the player's left (+x when facing +z), -1 from the right, about 0 from the front or the back (measured at those
	// four directions only; the sine of the angle between is assumed).
	MCPLAYER_API float GetHurtTilt(float HurtTicks);
	// The side of a hit, -1 to 1: the sine of the angle to the attacker's direction (positive on the player's left), for a Minecraft
	// yaw in degrees and the attacker's offset in x and z. A hit without a place (a fall, fire) is +1, as the reference client's damage
	// without a source.
	MCPLAYER_API float GetHurtSide(float McYawDegrees, double OffsetX, double OffsetZ, bool bHasSource);
	// Durability the swing costs the held item: a sword 1... tools 2, anything else 0 (they are not made to hit with).
	MCPLAYER_API int32 GetWear(const FMcItemStack& Held);

	// The push a hit gives: velocity (blocks a tick) for a victim at Victim hit from Attacker, on top of what it had.
	MCPLAYER_API FVector GetKnockback(const FVector& Victim, const FVector& Attacker, const FVector& OldVelocity, float Strength = 0.4f);
}
