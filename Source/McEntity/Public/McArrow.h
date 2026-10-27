// An arrow in flight: a skeleton's shot. It flies under gravity (0.05 a tick) and drag (0.99), hurts the first player
// it reaches and sticks in the first block it hits, where it stays for a minute. Players are the ones in the
// manager's PlayerPositions; the damage goes to FMcEntityManager::HurtPlayer.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class MCENTITY_API FMcArrowEntity final : public FMcEntity
{
public:
	FMcArrowEntity();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	// A shot is over in seconds: saving it isn't worth the bytes.
	virtual bool IsPersistent() const override { return false; }

	// What it does to a player it hits.
	float Damage = 4.0f;
	bool bStuck = false;
	int32 StuckTicks = 0;

	// A flying arrow that hasn't hit anything after a minute is gone; one in a block stays a minute too.
	static constexpr int32 LifeTicks = 1200;
	static constexpr double Gravity = 0.05;
	static constexpr double Drag = 0.99;
};
