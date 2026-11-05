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
	// Evokers cast: fangs along the ground at their target, and vexes when none are about.
	bool bCastsSpells = false;
	// The evoker's fangs: they come up after a delay, bite what stands there once and go.
	bool bIsFangs = false;
	// The illagers that raid: pillager, vindicator, evoker, ravager and witch. A patrol or a raid has them, one a captain.
	bool bIsRaider = false;
	// Witches throw potions at their target from up to 10 blocks (and drink their own when they need to).
	bool bThrowsPotions = false;

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
	// Drowned swim at a target in water, straight at it in three dimensions, instead of paddling up like the rest.
	bool bSwims = false;
	// Zombies in water do not paddle up: they sink (logged, a steady 0.025 blocks a tick), unlike the animals.
	bool bSinks = false;

	// Mobs that go after players: what the spawner caps, and what despawns far from everyone.
	// The golem fights hostile mobs rather than being one.
	bool IsHostile() const { return (AttackDamage > 0.0f || bSwells || bCastsSpells) && !bHuntsHostiles; }

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

	// A chick: half the size, and grown up after twenty minutes (the baby's age counts up from -24000): SetYoung(YoungSpan).
	void MakeBaby();

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
	// The id of the vehicle it sits in (a boat picks up what it bumps into), or 0. A mob that sits does nothing and is
	// moved by its vehicle; it gets out when that goes. Not saved.
	int32 RidingId = 0;
	int32 PanicTicks = 0;
	int32 AttackCooldown = 0;
	// Ticks a hurt neutral mob stays after whoever hurt it (a minute).
	int32 AngerTicks = 0;
	// The entity (a wandering trader) this mob is on a lead to, 0 for none: it follows it, and is let go if that is gone.
	int32 LeashHolderId = 0;
	// A golem holding out a poppy to a child: ticks left. It stands still and faces the child meanwhile.
	int32 OfferTicks = 0;
	// A piglin that took a gold ingot: ticks left before it throws what it gives for it. Not saved.
	int32 BarterTicks = 0;
	// A dropped gold ingot it is walking to. Not saved.
	FVector BarterGoal = FVector::ZeroVector;
	bool bHasBarterGoal = false;
	// The item in its right hand (drawn on the model), None for nothing.
	FName HeldItem;
	// Fangs: ticks before they bite. A vex: ticks of life left (it starts to waste away when they run out), 0 for no limit.
	int32 SpellDelay = 0;
	int32 SpellCooldown = 0;
	int32 LifeTicks = 0;
	// The one leading a patrol or a wave of a raid: it carries the ominous banner and drops an ominous bottle. Not saved.
	bool bCaptain = false;
	// The Age at which a player last hurt it, and whether that was a shot: its loot asks whether a player killed it
	// (the game counts the last five seconds). Not saved.
	int32 PlayerHitAge = -1000000;
	bool bPlayerHitWasShot = false;
	// Ticks left of glowing (a bell rung near a raider outlines it), and the raid it belongs to, 0 for none.
	int32 GlowTicks = 0;
	int32 RaidId = 0;
	// Never despawns for being far from everyone (a swamp hut's witch).
	bool bPersistent = false;
	// Wolves: tamed by a player (the name is who), told to sit (it stays put, and stands again when told), and the dye of the collar (0..15, red 14).
	bool bTamed = false;
	bool bSitting = false;
	int32 CollarColor = 14;
	FString OwnerName;
	// Sets the wolf's ArmPose, which is how its look reaches the screen: bit 1 sitting, 2 angry, 4 tame, the collar from bit 8 up.
	void UpdateWolfPose();
	// What the arms are doing, for how it is drawn: 0 idle, 1 armed or attacking, 2 charging a crossbow, 3 casting, 4 drinking.
	int32 ArmPose = 0;
	// A witch drinking: ticks left of the drink and what is in it ("healing"), and ticks until it may drink again.
	int32 DrinkTicks = 0;
	int32 DrinkCooldown = 0;
	FName DrinkEffect;
	// A witch's speed potion: ticks of the swift legs it gives.
	int32 SwiftTicks = 0;
	// Development: stands where it was put, doing nothing (for a picture).
	bool bFrozen = false;
	// The game's NoAI: it stands where it is and does nothing, but is saved and can be hurt (an imported world's).
	bool bNoAI = false;
	// A sheep's wool dye (0..15, the game's order), -1 for the plain one.
	int32 DyeColor = -1;
	// The name it was given (a name tag), empty for none. Kept and saved; not drawn yet.
	FString CustomName;
	// What it wears: head, chest, legs, feet (item names), None for nothing. Kept and saved; not drawn yet.
	FName Armor[4];
	// What the host keeps after the kind's own bytes: the coat, the youth, then the flags, dye, name and gear. A save without
	// the later ones reads as before; SaveTrailer/LoadTrailer are the one place that says what order they come in.
	void SaveTrailer(FArchive& Ar) const;
	void LoadTrailer(FArchive& Ar);
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
	// Throws a potion at the target: harming or poison at a player, slowness from far and weakness from close.
	void ThrowPotion(FMcEntityManager& Manager, const FVector& TargetFeet);
	// A witch's own potions: healing when hurt, water breathing in water, swiftness to close in.
	void TickWitch(FMcEntityManager& Manager, bool bChasing, double TargetDistance);
	void TickBarter(FMcEntityManager& Manager);
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
