// The 20 ticks a second the world runs on (minecraft.wiki, "Tick"): scheduled block ticks, and the rules that
// use them. Fluids flow (minecraft.wiki, "Water", "Lava"), and sand and gravel fall.
//
// A block change schedules a tick for the block and its six neighbours when they have something to do (a fluid
// to spread, a block to drop), so the world only does work where something changed. Game thread only.

#pragma once

#include "CoreMinimal.h"
#include "McGameEvent.h"
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
	// A projectile hit the target block at Pos: it gives Power (1 to 15) for Duration ticks.
	void HitTarget(const FIntVector& Pos, int32 Power, int32 Duration);
	// A lightning bolt struck at Pos: lightning rods in or under it power up.
	void LightningStruck(const FIntVector& Pos);
	// Whether a minecart is on the detector rail at Pos.
	void SetDetectorRail(const FIntVector& Pos, bool bOccupied);
	// What a container at Pos holds has changed: comparators that read it (beside it, or through a solid block) look again.
	void NotifyContainerChanged(const FIntVector& Pos);
	// A player put the block at Pos in place by hand. A tripwire hook put down this way looks along its line at once, which
	// one put by a command does not (reference run rs_trip4).
	void PlayerPlaced(const FIntVector& Pos);
	// A player broke the block that was at Pos (State is what it was).
	void PlayerBroke(const FIntVector& Pos, FMcStateId State);
	// Something made a vibration (frequency 1 to 15) at Pos; sculk sensors hear it. (Prefer EmitGameEvent.)
	void Vibrate(const FIntVector& Pos, int32 Frequency);
	// A game event happened at Pos (name as in McGameEvents, "minecraft:step" or "step"): sculk sensors in range hear it as a
	// vibration of its frequency, and every listener is told. Whoever simulates an entity calls this for what it does: steps,
	// landings, damage, eating, mounting, explosions and so on (the frequency table is in McGameEvent.cpp).
	void EmitGameEvent(FName Name, const FIntVector& Pos, const FMcGameEventSource& Source = FMcGameEventSource());
	// Listeners (a warden's hearing) get every event with a frequency; use McGameEvents::WardenCanHear and IsVibrationOccluded.
	int32 AddGameEventListener(TFunction<void(const FMcGameEvent&)> Listener);
	void RemoveGameEventListener(int32 Handle);
	// True if a wool or carpet block lies on the ray between the centres of the two cells (what stops a vibration).
	bool IsVibrationOccluded(const FIntVector& From, const FIntVector& To) const;
	// A player stands on the black top of the sculk shrieker at Pos. Only a naturally generated shrieker (can_summon) counts
	// warnings; any shrieker shrieks.
	void ShriekerTouched(const FIntVector& Pos, int32 PlayerId);
	// A book went into or out of slot 0 to 5 of the chiseled bookshelf at Pos: comparators read slot + 1 (a vibration of 11 as well).
	void BookshelfUsed(const FIntVector& Pos, int32 Slot);
	// A player's warning level, 0 to 4; it drops by 1 for every 12000 ticks since the player last set a shrieker off.
	int32 GetWarningLevel(int32 PlayerId) const;
	// A shrieker started shrieking for this player: the player's warning level now, and the Darkness (12 s, players within 40 blocks)
	// that follows when it ends. For whoever owns sounds and effects.
	TFunction<void(const FIntVector& Pos, int32 PlayerId, int32 WarningLevel)> OnShriek;
	// The shrieker at Pos has shrieked at warning level 4 for this player: a warden should try to emerge within 11x13x11 of it
	// (20 tries, none if another warden is within 24 blocks); for whoever owns the warden.
	TFunction<void(const FIntVector& Pos, int32 PlayerId)> OnWardenSummon;
	// (Used by the redstone side.)
	bool CanShriekFor(int32 PlayerId, bool bCanSummon) const;
	void ShriekerActivated(const FIntVector& Pos, int32 PlayerId, bool bCanSummon);
	void ShriekerFinished(const FIntVector& Pos, bool bCanSummon);
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
	// The sea level cold is measured from (63; a superflat world has -63).
	void SetSeaLevel(int32 Level);
	bool IsRainingAt(const FIntVector& Pos) const;

	// Bone meal on the block at Pos: grows a sapling (40% of the time, measured), a crop or stem, spreads grass over grass
	// blocks, doubles short grass. True if the item is used up.
	bool Bonemeal(const FIntVector& Pos);

	// A player's hand on a block (minecraft.wiki: "Lever", "Button", "Door", "Trapdoor", "Fence Gate"): flips a lever,
	// presses a button (stone ones for 20 ticks, wooden ones 30), opens or shuts a door, trapdoor or fence gate. Iron
	// doors and trapdoors answer only to redstone. A fence gate opened from its far side turns to open away from the
	// player. Yaw is where the player looks (Minecraft's: 0 south, 90 west). True if the block took the click.
	bool UseBlock(const FIntVector& Pos, float Yaw);

	// The TNT block at Pos is lit (by fire, flint and steel or redstone): the server turns it into primed TNT
	// (FMcPrimedTnt::Ignite). Redstone calls LightTnt when the power on a TNT block rises.
	TFunction<void(const FIntVector&)> OnLightTnt;
	void LightTnt(const FIntVector& Pos) const
	{
		if (OnLightTnt)
		{
			OnLightTnt(Pos);
		}
	}

	// Note blocks (McNoteBlock.h). PlayNote sounds the note block at Pos (the redstone side calls it when power on a
	// note block rises, setting `powered` itself; a punch in survival calls it too): false if there is no note block
	// or the space above it is not air or a head. TuneNote is a player's use: steps the note on (25 back to 0) and
	// plays it (a silenced block still steps). Both only on the server.
	bool PlayNote(const FIntVector& Pos);
	bool TuneNote(const FIntVector& Pos);

	// A powered dispenser or dropper at Pos fires: whatever keeps its items does the rest.
	TFunction<void(const FIntVector&)> OnDispense;
	// The bell at Pos was rung by redstone, as if struck on Side (McBell's numbering): it swings for every viewer.
	TFunction<void(const FIntVector&, int32)> OnBellRung;
	// An awake conduit at Pos looks round, every ConduitPeriod ticks: players within Range blocks of its middle who
	// are in water or rain get Conduit Power for ConduitPowerTicks. Hunting: it has the whole frame of 42.
	TFunction<void(const FIntVector&, int32 Range, bool bHunting)> OnConduitPulse;
	// Measured in vanilla (conduit_log): the conduit wakes, sleeps and refreshes the effect only on a check every 40
	// ticks, and the effect is seen at 259 the tick after it is given.
	static constexpr int32 ConduitPeriod = 40;
	static constexpr int32 ConduitPowerTicks = 260;

	// A note block played: where its note particle rises (above the block) and which note it shows.
	TFunction<void(const FVector&, int32)> OnNoteParticle;
	// A powered crafter at Pos tries to craft: it takes its items, hands on the product and says whether it made one.
	TFunction<bool(const FIntVector&)> OnCraft;

	// Sand, gravel and the like in the air: a falling block is not a block until it lands. Position is the bottom of the block.
	struct FFallingBlock
	{
		// Unique among the falling blocks, for whoever mirrors them as entities.
		int32 Id = 0;
		FIntVector Column;
		double Y = 0.0;
		// The distance fallen, summed in a float tick by tick as the game does (an anvil hurts and wears by it).
		float FallDistance = 0.0f;
		double Velocity = 0.0;
		FMcStateId State = 0;
	};
	const TArray<FFallingBlock>& GetFallingBlocks() const { return Falling; }
	// A falling block lands somewhere it can't be placed (a torch, a slab): it breaks and drops as an item.
	TFunction<void(const FIntVector&, FMcStateId)> OnFallingBlockDropped;
	// A falling block came to rest at the cell, having fallen the distance: for whoever hurts what is under an anvil.
	TFunction<void(const FIntVector&, FMcStateId, double)> OnFallingBlockLanded;
	// A piston started to push blocks (and its head) one cell along Dir: Cells are where the pushed shapes start. For two
	// ticks each moves half a block a tick, shoving what is in its way (reference run pushent): for whoever moves entities.
	TFunction<void(const TArray<FIntVector>&, const FIntVector&)> OnBlocksPushed;

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
	static constexpr int32 BubbleBaseDelay = 20;
	static constexpr int32 BubbleColumnDelay = 5;
	static constexpr int32 LavaDelay = 30;
	static constexpr int32 GravityDelay = 2;
	static constexpr int32 LeafDelay = 1;

