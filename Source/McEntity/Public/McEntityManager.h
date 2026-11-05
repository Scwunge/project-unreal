// The server's entities, ticked at 20 a second alongside the block simulation. They are plain C++ objects, not
// actors: the world they live in is the block world, and what players see of them is sent by McNet and drawn by
// McRender. Game thread only. Numbers are from minecraft.wiki ("Item (entity)", "Entity").

#pragma once

#include "CoreMinimal.h"
#include "McBlockEntity.h"
#include "McExplosion.h"
#include "McPhysics.h"
#include "McSound.h"

class FArchive;
class FMcBlockWorld;
class FMcEntityManager;
class FMcMob;

class MCENTITY_API FMcEntity
{
public:
	virtual ~FMcEntity() = default;

	// The registry name of the kind of entity, e.g. "minecraft:item".
	virtual FName GetType() const = 0;
	virtual void Tick(FMcEntityManager& Manager) {}
	// True for mobs and the like, which have health and can be hit.
	virtual bool IsLiving() const { return false; }
	// Minecarts and boats, which a player can ride (see McVehicle.h).
	virtual bool IsVehicle() const { return false; }
	// What a player's blow can land on: the living and the vehicles.
	virtual bool IsAttackable() const { return IsLiving(); }
	// A player hit something attackable that isn't a mob (a vehicle): Damage is what the held item does. False if it did nothing.
	virtual bool OnAttacked(float Damage, bool bCreative) { return false; }
	// False for what isn't worth keeping in a chunk's save (an arrow in flight).
	virtual bool IsPersistent() const { return true; }

	// Reads or writes what a save keeps of the entity, by whether the archive is loading: where it is and how it
	// moves, its age, and what its kind adds (a stack, a health). Not the id (a loaded entity gets a new one) or the
	// kind, which the saver writes first so it knows what to make. False if a loading archive is malformed.
	virtual bool Serialize(FArchive& Ar);

	int32 Id = 0;
	// The manager it was added to (null before), for what an entity does outside its own tick: being hurt makes a sound.
	FMcEntityManager* Owner = nullptr;
	FMcBody Body;
	float Yaw = 0.0f;
	float Pitch = 0.0f;
	int32 Age = 0;
	// Ticks before a portal can take it again (McTravel; not saved).
	int32 PortalCooldown = 0;
	// Tied by a lead (McLeash.h): to a knot entity by id, or (id 0) to the player last seen near LeashPlayerPos. Not saved.
	bool bLeashed = false;
	int32 LeashKnot = 0;
	FVector LeashPlayerPos = FVector::ZeroVector;
	// Saddle, tamed state and rider of a mount (McMount.h); null for anything that isn't one. Not saved yet.
	TSharedPtr<struct FMcMountState> Mount;
	// Set to have the manager drop it at the end of the tick.
	bool bRemoved = false;
};

// A stack lying on the ground.
class MCENTITY_API FMcItemEntity final : public FMcEntity
{
public:
	FMcItemEntity();
	// The type name, made once: comparing against a string literal looks the name up each time.
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool Serialize(FArchive& Ar) override;

	FMcItemStack Stack;
	// Ticks before a player can pick it up: 10 for a dropped item, 40 for a thrown one.
	int32 PickupDelay = 10;

	// What a blast has to do to destroy it.
	static constexpr float Health = 5.0f;
	static constexpr int32 LifeTicks = 6000;
	static constexpr int32 DefaultStackSize = 64;
	// A resting stack looks for one to merge with this often (staggered by id), as the search scans every entity.
	static constexpr int32 MergeIntervalTicks = 10;
};

// A sand, gravel or other block in the air. The block simulation moves it (FMcSimulation::GetFallingBlocks) and whoever
// owns both mirrors it here so clients are sent and draw it like any other entity. Not kept in a save: the simulation
// owns the falling block, and it lands (or is lost) when the world closes.
class MCENTITY_API FMcFallingBlockEntity final : public FMcEntity
{
public:
	FMcFallingBlockEntity();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual bool IsPersistent() const override { return false; }

	// The block's registry name, e.g. "minecraft:sand": its picture is the block's.
	FName Block;
};

class MCENTITY_API FMcEntityManager
{
public:
	explicit FMcEntityManager(FMcBlockWorld& InWorld) : World(InWorld) {}
	UE_NONCOPYABLE(FMcEntityManager);

	// Takes ownership and gives the entity its id.
	FMcEntity& Add(TUniquePtr<FMcEntity> Entity);
	// Drops a stack at Pos (block coordinates) with the small random push a broken block gives it.
	FMcItemEntity& DropItem(const FVector& Pos, const FMcItemStack& Stack, int32 PickupDelay = 10);

