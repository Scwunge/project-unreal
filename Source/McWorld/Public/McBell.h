// Bells (minecraft.wiki, "Bell"): using one rings it, and it swings away from the side it was struck on for a couple
// of seconds. The bell hangs from a bar and swings across it, so it can be struck only from the sides the bar doesn't
// run to: a floor bell from its front and back, a wall bell from either side of its bar, a ceiling bell from any side.
//
// Sides are numbered 0 north, 1 south, 2 west, 3 east.

#pragma once

#include "CoreMinimal.h"
#include "McBlockRegistry.h"

namespace McBell
{
	// How long a bell swings once struck, ticks.
	constexpr int32 RingTicks = 50;

	MCWORLD_API bool IsBell(FMcStateId State);

	// The side of the bell at BellPos that a player whose eye is at Eye strikes: the side of the block they are
	// nearest square on to. INDEX_NONE if that side can't be struck, or the state isn't a bell.
	MCWORLD_API int32 GetStruckSide(FMcStateId State, const FIntVector& BellPos, const FVector& Eye);

	// How far the bell is swung, radians, Ticks after it was struck (0 at rest from RingTicks on): back and forth,
	// dying away. Positive is away from the struck side.
	MCWORLD_API float GetSwing(float Ticks);
}
