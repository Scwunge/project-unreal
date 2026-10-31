// Shulker box lids, as they open and close on every machine (the server says how many have each box open). Fitted to
// vanilla's logged per-tick state (E:/mc-reference/scripts/shulker_log.txt, `log shulker`): progress goes 0.1 a tick
// towards 1 while anyone has the box open and back to 0 when no one does; the box's collision is the block grown by
// half its progress the way it faces (1.5 blocks open). While opening, a thing in the lid's way is moved 0.11 a tick
// along the facing: whatever overlaps the block shifted by progress (and 0.01 more). A box does not open when a block's
// collision is in the half block in front of it (stone, a bottom slab, carpet, a chest did stop it; a top slab, a torch
// did not).

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;

class MCENTITY_API FMcShulkerLids
{
public:
	static constexpr float Step = 0.1f;

	explicit FMcShulkerLids(FMcBlockWorld& InWorld) : World(InWorld) {}

	// How many have the box at Pos open now (from the server).
	void SetOpeners(const FIntVector& Pos, int32 Count);
	int32 GetOpeners(const FIntVector& Pos) const;

	// One tick: each lid steps, the block's collision follows it, and this tick's pushes are worked out.
	void Tick();
	int64 GetTicks() const { return Ticks; }

	float GetProgress(const FIntVector& Pos) const;
	// The motion this tick's opening lids give a thing whose box is Box (zero for most).
	FVector GetPush(const FBox& Box) const;

	// The step from a box to the block its lid opens into.
	static FIntVector GetFacing(FMcStateId State);
	// Whether a block's collision is where the box at Pos would open, so it stays shut.
	static bool IsBlocked(const FMcBlockWorld& World, const FIntVector& Pos);

private:
	struct FLid
	{
		int32 Openers = 0;
		float Progress = 0.0f;
	};
	struct FPush
	{
		FBox Area;
		FVector Motion;
	};

	FMcBlockWorld& World;
	TMap<FIntVector, FLid> Lids;
	TArray<FPush> Pushes;
	int64 Ticks = 0;
};
