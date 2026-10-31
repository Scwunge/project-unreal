// The 20 ticks a second the world runs on (minecraft.wiki, "Tick"): scheduled block ticks, and the rules that
// use them. Fluids flow (minecraft.wiki, "Water", "Lava"), and sand and gravel fall.
//
// A block change schedules a tick for the block and its six neighbours when they have something to do (a fluid
// to spread, a block to drop), so the world only does work where something changed. Game thread only.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "McSound.h"

class FMcBlockWorld;
class FMcNature;
class FMcRedstone;

enum class EMcWeather : uint8
{
	Clear,
	Rain,
	Thunder,
};

class MCSIM_API FMcSimulation
{
public:
	explicit FMcSimulation(FMcBlockWorld& InWorld);
	~FMcSimulation();
	UE_NONCOPYABLE(FMcSimulation);

	// Runs one game tick: every scheduled block tick that has come due, then the random ticks.
	void Tick();

	// The chunks that get random ticks (near players); each loaded one ticks RandomTickSpeed blocks per 16 high
	// section a tick, as the game's randomTickSpeed rule does (default 3).
	void SetTickingChunks(TArray<FIntPoint> Chunks) { TickingChunks = MoveTemp(Chunks); }
	void SetRandomTickSpeed(int32 Speed) { RandomTickSpeed = Speed; }
	// Runs the random tick rules for one block now (grass spreading, crops growing, leaves decaying). For tests.
	void RandomTickBlock(const FIntVector& Pos);

	int64 GetGameTime() const { return GameTime; }
	// Asks for a block tick Delay ticks from now at Pos (nothing if one is already waiting there).
	void ScheduleTick(const FIntVector& Pos, int32 Delay);
	int32 NumScheduled() const { return Pending.Num(); }

	// Redstone (minecraft.wiki, "Redstone circuits", "Piston"): dust, repeaters, comparators, torches, observers,
	// lamps, doors and pistons run off block changes and scheduled ticks like the rest. What stands on a pressure
	// plate is told to the simulation, and daylight detectors read the sun level (0 to 15) it is given.
	void SetPressurePlate(const FIntVector& Pos, int32 NumEntities);
	// Whether a minecart is on the detector rail at Pos.
	void SetDetectorRail(const FIntVector& Pos, bool bOccupied);
	void SetSunLight(int32 Level);
	int32 GetDustPower(const FIntVector& Pos) const;
	// SetSunLight fixes what detectors read; without it they follow the day clock and the weather.
	void ClearSunLight();

	// The day (minecraft.wiki, "Daylight cycle"): 24000 ticks, 0 at sunrise, 6000 noon, 12000 sunset, 18000 midnight.
	// It runs with the game time; SetDayTime moves it (e.g. /time set) without disturbing the scheduled ticks.
	int64 GetDayTime() const { return (GameTime + DayOffset) % 24000; }
	void SetDayTime(int64 DayTime);
	// The sun's light level now, 15 at noon down to 4 at night (less in rain and thunder).
	int32 GetSunLevel() const;

	// Weather (minecraft.wiki, "Weather"): clear, rain and thunder come and go on their own timers. SetWeather
	// forces one for Duration ticks (0: a random while). Rain fills cauldrons and wets farmland and puts out fire in
	// the open; in cold biomes it is snow, which lays one layer on the ground (SetSnowAccumulation for more), and
	// cold freezes water at the edge of a pool. Ice and snow beside a light melt.
	EMcWeather GetWeather() const;
	void SetWeather(EMcWeather Weather, int32 Duration = 0);
	// The doWeatherCycle rule: with it off the weather never changes by itself.
	void SetWeatherCycle(bool bCycle);
	void SetSnowAccumulation(int32 Layers);
	bool IsRainingAt(const FIntVector& Pos) const;

	// Bone meal on the block at Pos: grows a sapling (45% of the time), a crop or stem, spreads grass over grass
	// blocks, doubles short grass. True if the item is used up.
	bool Bonemeal(const FIntVector& Pos);

	// A player's hand on a block (minecraft.wiki: "Lever", "Button", "Door", "Trapdoor", "Fence Gate"): flips a lever,
	// presses a button (stone ones for 20 ticks, wooden ones 30), opens or shuts a door, trapdoor or fence gate. Iron
	// doors and trapdoors answer only to redstone. A fence gate opened from its far side turns to open away from the
	// player. Yaw is where the player looks (Minecraft's: 0 south, 90 west). True if the block took the click.
	bool UseBlock(const FIntVector& Pos, float Yaw);

	// A powered dispenser or dropper at Pos fires: whatever keeps its items does the rest.
	TFunction<void(const FIntVector&)> OnDispense;

	// Sand, gravel and the like in the air: a falling block is not a block until it lands. Position is the bottom of the block.
	struct FFallingBlock
	{
		// Unique among the falling blocks, for whoever mirrors them as entities.
		int32 Id = 0;
		FIntVector Column;
		double Y = 0.0;
		double Velocity = 0.0;
		FMcStateId State = 0;
	};
	const TArray<FFallingBlock>& GetFallingBlocks() const { return Falling; }
	// A falling block lands somewhere it can't be placed (a torch, a slab): it breaks and drops as an item.
	TFunction<void(const FIntVector&, FMcStateId)> OnFallingBlockDropped;

	// Where the simulation's sounds go: the server sends them on to the players.
	TFunction<void(const FMcSoundEvent&)> OnSound;
	void PlaySound(const FMcSoundEvent& Event) const
	{
		if (OnSound && !Event.Name.IsNone())
		{
			OnSound(Event);
		}
	}

	// Water and lava delays: water flows every 5 ticks, lava every 30 in the overworld.
	static constexpr int32 WaterDelay = 5;
	static constexpr int32 LavaDelay = 30;
	static constexpr int32 GravityDelay = 2;
	static constexpr int32 LeafDelay = 1;

private:
	void OnBlockChanged(const FIntVector& Pos, FMcStateId State);
	// Schedules the tick a block needs after something beside it changed.
	void Notify(const FIntVector& Pos);
	void RunTick(const FIntVector& Pos);
	void TickFluid(const FIntVector& Pos, FMcStateId State);
	void TickGravity(const FIntVector& Pos, FMcStateId State);
	void StepFalling();
	void TickLeaves(const FIntVector& Pos, FMcStateId State);
	void TickSupport(const FIntVector& Pos, FMcStateId State);
	void TickFire(const FIntVector& Pos, FMcStateId State);
	static bool IsFireBlock(FMcStateId State);
	void RandomTicks();

	FMcBlockWorld& World;
	FDelegateHandle ChangedHandle;
	int64 GameTime = 0;
	// Positions by the tick they are due.
	TMap<int64, TArray<FIntVector>> Due;
	TSet<FIntVector> Pending;
	TArray<FFallingBlock> Falling;
	int32 NextFallingId = 1;
	TArray<FIntPoint> TickingChunks;
	int32 RandomTickSpeed = 3;
	FRandomStream Random{ 1 };
	TUniquePtr<FMcRedstone> Redstone;
	TUniquePtr<FMcNature> Nature;
	int64 DayOffset = 0;
	bool bSunFixed = false;
};
