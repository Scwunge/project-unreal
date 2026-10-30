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
	const FMcBlockWorld& GetWorld() const { return World; }

	// Whether the sun is up, for mobs that burn in it; set by the server each tick.
	bool bDaylight = false;
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
	// Called with the position of a player (the block position of the feet, as in PlayerPositions) and the damage to
	// give: for bites, arrows and blasts. Left unset, the hit is dropped.
	TFunction<void(const FVector&, float)> OnPlayerHurt;
	// Called with a villager a mob has just killed, so a zombie's kill can turn it into a zombie villager.
	TFunction<void(FMcMob&)> OnVillagerSlain;
	// Called with where a village wants a golem (villagers that are scared, or crowded together); the host puts one beside them.
	TFunction<void(const FVector&)> OnGolemWanted;
	// Called to change a block (a villager shutting a door), so the owner of the blocks can send the change on.
	TFunction<void(const FIntVector&, FMcStateId)> OnSetBlock;
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

	FRandomStream& GetRandom() { return Random; }

private:
	FMcBlockWorld& World;
	TArray<TUniquePtr<FMcEntity>> Entities;
	int32 NextId = 1;
	FRandomStream Random{ 7 };
	int32 AlarmTicks = 0;
	FVector AlarmPos = FVector::ZeroVector;
};
