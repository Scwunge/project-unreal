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

	// One game tick. Players are given as the block position of their feet; DayTicks is the time of day.
	void Tick(const TArray<FVector>& Players, double DayTicks);

	// A chunk has joined the world: a few of them get a herd of animals.
	void OnChunkAdded(FIntPoint Chunk);

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

	FMcBlockWorld& World;
	FMcEntityManager Manager;
	int64 Seed;
	int64 Ticks = 0;
	FRandomStream Random;
	TSet<FIntPoint> Seeded;

	TWeakObjectPtr<UWorld> GameWorld;
	TSubclassOf<AActor> ProxyClass;
	TMap<int32, TWeakObjectPtr<AActor>> Proxies;
};
