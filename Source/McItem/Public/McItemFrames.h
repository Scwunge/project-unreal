// The compass and the clock show a different picture of themselves as the world turns: compass_00 to _31 by the angle
// to the spawn (or the lodestone, or the death place), clock_00 to _63 by the time of day (the game's item model
// definitions: a range_dispatch on minecraft:compass with scale 32 and minecraft:time with scale 64).
//
// Whoever knows where the player is sets the context once a frame; whoever draws an item asks Resolve for the item
// whose picture to use. Not for the game thread's rivals: one context, set and read on the game thread.

#pragma once

#include "CoreMinimal.h"
#include "McItem.h"

namespace McItemFrames
{
	struct FContext
	{
		// Where the player stands and faces (Minecraft yaw in degrees, 0 south, 90 west), in block coordinates.
		FVector2D Position = FVector2D::ZeroVector;
		double Yaw = 0.0;
		// What a compass points at: the world spawn.
		FVector2D Spawn = FVector2D::ZeroVector;
		// The time of day in ticks (any value; wrapped by the day), and whether this is the overworld (other
		// dimensions have a clock that spins about).
		double DayTime = 0.0;
		bool bOverworld = true;
		// Game ticks so far; the needles' springs step once per tick since the last call.
		int64 GameTicks = 0;
	};

	// Sets the context and moves the needles on to now.
	MCITEM_API void SetContext(const FContext& Context);

	// The clock's 0 to 1 value for a time of day in ticks, without the needle's spring (a frame is 1/64).
	MCITEM_API double ClockValue(double DayTime);

	// The compass angle, 0 to 1 (0 or 1 when the target is straight ahead), without the needle's spring.
	MCITEM_API double CompassValue(const FVector2D& From, double Yaw, const FVector2D& Target);

	// The item whose picture to draw for Item: compass_NN or clock_NN or recovery_compass_NN for the three, Item itself for
	// anything else.
	MCITEM_API FName Resolve(FName Item);

	// Which of Count frames a 0 to 1 value falls on (the game's thresholds sit at half frames: 0, 0.5, 1.5 ... Count - 0.5).
	MCITEM_API int32 FrameOf(double Value, int32 Count);
}
