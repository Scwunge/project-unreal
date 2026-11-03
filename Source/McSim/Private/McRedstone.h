// Redstone (minecraft.wiki, "Redstone circuits", "Redstone Dust", "Piston"): power sources, dust, the components
// that read and give power, and pistons. It is the part of the simulation FMcSimulation hands block changes and
// due ticks to for redstone blocks; everything it does to the world goes through FMcBlockWorld::SetBlock.
//
// Power follows the wiki's strong and weak rules: a block gives a signal to the blocks around it, and some of them
// also strongly power the block they touch, which then gives that power on to its own neighbours. Dust is worked
// out a whole connected network at a time so a loop of dust cannot hold power with nothing feeding it.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;
class FMcSimulation;

class FMcRedstone
{
public:
	FMcRedstone(FMcBlockWorld& InWorld, FMcSimulation& InSim);

	// A block changed: the blocks around it (and some one step further) may have something to do.
	void OnBlockChanged(const FIntVector& Pos, FMcStateId State);
	// A scheduled tick came due. False if the block isn't a redstone block, so the caller carries on with it.
	bool RunTick(const FIntVector& Pos, FMcStateId State);
	// Pistons told of a change while a game tick's scheduled ticks run act at the end of that same tick (block events),
	// not the next one (reference run rs_world, an observer pulse driving a piston).
	void BeginTick()
	{
		bInTick = true;
		for (const FIntVector& Pos : NextPistonEvents) PistonEvents.AddUnique(Pos);
		NextPistonEvents.Reset();
	}
	void EndTick();

	// What stands on a pressure plate: how many entities. Pressing and releasing follow.
	void SetPlate(const FIntVector& Pos, int32 NumEntities);
	void HitTarget(const FIntVector& Pos, int32 Power, int32 Duration);
	// Something at Source made a vibration of the given frequency (1 to 15): sculk sensors within range hear it after a delay
	// of one tick a block (minecraft.wiki, "Sculk Sensor"; reference run rs_sculk).
	void Vibrate(const FIntVector& Source, int32 Frequency);
	// A minecart on a detector rail (or none): the rail powers while it is there and for a second after.
	void SetDetector(const FIntVector& Pos, bool bOccupied);
	// A player's hand on a lever, button, door, trapdoor or fence gate (see FMcSimulation::UseBlock).
	bool Use(const FIntVector& Pos, float Yaw);
	// A container at Pos changed what it holds: the comparators that read it look again.
	void NotifyContainerChanged(const FIntVector& Pos);
	void PlayerPlaced(const FIntVector& Pos);
	// A lightning bolt hit Pos (or the cell above a lightning rod).
	void LightningStruck(const FIntVector& Pos);
	// A player broke the block that was at Pos (its kind in State): a vibration of 12.
	void PlayerBroke(const FIntVector& Pos, FMcStateId State);

	// Daylight detectors read this, 0 (night) to 15 (noon).
	void SetSunLight(int32 Level) { SunLight = Level; SkyLevel = Level; SunScale = 1.0; }
	// What a plain detector reads (the sky level scaled by the sun's height) and the sky level an inverted one subtracts.
	// Scale is the factor Plain is the sky level times, for a detector that sees less than the open sky.
	void SetDetector(int32 Plain, int32 Sky, double Scale) { SunLight = Plain; SkyLevel = Sky; SunScale = Scale; }

	// The power of the dust at Pos, 0 if there is none.
	int32 GetDustPower(const FIntVector& Pos) const;

private:
	// Direction indices: 0 north, 1 east, 2 south, 3 west (each the next clockwise), 4 up, 5 down.
	// "Toward" below is the direction from the block asking to the block it asks about, as in the game.
	int32 Signal(FMcStateId State, const FIntVector& Pos, int32 Toward) const;
	int32 DirectSignal(FMcStateId State, const FIntVector& Pos, int32 Toward) const;
	int32 DirectSignalTo(const FIntVector& Pos) const;
	int32 SignalAt(const FIntVector& Pos, int32 Toward) const;
	int32 BestSignal(const FIntVector& Pos) const;
	int32 ControlInput(const FIntVector& Pos, int32 Toward, bool bOnlyDiodes) const;
	int32 ContainerLevel(const FIntVector& Pos) const;
	bool IsConductor(FMcStateId State) const;

	void Notify(const FIntVector& Pos);

	// Dust.
	void FlushDust();
	FMcStateId DustShape(const FIntVector& Pos, FMcStateId State, int32 Power) const;
	void DustReads(const FIntVector& Pos, TArray<FIntVector, TInlineAllocator<8>>& Out) const;
	bool ConnectsTo(FMcStateId State, int32 Toward) const;