	void Tick();
	FMcEntity* Find(int32 Id);
	// The entities whose boxes touch Box.
	void FindInBox(const FBox& Box, TArray<FMcEntity*>& Out);
	// A player whose hitbox is PlayerBox takes the items within a block of it (half a block up and down),
	// returning what it picked up. Items still in their pickup delay stay.
	TArray<FMcItemStack> CollectItems(const FBox& PlayerBox);

	const TArray<TUniquePtr<FMcEntity>>& GetEntities() const { return Entities; }
	FMcBlockWorld& GetWorld() { return World; }
	FRandomStream& GetRandom() { return Random; }
	const FMcBlockWorld& GetWorld() const { return World; }

	// Whether the sun is up, for mobs that burn in it; set by the server each tick.
	bool bDaylight = false;
	// Whether it rains (where the sky is open); set by the server each tick.
	bool bRaining = false;
	// 0 peaceful to 3 hard; what some mob effects scale with.
	int32 Difficulty = 2;
	// The tick of the day, 0 at sunrise to 23999; set by the server each tick. Villagers keep their hours by it.
	int32 TimeOfDay = 0;
	// A bell rung for an alarm (a raid): every villager that hears it goes to Pos until it has run out.
	void SoundAlarm(const FVector& Pos, int32 Ticks)
	{
		AlarmPos = Pos;
		AlarmTicks = Ticks;
	}
	bool IsAlarmSounding() const { return AlarmTicks > 0; }
	const FVector& GetAlarmPos() const { return AlarmPos; }
	// Players (block position of their feet) for mobs to chase and bite; set by the server before each tick.
	TArray<FVector> PlayerPositions;
	// The mob griefing game rule (endermen take blocks, villagers..): on unless the host says otherwise.
	bool bMobGriefing = true;
	// Where each player looks (unit vectors from their eyes, parallel to PlayerPositions); empty where the host doesn't know.
	TArray<FVector> PlayerLooks;
	// Whether nothing solid is over the block, so rain and sun reach it.
	bool IsSkyExposed(const FIntVector& Pos) const;

	// How many of an item fit in one stack; set by whatever knows the item registry (default 64).
	TFunction<int32(FName)> StackSizeOf;

	// ---- Light, for deciding where hostile mobs may spawn and when a spider stays peaceful ----
	// There is no light stored on the server, so these are estimates: sky light is full where the sky is open (less
	// the night's 11) and falls by one a block from the nearest opening in the four directions; block light is the
	// brightest light source within 14 blocks minus its distance, ignoring walls (so a torch behind a wall still
	// counts: it can only make a spot look brighter than it is).
	int32 EstimateSkyLight(const FIntVector& Cell) const;
	int32 EstimateBlockLight(const FIntVector& Cell) const;
	// The larger of the two: the light level a spider looks at.
	int32 EstimateLightLevel(const FIntVector& Cell) const;
	// The rule for hostile mobs: block light 0 and sky light 7 or less.
	bool IsDarkEnoughToSpawn(const FIntVector& Cell) const;