private:
	void OnBlockChanged(const FIntVector& Pos, FMcStateId State);
	// Sets the instrument of the note block at Pos from what is below and above it (nothing if it is not one).
	void RefreshNoteBlock(const FIntVector& Pos);
	// A chunk loaded (from disk, the generator or the network): its conduits start their round of checks.
	void OnChunkAdded(FIntPoint ChunkPos);
	// Schedules the tick a block needs after something beside it changed.
	void Notify(const FIntVector& Pos);
	// A sponge with water beside it soaks up the water around (reference run rs_sponge).
	void AbsorbWater(const FIntVector& Sponge);
	void RunTick(const FIntVector& Pos);
	void TickFluid(const FIntVector& Pos, FMcStateId State);
	void TickConduit(const FIntVector& Pos);
	// Turns the water sources from Pos up into a bubble column, or back into water when nothing drives it.
	void UpdateBubbleColumn(const FIntVector& Pos);
	void TickGravity(const FIntVector& Pos, FMcStateId State);
	void StepFalling();
	void TickLeaves(const FIntVector& Pos, FMcStateId State);
	void TickSupport(const FIntVector& Pos, FMcStateId State);
	void TickFire(const FIntVector& Pos, FMcStateId State);
	static bool IsFireBlock(FMcStateId State);
	void RandomTicks();

	FMcBlockWorld& World;
	FDelegateHandle ChangedHandle;
	FDelegateHandle AddedHandle;
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
	TMap<int32, TFunction<void(const FMcGameEvent&)>> GameListeners;
	int32 NextListener = 1;
	// Shrieker warning: level, the tick it last rose, and the tick its next activation is allowed.
	struct FWarning { int32 Level = 0; int64 Raised = 0; int64 NextAllowed = 0; };
	TMap<int32, FWarning> Warnings;
	// Who set the shrieker at Pos off (for the warden summon at its end).
	TMap<FIntVector, int32> ShriekedFor;
};
