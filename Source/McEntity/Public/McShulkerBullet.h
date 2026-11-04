// A shulker's bullet. Measured on the reference (atk1.txt, 4 bullets, one run): it appears on top of the shulker and homes on the player;
// its speed along the flight follows one table (0.031, 0.056, 0.077, 0.095, ... 0.14 at tick 8, 0.20 at 17, 0.29 at 31, 0.36 at 40) and
// drops back to about 0.13 (the table's tick 7) each time it changes course, every 24 to 40 ticks (n=4, so that is a guess at the rule).
// Bullets lasted 42 to 88 ticks. NOT measured: the damage (the log only sampled health every 100 ticks; 4 is the game's value) and the
// levitation it gives.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class MCENTITY_API FMcShulkerBullet final : public FMcEntity
{
public:
	FMcShulkerBullet();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsPersistent() const override { return false; }

	static constexpr float Damage = 4.0f;
	static constexpr int32 LifeTicks = 100;
	static constexpr int32 ResetTick = 7;

	// The speed the bullet has at this tick of a leg.
	static float SpeedAt(int32 LegTick);

private:
	int32 LegTick = 0;
	int32 LegLength = 30;
};
