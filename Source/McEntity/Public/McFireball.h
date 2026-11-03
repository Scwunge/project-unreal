// A fireball in flight: a ghast's (explodes, power 1) or a blaze's small one (5 damage to a player). Measured on the reference
// (nether1.txt, nether2.txt): it starts at 0.1 a tick along its aim and each tick adds 0.1 along the aim then keeps 0.95 of the speed
// (0.1, 0.19, 0.2755, 0.357, ...), no gravity; the ghast's carries ExplosionPower 1 and acceleration_power 0.1, the blaze's hit took
// 5 hp. Its box (1.0 and 0.3125 across) and the fire it leaves were not measured.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class MCENTITY_API FMcFireball final : public FMcEntity
{
public:
	FMcFireball();
	static const FName& GhastType();
	static const FName& SmallType();
	virtual FName GetType() const override { return bSmall ? SmallType() : GhastType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsPersistent() const override { return false; }

	// Fires along Direction (unit) from where it is.
	void Launch(const FVector& Direction, bool bInSmall);

	static constexpr double Acceleration = 0.1;
	static constexpr double Inertia = 0.95;
	static constexpr float SmallDamage = 5.0f;
	static constexpr float ExplosionPower = 1.0f;
	static constexpr int32 LifeTicks = 600;

private:
	void Hit(FMcEntityManager& Manager, const FVector* PlayerFeet);

	bool bSmall = false;
	FVector Aim = FVector::ForwardVector;
};
