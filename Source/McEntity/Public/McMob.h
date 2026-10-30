// Mobs: living entities that wander, flee, chase and bite (minecraft.wiki, the page of each mob). The numbers per
// kind are in a table; the behaviour is a handful of goals shared between kinds, run in priority order each tick
// the way the game's goal selector does: the first goal that wants to run and doesn't clash with a running one.
// Walking goes along a path found by A* over the blocks (McPathfinding), not in a straight line.

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
	// Zombies chase and bite villagers as well as players.
	bool bHuntsVillagers = false;
	// Iron golems go for the hostile mobs near them (not creepers), and leave players be unless hurt.
	bool bHuntsHostiles = false;

	// Creepers walk up to a player, swell for 1.5 seconds and explode.
	bool bSwells = false;
	// Spiders climb whatever wall they walk into.
	bool bClimbsWalls = false;
	// Only fights after being hurt (endermen; the game's other trigger, a player's look, isn't there yet).
	bool bNeutral = false;
	// Spiders: only fights after being hurt unless the light around it is 11 or less.
	bool bNeutralInLight = false;
	// Skeletons shoot arrows from up to 15 blocks and don't come closer than 10.
	bool bShoots = false;
	// Endermen teleport away when hurt.
	bool bTeleports = false;
	// Slimes get about by hopping, come in sizes 1, 2 and 4, and split into smaller ones when they die.
	bool bHops = false;
	// Bees fly: no gravity, and they wander at different heights.
	bool bFlies = false;

	// Mobs that go after players: what the spawner caps, and what despawns far from everyone.
	// The golem fights hostile mobs rather than being one.
	bool IsHostile() const { return (AttackDamage > 0.0f || bSwells) && !bHuntsHostiles; }

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
	virtual bool Serialize(FArchive& Ar) override;

	// Hurts the mob; Source (block coordinates) pushes it away from there, as a hit does. True if it died.
	bool Damage(float Amount, const FVector* Source);
	// Whether this mob goes for that entity as its prey (zombies: villagers; golems: hostile mobs but the creeper).
	bool IsPrey(const FMcEntity& Other) const;

	// Slimes only: 1, 2 or 4. Sets the size of the body and (with bHeal) the health, size squared.
	void SetSize(int32 InSize, bool bHeal = true);
	// What a bite does: the type's, except for slimes, where the smallest does none and the others their size.
	float GetAttackDamage() const;

	const FMcMobType& Type;
	float Health;
	int32 Size = 1;
	// Ticks left of the red flash and of the invulnerability after a hit (10 each, for the damage that gets through).
	int32 HurtTime = 0;
	// Counts up with every hit that gets through, so a watcher can tell a new hit from the last without having to
	// catch HurtTime at its peak (it has already counted down once by the time the tick is over).
	int32 HurtEvents = 0;
	int32 PanicTicks = 0;
	int32 AttackCooldown = 0;
	// Ticks a hurt neutral mob stays after whoever hurt it (a minute).
	int32 AngerTicks = 0;
	// The entity (a wandering trader) this mob is on a lead to, 0 for none: it follows it, and is let go if that is gone.
	int32 LeashHolderId = 0;
	// Development: stands where it was put, doing nothing (for a picture).
	bool bFrozen = false;
	// Which of its kind's looks it has (McMobLook), chosen at birth and saved; -1 for none.
	int32 LookSeed = -1;
	// A young animal: half the size, and grown up when the ticks run out. 0 for a grown one. (Villagers keep their own.)
	int32 YoungTicks = 0;
	static constexpr int32 YoungSpan = 24000;
	bool IsYoung() const { return YoungTicks > 0; }
	void SetYoung(int32 Ticks);
	// The animals that breed, and so have young.
	static bool CanBeYoung(FName Kind);

	// Creepers: ticks the fuse has burned (it explodes at FuseTicks), and whether it is burning now.
	int32 SwellTicks = 0;
	bool bSwelling = false;
	// Skeletons: ticks to the next shot.
	int32 ShootCooldown = 0;
	// Slimes: ticks on the ground to the next hop.
	int32 HopDelay = 0;

	// Walking speed in blocks per second per point of the speed attribute: the wiki gives a zombie (0.23) about 2.3
	// and a wandering pig (0.25) about 2.4, where the player's physics turn the player's 0.1 into 4.3. The mob's
	// movement input is scaled down to match; the physics are the player's.
	static constexpr double BlocksPerSecondPerSpeed = 10.0;
	static constexpr double PlayerBlocksPerSecondPerSpeed = 43.17;
	static double GetInputSpeed(const FMcMobType& InType)
	{
		return InType.Speed * BlocksPerSecondPerSpeed / PlayerBlocksPerSecondPerSpeed;
	}

	static constexpr int32 FuseTicks = 30;
	static constexpr float ExplosionPower = 3.0f;
	static constexpr double ShootRange = 15.0;
	// A skeleton stops coming closer inside this.
	static constexpr double ShootStopRange = 10.0;
	static constexpr int32 ShootIntervalTicks = 60;
	static constexpr double ArrowSpeed = 1.6;
	// The error of a Normal difficulty skeleton's aim.
	static constexpr double ArrowInaccuracy = 6.0;

	// Where the mob is walking, if anywhere.
	TOptional<FVector> Goal;
	// Set by a mob's routine while it works or sleeps: it stays where it is instead of wandering off.
	bool bStayPut = false;
	// Called with the damage when the mob bites a player at that position.
	TFunction<void(const FVector&, float)> OnAttackPlayer;
	// Called when the mob dies, to drop its loot.
	TFunction<void(FMcMob&)> OnDeath;

	// The path being followed (the blocks to walk through, from the next one on) and where it was found to lead.
	const TArray<FIntVector>& GetPath() const { return Path; }
	int32 GetPathIndex() const { return PathIndex; }

private:
	// The point to head for on the way to Dest: the next block of the path, found afresh when there is none, the goal
	// has moved or the mob is stuck; Dest itself when it is near or there is no path.
	FVector NextWaypoint(FMcEntityManager& Manager, const FVector& Dest);
	void Shoot(FMcEntityManager& Manager, const FVector& TargetFeet);
	// The mob's own voice: ambient, hurt or death, with the pitch it varies by (and a slime's size-scaled volume).
	void PlayVoice(McSound::EMobSound Kind);
	// A slime's jump or squish: volume 0.4 a size, low for the big ones and high for the small.
	void PlaySlimeSound(FName Event);
	// A step every 1.67 blocks walked on the ground.
	void UpdateSteps(FMcEntityManager& Manager);
	// Moves to a random spot with ground up to 32 blocks away, if one turns up in 16 tries.
	bool TryTeleport(FMcEntityManager& Manager);

	FRandomStream Random;
	// Pitch and timing of the sounds, apart from the stream that drives the mob so sounds don't change where it goes.
	FRandomStream VoiceRandom;
	int32 AmbientTime = 0;
	FVector StepLast = FVector::ZeroVector;
	double StepDistance = 0.0;
	double NextStep = 1.0;
	TArray<FIntVector> Path;
	int32 PathIndex = 0;
	FIntVector PathGoal = FIntVector::ZeroValue;
	int32 RepathCooldown = 0;
	int32 StuckTicks = 0;
	FVector LastPosition = FVector::ZeroVector;
	bool bBrightHere = false;
	bool bTeleportPending = false;
};
