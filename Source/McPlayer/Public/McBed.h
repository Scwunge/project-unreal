// Beds (minecraft.wiki: "Bed"): when a player may sleep, where they lie, and where they stand up or respawn.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;
class FMcEntityManager;

namespace McBed
{
	// Ticks every player in the world must have been in bed before the night is skipped.
	constexpr int32 TicksToSleep = 100;
	// The sky time (ticks into the day) from which the night runs, and to which it runs.
	constexpr int64 NightStart = 12542;
	constexpr int64 NightEnd = 23459;

	// Why a click on a bed did not put the player to sleep, or None if it did.
	enum class EResult : uint8 { Sleeps, NotNow, MonstersNearby, TooFar, NotOverworld };

	MCPLAYER_API bool IsBed(FStringView BlockName);
	// Whether it is dark enough to sleep: at night, or in a thunderstorm.
	MCPLAYER_API bool CanSleepAt(int64 DayTime, bool bThundering);
	// The first morning after DayTime: the next multiple of a day.
	MCPLAYER_API int64 GetMorning(int64 DayTime);

	// Where the other half of the bed is (block coordinates), given either half. Unset if Pos is no bed.
	MCPLAYER_API TOptional<FIntVector> GetHead(const FMcBlockWorld& World, const FIntVector& Pos);
	// Where a player's feet are while they lie in the bed whose head is at Head.
	MCPLAYER_API FVector GetSleepPosition(const FIntVector& Head);
	// Minecraft yaw that has the player's head toward the pillow: they look along the bed's length, from foot to head.
	MCPLAYER_API TOptional<float> GetSleepYaw(const FMcBlockWorld& World, const FIntVector& Head);
	// Where a player stands up beside the bed at Pos (either half), or respawns: the first free spot round it with
	// ground underneath. Unset if there is none.
	MCPLAYER_API TOptional<FVector> FindStandPosition(const FMcBlockWorld& World, const FIntVector& Pos);
	// Whether a hostile mob that is after players is within 8 blocks across and 5 up or down of the bed.
	MCPLAYER_API bool HasMonstersNear(const FMcEntityManager& Entities, const FIntVector& Pos);
}
