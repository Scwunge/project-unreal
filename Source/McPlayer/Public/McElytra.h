// Elytra (minecraft.wiki: "Elytra", "Firework Rocket"): wings worn in the chest slot. A jump in the air with them on
// starts a glide, which lasts until the player lands, goes into water or lava, catches a ladder or vine, or the wings
// wear down to their last point (they stop working there and are never broken). Gliding wears them by 1 a second.
//
// The flight here is this port's own model, its numbers fitted to vanilla glides measured from positions read 10 ticks
// apart (mc-reference scripts/glide_steady.txt: long glides looking level, 20 down, 20 up, 45 up and straight down):
// the wings carry most of the weight looking level, a little of the fall is turned into speed ahead, looking up
// trades speed for height, the fall is damped, and the whole speed meets a drag that grows with it. As measured, level
// flight runs at about 24 m/s after 10 s and 30 after 20 (still gaining) and sinks about 3 m/s, 20 down holds about
// 37 m/s, a climb at 20 up rises steeply and then stalls in a few seconds, and a vertical dive averages about 60 m/s
// over its first 10 seconds while still drifting ahead. The fit is within about a tenth of those, except that climbs
// lose their speed ahead faster than vanilla's (a 45 degree climb after a stall creeps at 0.5 blocks a tick, not 3).
//
// A firework rocket used while gliding pulls the player along the look at 33.5 blocks a second (wiki; measured 1.673
// blocks a tick), for 10 ticks for each level of flight duration plus 10, and up to 11 more at random (wiki). Measured
// (mc-reference scripts/glide_speed.txt, positions every 2 ticks after the click), the gap to that speed about halves
// each tick. Hitting a wall sideways hurts by 10 times the speed lost (blocks a tick) less 3 (wiki).

#pragma once

#include "CoreMinimal.h"

struct FMcItemStack;

namespace McElytra
{
	// The player's gravity (minecraft.wiki "Entity", acceleration of players), blocks a tick a tick.
	constexpr double Gravity = 0.08;
	// Fitted to scripts/glide_steady.txt (see above).
	// Held to the wiki's glide ratio (about 10) in a long level glide.
	constexpr double Carry = 0.891;
	constexpr double CarryPower = 0.412;
	constexpr double FallToAhead = 0.0312;
	constexpr double AheadToClimb = 0.0984;
	constexpr double KeepUp = 0.9845;
	// Each tick loses this much of the speed for each block a tick of it.
	constexpr double DragPerSpeed = 0.00143;
	// How far the motion along the ground is pulled toward the look's heading each tick: measured from a 90 degree turn
	// (scripts/glide_turn.txt), the old way's part shrinks by 0.891 a tick and the speed falls from 1.33 to 0.82.
	constexpr double Turn = 0.108;
	// Blocks a tick a rocket pulls toward (33.5 blocks a second).
	constexpr double BoostSpeed = 33.5 / 20.0;
	constexpr double BoostPull = 0.5;
	// Ticks a glide wears the wings by one.
	constexpr int32 WearTicks = 20;

	// Whether the chest slot's stack lets its wearer glide: elytra with more than their last point left.
	MCPLAYER_API bool CanGlide(const FMcItemStack& Chest);
	// The look as a direction (Minecraft yaw and pitch, degrees, pitch down positive).
	MCPLAYER_API FVector LookOf(float Yaw, float Pitch);
	// One tick of gliding: the velocity (blocks a tick) after it.
	MCPLAYER_API FVector Glide(const FVector& Velocity, float Yaw, float Pitch);
	// One tick of a rocket's pull, after the glide.
	MCPLAYER_API FVector Boost(const FVector& Velocity, float Yaw, float Pitch);
	// How long a rocket of this flight duration pulls (ticks).
	MCPLAYER_API int32 RollBoostTicks(FRandomStream& Random, int32 FlightDuration);
	// The harm of flying into a wall: the horizontal speed before and after the bump (blocks a tick). 0 if none.
	MCPLAYER_API float WallDamage(double SpeedBefore, double SpeedAfter);
}
