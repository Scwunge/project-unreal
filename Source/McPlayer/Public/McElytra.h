// Elytra (minecraft.wiki: "Elytra", "Firework Rocket"): wings worn in the chest slot. A jump in the air with them on
// starts a glide, which lasts until the player lands, goes into water or lava, catches a ladder or vine, or the wings
// wear down to their last point (they stop working there and are never broken). Gliding wears them by 1 a second.
//
// The flight here is this port's own model, tuned to the wiki's figures rather than taken from the game: the wings
// lift across the way the body moves, in proportion to the speed along the look and weaker as the look tips toward
// straight down, and the motion is turned toward the look. Tuned so that level flight glides about 10 blocks for each
// one lost at about 12 m/s, a dive tops out near 68 m/s, and pitching up trades speed for height until the glide
// stalls and drops.
//
// A firework rocket used while gliding pulls the player along the look at 33.5 blocks a second, for 10 ticks for each
// level of flight duration plus 10, and up to 11 more at random. Hitting a wall sideways hurts by 10 times the speed
// lost (blocks a tick) less 3.

#pragma once

#include "CoreMinimal.h"

struct FMcItemStack;

namespace McElytra
{
	constexpr double Gravity = 0.08;
	constexpr double LiftPerSpeed = 0.12;
	constexpr double MaxLift = 0.12;
	constexpr double Drag = 0.010;
	constexpr double DragPerSpeed = 0.004;
	constexpr double Turn = 0.1;
	// Blocks a tick a rocket pulls toward (33.5 blocks a second).
	constexpr double BoostSpeed = 33.5 / 20.0;
	constexpr double BoostPull = 0.15;
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
