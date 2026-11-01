// Primed TNT, measured from the reference client (scripts/tnt.txt, tnt2.txt, tnt3.txt; see PLAN.md): a 0.98 block that
// falls under gravity 0.04 (added, then the move, then drag 0.98; on the ground 0.7 on the sides and -0.5 up and down
// on top of that), burns a fuse of 80 ticks and blows up with power 4, 0.06 above its feet. One lit from a block starts
// at the block's centre moving 0.02 sideways in a random direction and 0.2 up. One a blast sets off starts with a
// shorter fuse (10 to 28 ticks in 47 samples).

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class FMcBlockWorld;

class MCENTITY_API FMcPrimedTnt final : public FMcEntity
{
public:
	FMcPrimedTnt();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	// Not kept in a chunk's save.
	virtual bool IsPersistent() const override { return false; }

	int32 Fuse = FullFuse;

	static constexpr int32 FullFuse = 80;
	static constexpr float Power = 4.0f;
	static constexpr double Gravity = 0.04;
	static constexpr double Drag = 0.98;
	static constexpr double GroundFriction = 0.7;

	// The fuse of one a blast lit, from a roll in [0, 1): 10 to 28, evenly. Measured in the reference client with pairs
	// of TNT, one lit by a redstone block and its neighbour by that blast (scripts tnt5.txt, `log ents tnt`): 47 chained
	// ones ran 10 to 28 ticks, spread flat (the one lit by redstone, fuse 80, shows 79; a chained one shows its fuse).
	// The ends are the sample's; with 47 over 19 values an end may lie one value further out.
	static int32 GetChainFuse(float Roll);

	// Whether a block of this name is TNT.
	static bool IsTnt(const FName& Block) { return Block == FName(TEXT("minecraft:tnt")); }

	// Lights the TNT block at Cell: the block goes (to air) and a primed one is made where it stood, with the push
	// a lit block gives it. For flint and steel, fire, blasts and redstone alike (the redstone side calls this).
	// Returns the entity, or null if the block is not TNT.
	static FMcPrimedTnt* Ignite(FMcEntityManager& Manager, const FIntVector& Cell, int32 InFuse = FullFuse);
};