	// Components.
	void TickRepeater(const FIntVector& Pos, FMcStateId State);
	int32 RepeaterInput(const FIntVector& Pos, FMcStateId State) const;
	bool IsRepeaterLocked(const FIntVector& Pos, FMcStateId State) const;
	void TickComparator(const FIntVector& Pos, FMcStateId State);
	int32 ComparatorOutput(const FIntVector& Pos, FMcStateId State) const;
	void TickTorch(const FIntVector& Pos, FMcStateId State);
	bool IsTorchPowered(const FIntVector& Pos, FMcStateId State) const;
	bool IsToggledTooOften(const FIntVector& Pos, bool bAdd);
	void TickLamp(const FIntVector& Pos, FMcStateId State);
	void TickObserver(const FIntVector& Pos, FMcStateId State);
	void TickOpening(const FIntVector& Pos, FMcStateId State);
	void TickButton(const FIntVector& Pos, FMcStateId State);
	void TickPlate(const FIntVector& Pos, FMcStateId State);
	void TickTripwire(const FIntVector& Pos, FMcStateId State);
	// Recomputes every hook whose line passes through Pos (or is Pos), and those hooks' ends.
	void UpdateTripwires(const FIntVector& Pos);
	void UpdateHookLine(const FIntVector& Hook);
	void ShapeTripwire(const FIntVector& Pos);
	// The hook being put in place by the change being handled (X is INT32_MIN when none).
	FIntVector HookPlaced = FIntVector(INT32_MIN);
	static constexpr int32 TripwireDelay = 10;
	// Strings that are disarmed now, and whether the change being handled is one of them going (shears).
	TSet<FIntVector> DisarmedStrings;
	bool bShornBreak = false;
	// Every string seen, and whether the change being handled turns one into another state of itself.
	TSet<FIntVector> StringCells;
	bool bStringReplaced = false;
	bool bDetachLine = false;
	FIntVector RemovedString = FIntVector(INT32_MIN);
	FIntVector ChangedPos = FIntVector(INT32_MIN);
	// The hook each attached hook is joined to.
	TMap<FIntVector, FIntVector> HookPartner;
	static constexpr int32 TripwireMaxLength = 40;
	void TickTarget(const FIntVector& Pos, FMcStateId State);
	void TickDaylight(const FIntVector& Pos, FMcStateId State);
	// The sky light that reaches the cell at Pos, 0 to 15, from the light engine (15 where nothing above dims it).
	int32 SkyLightAt(const FIntVector& Pos) const;
	bool IsSupported(const FIntVector& Pos, FMcStateId State) const;
	bool IsRailPowered(const FIntVector& Pos, FMcStateId State) const;
	void TickDetector(const FIntVector& Pos, FMcStateId State);
	bool HasPower(const FIntVector& Pos) const;

	// Pistons.
	void TickPiston(const FIntVector& Pos, FMcStateId State);
	void TickPistonHead(const FIntVector& Pos, FMcStateId State);
	void TickMovingPiston(const FIntVector& Pos, FMcStateId State);
	bool ShouldExtend(const FIntVector& Pos, int32 Facing) const;
	bool Extend(const FIntVector& Pos, FMcStateId State, int32 Facing, bool bSticky);
	void Retract(const FIntVector& Pos, FMcStateId State, int32 Facing, bool bSticky);
	bool ResolvePush(const FIntVector& PistonPos, const FIntVector& Start, int32 Toward, TArray<FIntVector>& Move, TArray<FIntVector>& Destroy) const;
	void MoveBlocks(const TArray<FIntVector>& Move, const TArray<FIntVector>& Destroy, int32 Toward, int32 PistonFacing);
	bool IsPushable(const FIntVector& Pos, FMcStateId State) const;
	bool IsBreakable(FMcStateId State) const;

	struct FChange
	{
		FName Name;
		FName Value;
		FChange(const TCHAR* InName, const TCHAR* InValue) : Name(InName), Value(InValue) {}
		FChange(const TCHAR* InName, int32 InValue) : Name(InName), Value(*FString::FromInt(InValue)) {}
	};
	FMcStateId With(FMcStateId State, std::initializer_list<FChange> Changes) const;
	void Set(const FIntVector& Pos, std::initializer_list<FChange> Changes);
	int32 Number(FMcStateId State, const TCHAR* Property) const;
	bool Flag(FMcStateId State, const TCHAR* Property) const;
	int32 FacingOf(FMcStateId State) const;
	bool IsKind(const FIntVector& Pos, int32 Kind) const;

	FMcBlockWorld& World;
	FMcSimulation& Sim;
	TSet<FIntVector> DirtyDust;
	bool bFlushing = false;
	bool bInTick = false;
	// A moving block is landing: its neighbours hear of it after this tick's block events, so a piston acts next tick.
	bool bLanding = false;
	TArray<FIntVector> NextPistonEvents;
	// Composters at level 7 with their turn to 8 scheduled.
	TSet<FIntVector> ComposterPending;
	TArray<FIntVector> PistonEvents;
	// Set while an observer changes its own state, which is not a new observer.
	bool bObserverSelf = false;
	// Set while a block's state is changed without telling its neighbours (a dispenser's triggered flag).
	bool bQuietChange = false;
	// Set while a dust network is worked out: dust gives no signal to the blocks it is reading.
	bool bIgnoreDust = false;
	int32 SunLight = 15;
	int32 SkyLevel = 15;
	double SunScale = 1.0;
	TMap<FIntVector, int32> ComparatorOutputs;
	TMap<FIntVector, TArray<int64>> TorchToggles;
	TMap<FIntVector, int32> Plates;
	TMap<FIntVector, int64> TargetUntil;
	// A vibration on its way to a sensor: when it arrives and the strength it gives.
	struct FVibration { int64 Arrive; int32 Power; };
	TMap<FIntVector, FVibration> Vibrations;
	void TickSculk(const FIntVector& Pos, FMcStateId State);
	TSet<FIntVector> Detectors;
	// Blocks a piston is carrying: the state, and the game tick it lands on.
	TMap<FIntVector, TPair<FMcStateId, int64>> MovingBlocks;
	// When each extended piston started to extend, for the rule that a short pulse leaves what it pushed behind.
	TMap<FIntVector, int64> ExtendedAt;
};
