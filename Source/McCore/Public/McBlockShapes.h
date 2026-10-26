// What each block state is like to bump into and walk on: collision boxes, slipperiness and speed factors.
// The server needs these too, so they come from block names and states (facts from minecraft.wiki), not from
// the client's models.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

enum class EMcFluid : uint8
{
	None,
	Water,
	Lava,
};

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

	// Ladders, vines and scaffolding: the #minecraft:climbable tag from cache/ (a built-in list if it's missing).
	bool IsClimbable(FMcStateId State) const { return States[State].bClimbable; }
	// Cobwebs tangle whatever is inside them.
	bool SlowsLikeCobweb(FMcStateId State) const { return States[State].Stuck == EStuck::Cobweb; }
	// What an entity inside the block multiplies its motion by each tick (x, y, z; Minecraft axes): cobwebs
	// (0.25, 0.05, 0.25), sweet berry bushes (0.8, 0.75, 0.8), powder snow (0.9, 1.5, 0.9), else all 1.
	FVector3f GetStuckMultiplier(FMcStateId State) const;

	// The fluid a state holds: water and lava blocks, the blocks that always hold water (kelp, seagrass, bubble
	// columns) and anything waterlogged. The level is the block's own "level" (0 is a source, 1..7 flowing,
	// 8 and up falling); 0 for the water inside other blocks.
	EMcFluid GetFluid(FMcStateId State) const { return States[State].Fluid; }
	uint8 GetFluidLevel(FMcStateId State) const { return States[State].FluidLevel; }

private:
	enum class EStuck : uint8
	{
		None,
		Cobweb,
		SweetBerryBush,
		PowderSnow,
	};

	struct FState
	{
		int32 FirstBox = 0;
		int32 NumBoxes = 0;
		float Slipperiness = 0.6f;
		float SpeedFactor = 1.0f;
		float JumpFactor = 1.0f;
		EStuck Stuck = EStuck::None;
		bool bClimbable = false;
		EMcFluid Fluid = EMcFluid::None;
		uint8 FluidLevel = 0;
	};

	TArray<FState> States;  // indexed by state id
	TArray<FBox3f> Boxes;
};
