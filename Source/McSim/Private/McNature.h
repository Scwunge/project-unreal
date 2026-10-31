// Weather, the day, and what they do to the land (minecraft.wiki, "Weather", "Daylight cycle", "Snow", "Ice",
// "Sapling", "Bone Meal"): rain and thunder come and go on timers; rain fills cauldrons, wets farmland and puts out
// fire; cold biomes freeze water and gather snow; ice and snow melt near light; saplings grow into trees.
//
// The weather and the clock belong to the simulation (FMcSimulation hands out getters); this class holds them and
// the block rules that read them. Trees are grown with McGen's tree features.

#pragma once

#include "CoreMinimal.h"
#include "McBiomeRegistry.h"
#include "McCore.h"
#include "McSimulation.h"

class FMcBlockWorld;
class FMcFeatureLibrary;

class FMcNature
{
public:
	FMcNature(FMcBlockWorld& InWorld, FMcSimulation& InSim);
	~FMcNature();

	// One game tick of the weather timers.
	void Tick();
	EMcWeather GetWeather() const;
	void SetWeather(EMcWeather Weather, int32 Duration);
	// How many layers of snow a snowfall piles up (the game's snowAccumulationHeight rule, default 1).
	void SetSnowAccumulation(int32 Layers) { SnowAccumulation = Layers; }

	// The sun's light level from the day clock and the weather: 15 at noon, 4 on a clear night.
	int32 GetSunLight() const;
	// What a daylight detector reads, 0 to 15: it follows the sun's height as well, so night reads 0.
	int32 GetDetectorLevel() const;

	// Biome the block at Pos is in, its temperature there and what falls from the sky.
	enum class EPrecipitation : uint8 { None, Rain, Snow };
	float GetTemperature(const FIntVector& Pos) const;
	EPrecipitation GetPrecipitation(const FIntVector& Pos) const;
	// Rain or snow is falling on Pos now: raining, nothing above, the biome has some.
	bool IsRainingAt(const FIntVector& Pos) const;
	bool IsExposedToSky(const FIntVector& Pos) const;

	// Block light an estimate: sources within reach, each dimmed by distance (walls aren't counted).
	int32 BlockLightAt(const FIntVector& Pos) const;
	// Sky light the same way: the sky's level less what the blocks above hold back.
	int32 SkyLightAt(const FIntVector& Pos) const;

	// The random tick rules for ice, snow, cauldrons, saplings. False if the block isn't one of those.
	bool RandomTick(const FIntVector& Pos, FMcStateId State);
	// Once per chunk and tick, a one-in-sixteen chance of a random column being frozen or snowed on.
	void TickChunk(FIntPoint Chunk);
	void SetWeatherCycle(bool bCycle) { bWeatherCycle = bCycle; }

	// Bone meal on the block at Pos (a sapling, a crop, grass). True if it did anything, so the item is used up.
	bool Bonemeal(const FIntVector& Pos);

private:
	struct FBiomeInfo
	{
		float Temperature = 0.8f;
		bool bPrecipitation = true;
	};
	const FBiomeInfo& InfoAt(const FIntVector& Pos) const;
	void NextRain();
	void NextThunder();
	void FreezeColumn(const FIntVector& Top);
	bool GrowSapling(const FIntVector& Pos, FMcStateId State);
	bool GrowTree(const FIntVector& Pos, FMcStateId State);
	void SpreadGrass(const FIntVector& Pos);

	FMcBlockWorld& World;
	FMcSimulation& Sim;
	FRandomStream Random{ 7 };
	// The doWeatherCycle rule: off, the weather stays as it is (a command can still change it).
	bool bWeatherCycle = true;
	bool bRaining = false;
	bool bThundering = false;
	// How much rain and thunder the sky shows, 0 to 1: they follow the flags by 0.01 a tick.
	float RainLevel = 0.0f;
	float ThunderLevel = 0.0f;
	int32 RainTime = 0;
	int32 ThunderTime = 0;
	int32 SnowAccumulation = 1;
	mutable TMap<FMcBiomeId, FBiomeInfo> Biomes;
	TUniquePtr<FMcFeatureLibrary> Features;
};
