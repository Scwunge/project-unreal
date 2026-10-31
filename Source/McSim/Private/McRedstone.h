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

	// What stands on a pressure plate: how many entities. Pressing and releasing follow.
	void SetPlate(const FIntVector& Pos, int32 NumEntities);
	void HitTarget(const FIntVector& Pos, int32 Power, int32 Duration);
	// A minecart on a detector rail (or none): the rail powers while it is there and for a second after.
	void SetDetector(const FIntVector& Pos, bool bOccupied);
	// A player's hand on a lever, button, door, trapdoor or fence gate (see FMcSimulation::UseBlock).
	bool Use(const FIntVector& Pos, float Yaw);

	// Daylight detectors read this, 0 (night) to 15 (noon).
	void SetSunLight(int32 Level) { SunLight = Level; SkyLevel = Level; }
	// What a plain detector reads (the sky level scaled by the sun's height) and the sky level an inverted one subtracts.
	void SetDetector(int32 Plain, int32 Sky) { SunLight = Plain; SkyLevel = Sky; }

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
	void TickTarget(const FIntVector& Pos, FMcStateId State);
	void TickDaylight(const FIntVector& Pos, FMcStateId State);
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
	// Set while an observer changes its own state, which is not a new observer.
	bool bObserverSelf = false;
	// Set while a dust network is worked out: dust gives no signal to the blocks it is reading.
	bool bIgnoreDust = false;
	int32 SunLight = 15;
	int32 SkyLevel = 15;
	TMap<FIntVector, int32> ComparatorOutputs;
	TMap<FIntVector, TArray<int64>> TorchToggles;
	TMap<FIntVector, int32> Plates;
	TMap<FIntVector, int64> TargetUntil;
	TSet<FIntVector> Detectors;
	// Blocks a piston is carrying: the state, and the game tick it lands on.
	TMap<FIntVector, TPair<FMcStateId, int64>> MovingBlocks;
	// When each extended piston started to extend, for the rule that a short pulse leaves what it pushed behind.
	TMap<FIntVector, int64> ExtendedAt;
};