	// ---- Explosions ----
	// Blows up at Center (block coordinates): hurts the mobs and players near by how close and how open to the blast
	// they are, pushes the mobs and lying items (destroying the items it hurts by 5 or more), then destroys the blocks
	// the rays reach (calling OnBlocksDestroyed with them, after the items were dealt with, so the drops they leave
	// are safe).
	void Explode(const FVector& Center, float Power);
	// True while an explosion is hurting the mobs it reaches (an end crystal hit by one vanishes instead of going off).
	bool bInBlast = false;
	// Whether the dragon has been killed before in this world (the second kill gives 500 points, not 12000, and no egg).
	bool bDragonKilledBefore = false;
	// Called with the position of a player (the block position of the feet, as in PlayerPositions) and the damage to
	// give: for bites, arrows and blasts. Left unset, the hit is dropped.
	TFunction<void(const FVector&, float)> OnPlayerHurt;
	// Called instead of OnPlayerHurt for a player a blast reaches: the feet position, the damage, the push to add to
	// their velocity (blocks a tick) and the centre of the blast. Left unset, the damage goes to OnPlayerHurt.
	TFunction<void(const FVector&, float, const FVector&, const FVector&)> OnPlayerBlasted;
	// Called with the position of a player, an effect's name ("poison"), its level less one and its ticks, for a splash
	// potion that reached them. Left unset, the effect is dropped.
	TFunction<void(const FVector&, FName, int32, int32)> OnPlayerEffect;
	// Called with the position of a player (as in PlayerPositions): whether they wear any gold armour. Piglins leave
	// such a player alone. Left unset, nobody does.
	TFunction<bool(const FVector&)> PlayerWearsGold;
	// Called with a villager a mob has just killed, so a zombie's kill can turn it into a zombie villager.
	TFunction<void(FMcMob&)> OnVillagerSlain;
	// Called with where a village wants a golem (villagers that are scared, or crowded together); the host puts one beside them.
	TFunction<void(const FVector&)> OnGolemWanted;
	// Called to change a block (a villager shutting a door), so the owner of the blocks can send the change on.
	TFunction<void(const FIntVector&, FMcStateId)> OnSetBlock;
	// Called to put a mob of a kind at a position (an evoker's fangs and vexes); returns it, or null if it can't be.
	TFunction<FMcMob*(FName, const FVector&)> OnSpawnMob;
	// Called with where two villagers made a baby and the one that called (for its look); the host puts the baby there.
	TFunction<void(const FVector&, FMcMob&)> OnVillagerBred;
	// Called after an explosion with the blocks it destroyed and its power, so whoever knows the item registry can
	// drop what they leave.
	TFunction<void(const TArray<FMcDestroyedBlock>&, float)> OnBlocksDestroyed;
	void HurtPlayer(const FVector& Position, float Damage) const
	{
		if (OnPlayerHurt)
		{
			OnPlayerHurt(Position, Damage);
		}
	}

	// ---- Sounds ----
	// Called with every sound the entities make (a mob's voice and steps, a blast, an arrow in a wall), for whoever sends
	// them to the players. Left unset, they are dropped.
	TFunction<void(const FMcSoundEvent&)> OnSound;
	void PlaySound(const FMcSoundEvent& Event) const
	{
		if (OnSound && !Event.Name.IsNone())
		{
			OnSound(Event);
		}
	}
	void PlaySound(FName Name, const FVector& Position, float Volume = 1.0f, float Pitch = 1.0f) const
	{
		PlaySound(FMcSoundEvent{ Name, Position, Volume, Pitch });
	}

	// ---- Particles ----
	// Called with each effect the entities make that the players see (a blast's cloud), for whoever sends it to them; unset, dropped.
	// Name as /particle writes it, Position in blocks, Delta the spread, Speed, Count.
	TFunction<void(FName, const FVector&, const FVector&, float, int32)> OnParticle;
	void SpawnParticle(FName Name, const FVector& Position, const FVector& Delta = FVector::ZeroVector, float Speed = 0.0f, int32 Count = 1) const
	{
		if (OnParticle)
		{
			OnParticle(Name, Position, Delta, Speed, Count);
		}
	}

	// ---- Detector rails ----
	// Called when a minecart is on a detector rail (true, every tick) or has left it (false), for whoever runs the
	// redstone. Left unset, rails don't hear.
	TFunction<void(const FIntVector&, bool)> OnDetector;
	// Detector rails now powered (the tick of their next look) and those touched this tick.
	TMap<FIntVector, int32> Detectors;
	TSet<FIntVector> Touched;
	int32 TickNow = 0;
	void SetDetector(const FIntVector& Pos, bool bOccupied) const
	{
		if (OnDetector)
		{
			OnDetector(Pos, bOccupied);
		}
	}

	// ---- Target blocks ----
	// Called when an arrow sticks in a block: the cell, where it hit (block coordinates) and which way it was flying, for
	// whoever runs the redstone. Left unset, targets don't hear.
	TFunction<void(const FIntVector&, const FVector&, const FVector&)> OnArrowHit;

	// A minecart touching a detector rail's sensor (the rail's middle three fifths, up to half a block) powers it at once and
	// again every tick it touches; once on, the rail looks again 20 ticks on and every 20 after, and goes off at the first look
	// with no cart on it (vehicle_log12.txt: on at tick 557000 with the cart's box 0.01 over the sensor, still on at 557039
	// with the cart's box 0.5 past it, off at 557040).
	void TouchDetector(const FIntVector& Pos)
	{
		Touched.Add(Pos);
		if (!Detectors.Contains(Pos))
		{
			Detectors.Add(Pos, TickNow + 20);
			SetDetector(Pos, true);
		}
	}

private:
	FMcBlockWorld& World;
	TArray<TUniquePtr<FMcEntity>> Entities;
	int32 NextId = 1;
	FRandomStream Random{ 7 };
	int32 AlarmTicks = 0;
	FVector AlarmPos = FVector::ZeroVector;
};
