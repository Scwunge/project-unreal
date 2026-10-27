// Mobs: living entities that wander, flee, chase and bite (minecraft.wiki, the page of each mob). The numbers per
// kind are in a table; the behaviour is a handful of goals shared between kinds, run in priority order each tick
// the way the game's goal selector does: the first goal that wants to run and doesn't clash with a running one.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

struct MCENTITY_API FMcMobType
{
	FName Name;
	float Width = 0.6f;
	float Height = 1.8f;
	float MaxHealth = 20.0f;
	// The movement speed attribute; the player's is 0.1.
	float Speed = 0.1f;
	// 0 for a mob that doesn't attack.
	float AttackDamage = 0.0f;
	// Chases players within this many blocks; 0 for a mob that never does.
	float FollowRange = 0.0f;
	// Runs from whatever hurt it for a few seconds (the passive animals).
	bool bPanics = false;
	// Burns in daylight.
	bool bBurnsInSun = false;

	static const FMcMobType* Find(FName Name);
	static const TArray<FMcMobType>& GetAll();
};

class MCENTITY_API FMcMob : public FMcEntity
{
public:
	explicit FMcMob(const FMcMobType& InType);
	virtual FName GetType() const override { return Type.Name; }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsLiving() const override { return true; }

	// Hurts the mob; Source (block coordinates) pushes it away from there, as a hit does. True if it died.
	bool Damage(float Amount, const FVector* Source);

	const FMcMobType& Type;
	float Health;
	// Ticks left of the red flash and of the invulnerability after a hit (10 each, for the damage that gets through).
	int32 HurtTime = 0;
	// Counts up with every hit that gets through, so a watcher can tell a new hit from the last without having to
	// catch HurtTime at its peak (it has already counted down once by the time the tick is over).
	int32 HurtEvents = 0;
	int32 PanicTicks = 0;
	int32 AttackCooldown = 0;

	// Where the mob is walking, if anywhere.
	TOptional<FVector> Goal;
	// Called with the damage when the mob bites a player at that position.
	TFunction<void(const FVector&, float)> OnAttackPlayer;
	// Called when the mob dies, to drop its loot.
	TFunction<void(FMcMob&)> OnDeath;

private:
	FRandomStream Random;
};
