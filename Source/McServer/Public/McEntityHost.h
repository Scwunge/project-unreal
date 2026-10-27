// The entities of one dimension on the server: runs them each tick, spawns animals as chunks arrive and hostile
// mobs in the dark near players, drops what mobs carry when they die, and keeps a replicated actor for each so
// players see them. Server only; game thread.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class AActor;
class UWorld;
class FMcBlockWorld;
class FMcMob;

class MCSERVER_API FMcEntityHost
{
public:
	FMcEntityHost(FMcBlockWorld& InWorld, int64 InSeed);
	~FMcEntityHost();
	UE_NONCOPYABLE(FMcEntityHost);

	// The actor class made for each entity; it must implement IMcEntityProxy. With none, entities run unseen.
	void SetProxyClass(UWorld* InGameWorld, TSubclassOf<AActor> Class);
	// Destroys the actors made for the entities and stops making more. Call it while the game world is still up
	// (the owner's EndPlay): the destructor runs during garbage collection, too late to destroy actors.
	void DestroyProxies();

	// One game tick. Players are given as the block position of their feet; DayTicks is the time of day.
	void Tick(const TArray<FVector>& Players, double DayTicks);

	// A chunk has joined the world: a few of them get a herd of animals.
	void OnChunkAdded(FIntPoint Chunk);
	// A chunk has left the world: the mobs standing in it go with it (their ground is gone and they'd only pile up,
	// frozen, each with an actor), and it can be given animals again if it comes back.
	void OnChunkRemoved(FIntPoint Chunk);

	// Called with the position of the player and the damage when a mob bites. Until players have health this is
	// left unset and the bite is only logged.
	TFunction<void(const FVector&, float)> OnPlayerHit;

	FMcEntityManager& GetManager() { return Manager; }
	int32 NumHostile() const;

	// Player attacks: the mob (if any) the ray from the eyes hits within Reach, hurt by Damage and pushed away.
	// True if something was hit.
	bool Attack(const FVector& Eye, const FVector& Direction, float Reach, float Damage);

private:
	FMcMob* SpawnMob(FName Kind, const FVector& Pos);
	void SpawnHostiles(const TArray<FVector>& Players);
	void SyncProxies();
	// The y of the feet of a mob that would stand on the ground of this column near NearY, or none.
	TOptional<int32> FindStandingY(int32 X, int32 Z, int32 NearY, int32 Range) const;
	void DropLoot(FMcMob& Mob);
	// Puts the herd (if the chunk gets one) on the ground. Done a few chunks a tick, not all inside OnChunkAdded:
	// each herd searches columns for ground, and a player joining brings hundreds of chunks at once.
	void SeedChunk(FIntPoint Chunk);

	FMcBlockWorld& World;
	FMcEntityManager Manager;
	int64 Seed;
	int64 Ticks = 0;
	FRandomStream Random;
	// The chunks that have had their turn at animals, or are queued for it; forgotten when the chunk unloads.
	TSet<FIntPoint> Seeded;
	TArray<FIntPoint> SeedQueue;
	bool bDevSpawnDone = false;

	TWeakObjectPtr<UWorld> GameWorld;
	TSubclassOf<AActor> ProxyClass;
	TMap<int32, TWeakObjectPtr<AActor>> Proxies;
};
