// What each block state is like to bump into and walk on: collision boxes, slipperiness and speed factors.
// The server needs these too, so they come from block names and states (facts from minecraft.wiki), not from
// the client's models.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class MCCORE_API FMcBlockShapes
{
public:
	static FMcBlockShapes& Get();

	// Game thread. Loads the registry if needed.
	bool EnsureLoaded();
	bool IsLoaded() const { return States.Num() > 0; }

	// Boxes in block units, Minecraft axes (x east, y up, z south), relative to the block's minimum corner.
	// Usually within 0..1, but fences and walls reach 1.5 high. Empty for blocks you walk through.
	TConstArrayView<FBox3f> GetCollision(FMcStateId State) const;
	bool HasCollision(FMcStateId State) const { return States[State].NumBoxes > 0; }

	// How much horizontal speed survives each tick on this block, before air drag: 0.6 for most blocks.
	float GetSlipperiness(FMcStateId State) const { return States[State].Slipperiness; }
	// Multiplies the speed of whatever moves on (or in) the block: 0.4 on soul sand and honey.
	float GetSpeedFactor(FMcStateId State) const { return States[State].SpeedFactor; }
	// Multiplies jump strength: 0.5 on honey.
	float GetJumpFactor(FMcStateId State) const { return States[State].JumpFactor; }

private:
	struct FState
	{
		int32 FirstBox = 0;
		int32 NumBoxes = 0;
		float Slipperiness = 0.6f;
		float SpeedFactor = 1.0f;
		float JumpFactor = 1.0f;
	};

	TArray<FState> States;  // indexed by state id
	TArray<FBox3f> Boxes;
};
