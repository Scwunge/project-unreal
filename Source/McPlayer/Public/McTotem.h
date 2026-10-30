// Totem of Undying (minecraft.wiki: "Totem of Undying"): held in either hand when a blow would kill, it is used up
// instead and the holder lives on with 1 health, every effect taken away, then Regeneration II for 45 seconds, Fire
// Resistance for 40 and Absorption II for 5. Only the void (and /kill, which deals the void's damage) gets past it.
// The holder's screen shows the totem flying up at them while green and yellow sparks burst out around the body.

#pragma once

#include "CoreMinimal.h"

struct FMcPlayerStats;
struct FMcItemStack;

namespace McTotem
{
	// How long the totem flies across the holder's screen, and how long the body throws off sparks (ticks).
	// Measured from vanilla shots every 2 ticks (mc-reference scripts/totem_ticks.txt): the totem is on screen from
	// the tick after the blow to about 20 ticks on, and new sparks keep coming for about 15.
	constexpr int32 AnimationTicks = 20;
	constexpr int32 ParticleTicks = 15;
	// Ticks the totem takes to grow to its full size and turn half round.
	constexpr float GrowTicks = 4.0f;

	// Whether a death from damage of this kind (McSurvival::EDamage) can be cheated.
	MCPLAYER_API bool CanSave(uint8 DamageKind);
	// Uses up a totem from the main hand, or else the off hand, and gives the holder their life back with its effects.
	// False, changing nothing, if neither hand holds one.
	MCPLAYER_API bool TryUse(FMcPlayerStats& Stats, FMcItemStack& MainHand, FMcItemStack& OffHand);

	// Where the totem is on the holder's screen Age ticks into its flight: its middle as a share of the screen's width
	// and height from the top left, its height as a share of the screen's, and its turn about the upright (radians).
	// It grows in the middle from a tenth of the screen's height to just over half while turning half round, then
	// hangs there. Visible is false outside the flight.
	struct FFlight
	{
		bool bVisible = false;
		FVector2D Centre = FVector2D(0.5, 0.5);
		float Size = 0.0f;
		float Turn = 0.0f;
	};
	MCPLAYER_API FFlight GetFlight(float Age);
}
