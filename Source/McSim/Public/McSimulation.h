// The 20 ticks a second the world runs on (minecraft.wiki, "Tick"): scheduled block ticks, and the rules that
// use them. Fluids flow (minecraft.wiki, "Water", "Lava"), and sand and gravel fall.
//
// A block change schedules a tick for the block and its six neighbours when they have something to do (a fluid
// to spread, a block to drop), so the world only does work where something changed. Game thread only.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;

class MCSIM_API FMcSimulation
{
public:
	explicit FMcSimulation(FMcBlockWorld& InWorld);
	~FMcSimulation();
	UE_NONCOPYABLE(FMcSimulation);

	// Runs one game tick: every scheduled block tick that has come due.
	void Tick();

	int64 GetGameTime() const { return GameTime; }
	// Asks for a block tick Delay ticks from now at Pos (nothing if one is already waiting there).
	void ScheduleTick(const FIntVector& Pos, int32 Delay);
	int32 NumScheduled() const { return Pending.Num(); }

	// Water and lava delays: water flows every 5 ticks, lava every 30 in the overworld.
	static constexpr int32 WaterDelay = 5;
	static constexpr int32 LavaDelay = 30;
	static constexpr int32 GravityDelay = 2;

private:
	void OnBlockChanged(const FIntVector& Pos, FMcStateId State);
	// Schedules the tick a block needs after something beside it changed.
	void Notify(const FIntVector& Pos);
	void RunTick(const FIntVector& Pos);
	void TickFluid(const FIntVector& Pos, FMcStateId State);
	void TickGravity(const FIntVector& Pos, FMcStateId State);

	FMcBlockWorld& World;
	FDelegateHandle ChangedHandle;
	int64 GameTime = 0;
	// Positions by the tick they are due.
	TMap<int64, TArray<FIntVector>> Due;
	TSet<FIntVector> Pending;
};
