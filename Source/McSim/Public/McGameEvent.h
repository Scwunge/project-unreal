// Game events: what the world "hears". Sculk sensors, calibrated sensors and shriekers listen for them as vibrations; a warden
// (or anything else) can listen too with FMcSimulation::AddGameEventListener. Frequencies from minecraft.wiki, "Vibration"
// (the table of game events; the wiki marks block_change "more information needed").
#pragma once

#include "CoreMinimal.h"

// Who or what made an event; sensors, shriekers and wardens care.
struct FMcGameEventSource
{
	bool bPlayer = false;
	bool bSneaking = false;
	// A warden's own events are not heard by sensors, nor by other wardens.
	bool bWarden = false;
	bool bProjectile = false;
	// A mob that is dying (wardens ignore these) and a player in Creative or Spectator mode (wardens ignore them).
	bool bDying = false;
	bool bCreativeOrSpectator = false;
	bool bArmorStand = false;
	// Which player (any id the caller uses), for shriekers' warning levels.
	int32 PlayerId = 0;
};

struct FMcGameEvent
{
	FName Name;
	int32 Frequency = 0;
	FIntVector Pos = FIntVector::ZeroValue;
	FMcGameEventSource Source;
};

namespace McGameEvents
{
	// The frequency (1 to 15) of the named event ("minecraft:step" or just "step"); 0 if it makes no vibration or is unknown.
	MCSIM_API int32 FrequencyOf(FName Name);
	// Every named event that makes a vibration.
	MCSIM_API const TArray<FName>& AllNames();
	// A warden listens within a 15 block radius (an ovoid on the wiki; here a sphere), not to wardens, armor stands, dying mobs or
	// players in Creative or Spectator mode (minecraft.wiki, "Warden"). Wool in the way is checked by FMcSimulation::IsVibrationOccluded.
	MCSIM_API bool WardenCanHear(const FMcGameEvent& Event, const FIntVector& WardenPos);
	// While sneaking, a player is not heard making frequency 1, 2 or 3 events (minecraft.wiki, "Sculk Sensor").
	MCSIM_API bool SneakingHides(const FMcGameEvent& Event);
}
