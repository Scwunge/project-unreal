// An experience orb (minecraft.wiki: "Experience Orb"): lies where it was made, drifts to a player within 8 blocks
// and is worth Value points to whoever takes it (McSurvival::CollectXp). It is not saved with a chunk.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class MCENTITY_API FMcXpOrb final : public FMcEntity
{
public:
	FMcXpOrb();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsPersistent() const override { return false; }
	virtual bool Serialize(FArchive& Ar) override;

	int32 Value = 1;

	static constexpr int32 LifeTicks = 6000;
	static constexpr double AttractRange = 8.0;

	// The orb sizes the game splits a reward into, largest first.
	static void Split(int32 Points, TArray<int32>& Out);
	// Makes orbs worth Points in all at Pos (block coordinates), each with a small random push.
	static void Spawn(FMcEntityManager& Manager, const FVector& Pos, int32 Points, FRandomStream& Random);
};
