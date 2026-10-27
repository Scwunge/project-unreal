// When the background music plays (minecraft.wiki: "Music"): the dimension's music, or the biome's where the biome has
// its own, starts after a short wait in a new world and then, after each track, after a random wait between the
// music's least and greatest delays (10 to 20 minutes in the Overworld). Creative players hear the "creative" list
// where there is one. Music that replaces the current (the End's) stops another track for it. Clients only; this decides
// and the audio plays.

#pragma once

#include "CoreMinimal.h"
#include "McDimensionType.h"

class MCCORE_API FMcMusicPicker
{
public:
	enum class EAction : uint8
	{
		None,
		Start,
		Stop,
	};

	// Ticks before the first track.
	static constexpr int32 FirstDelay = 100;

	// One game tick. Wanted is the music for where the player is (null for none); Playing is the event of the track
	// playing now (none when nothing is); Draw is a random number in 0..1 for the next wait. Start means start Wanted's
	// sound, Stop means stop the track playing.
	EAction Tick(const FMcDimensionType::FMusic* Wanted, FName Playing, float Draw);

	// Which list a player hears: the creative one when in creative mode and the music has one, else the default.
	static const FMcDimensionType::FMusic* Choose(const TMap<FName, FMcDimensionType::FMusic>& Lists, bool bCreative);

	int32 GetDelay() const { return Delay; }

private:
	int32 Delay = FirstDelay;
	bool bWasPlaying = false;
};
