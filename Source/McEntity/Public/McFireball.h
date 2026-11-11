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
	static const FName& SkullType();
	static const FName& DragonType();
	static const FName& WindType();
	virtual FName GetType() const override { return bWind ? WindType() : bDragon ? DragonType() : bSkull ? SkullType() : (bSmall ? SmallType() : GhastType()); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsPersistent() const override { return false; }

	// Fires along Direction (unit) from where it is.
	void Launch(const FVector& Direction, bool bInSmall);
	// A wither's skull (wither1.txt, normal difficulty, 12 skulls identical): speed 0.1, 0.146, 0.18, 0.204 ... settling at 0.27, which is
	// speed = (speed + 0.1) * 0.73 along the aim, no gravity. Blast power 1 is the game's value and NOT measured (the player lost a net 2 hp
	// beyond a steady drain in the 20 ticks of each hit, which does not settle it).
	void LaunchSkull(const FVector& Direction);
	// The ender dragon's (dragon_phase1.txt: the one tick logged had speed 0.0998 aimed straight at the player, like a ghast's first tick; the rest of
	// its flight was not logged, so it flies as a ghast's does, UNVERIFIED). It leaves a damage cloud where it lands (the dragon keeps the clouds).
	void LaunchDragon(const FVector& Direction);
	// A breeze's wind charge (breeze.txt, 2026-11-05, 306 samples of one run): a steady 0.70 blocks a tick along the aim, no gravity and no
	// drag; a hit took 1 hp from a zoglin (the player was immune, so its damage is the same figure assumed). The burst that shoves entities
	// and flips levers is NOT done.
	void LaunchWind(const FVector& Direction);
	static constexpr double WindSpeed = 0.70;
	static constexpr float WindDamage = 1.0f;

	static constexpr double Acceleration = 0.1;
	static constexpr double Inertia = 0.95;
	static constexpr float SmallDamage = 5.0f;
	static constexpr float ExplosionPower = 1.0f;
	static constexpr int32 LifeTicks = 600;

private:
	void Hit(FMcEntityManager& Manager, const FVector* PlayerFeet);

	bool bSmall = false;
	bool bSkull = false;
	bool bDragon = false;
	bool bWind = false;
	FVector Aim = FVector::ForwardVector;
};
