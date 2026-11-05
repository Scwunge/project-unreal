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
	// A player's shot: it hurts mobs too, by the game's rule (the base times the speed, rounded up, and a critical
	// draw adds up to half as much again), and can be picked up where it lands unless it was shot in creative.
	float PlayerBase = 0.0f;
	// The bow's Punch: knockback points, each pushing a mob hit 0.6 further along the arrow's way (ench_log6: first-tick step 0.37, 0.97, 1.57 at Punch 0, I, II).
	float PunchKnockback = 0.0f;
	bool bCritical = false;
	bool bPickup = false;
	// An effect it gives the player it hits (a stray's slowness, a bogged's poison), for Duration ticks; none when unnamed.
	FName Effect;
	int32 EffectDuration = 0;
	bool bStuck = false;
	int32 StuckTicks = 0;

	// A flying arrow that hasn't hit anything after a minute is gone; one in a block stays a minute too.
	static constexpr int32 LifeTicks = 1200;
	static constexpr double Gravity = 0.05;
	static constexpr double Drag = 0.99;

	// The pitch of the sound of an arrow hitting something for a random U in 0 to 1 (volume 1): a plain range of 1.09 to 1.33.
	// 70 hits of the reference client: min 1.0927, max 1.3300, mean 1.2003, consistent with a uniform range (KS p 0.33); the
	// shape of the distribution is not fitted.
	static float GetHitPitch(float U) { return 1.09f + 0.24f * U; }
};
