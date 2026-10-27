// The server's entities, ticked at 20 a second alongside the block simulation. They are plain C++ objects, not
// actors: the world they live in is the block world, and what players see of them is sent by McNet and drawn by
// McRender. Game thread only. Numbers are from minecraft.wiki ("Item (entity)", "Entity").

#pragma once

#include "CoreMinimal.h"
#include "McBlockEntity.h"
#include "McPhysics.h"

class FMcBlockWorld;
class FMcEntityManager;

class MCENTITY_API FMcEntity
{
public:
	virtual ~FMcEntity() = default;

	// The registry name of the kind of entity, e.g. "minecraft:item".
	virtual FName GetType() const = 0;
	virtual void Tick(FMcEntityManager& Manager) {}
	// True for mobs and the like, which have health and can be hit.
	virtual bool IsLiving() const { return false; }

	int32 Id = 0;
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

	FMcItemStack Stack;
	// Ticks before a player can pick it up: 10 for a dropped item, 40 for a thrown one.
	int32 PickupDelay = 10;

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
	// Players (block position of their feet) for mobs to chase and bite; set by the server before each tick.
	TArray<FVector> PlayerPositions;
	// Whether nothing solid is over the block, so rain and sun reach it.
	bool IsSkyExposed(const FIntVector& Pos) const;

	// How many of an item fit in one stack; set by whatever knows the item registry (default 64).
	TFunction<int32(FName)> StackSizeOf;

private:
	FMcBlockWorld& World;
	TArray<TUniquePtr<FMcEntity>> Entities;
	int32 NextId = 1;
	FRandomStream Random{ 7 };
};
