// The entities of one dimension on the server: runs them each tick, spawns animals as chunks arrive and hostile
// mobs in the dark near players, drops what mobs carry when they die, and keeps a replicated actor for each so
// players see them. Server only; game thread.

#pragma once

#include "CoreMinimal.h"
#include "McBiomeRegistry.h"
#include "McEntityManager.h"
#include "McGameRules.h"

class AActor;
class UWorld;
class FMcBlockWorld;
class FMcMob;
class FMcVillager;

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

	// The mobs and items standing in a chunk, as bytes for its save (M9); empty if there are none. Take it before
	// the chunk leaves the world, since that removes its mobs.
	TArray<uint8> CaptureChunk(FIntPoint Chunk);
	// Puts back what CaptureChunk wrote, with new ids; returns how many came back. Entities of kinds this version
	// doesn't know, or whose bytes are damaged, are dropped.
	int32 RestoreChunk(TConstArrayView<uint8> Bytes);
	// A chunk that came from a save has had its turn at animals: don't give it a herd when it joins the world.
	// Call before adding the chunk.
	void MarkSeeded(FIntPoint Chunk) { Seeded.Add(Chunk); }
	// Removes the entities that stand in no loaded chunk (items that rolled off the edge, mobs that walked out):
	// nothing would save them, and they would only pile up. Returns how many.
	int32 RemoveStrays();

	// Called with the position of the player and the damage when a mob bites. Until players have health this is
	// left unset and the bite is only logged.
	TFunction<void(const FVector&, float)> OnPlayerHit;

	// The game rules and difficulty the host obeys: peaceful removes the hostile mobs and stops them spawning,
	// doMobSpawning off stops every natural spawn, doMobLoot off makes mobs drop nothing and doTileDrops off makes
	// blocks destroyed by explosions drop nothing.
	void SetRules(const FMcGameRules& InRules);
	const FMcGameRules& GetRules() const { return Rules; }
	// Takes the hostile mobs out of the world (peaceful). Returns how many.
	int32 RemoveHostiles();

	FMcEntityManager& GetManager() { return Manager; }
	int32 NumHostile() const;

	// Player attacks: the mob (if any) the ray from the eyes hits within Reach, hurt by Damage and pushed away.
	// True if something was hit.
	bool Attack(const FVector& Eye, const FVector& Direction, float Reach, float Damage);
	// The nearest living mob the ray from the eyes enters within Reach, or null.
	FMcMob* FindMob(const FVector& Eye, const FVector& Direction, float Reach);
	// A zombie got the villager: a zombie villager stands where it did, with what it was kept for a cure. Null if it is gone.
	FMcMob* Infect(FMcVillager& Villager);

	// The kind a weighted spawn list gives for a roll in [0, total weight); none for an empty list.
	static FName PickSpawn(TConstArrayView<FMcSpawnEntry> Entries, int32 Roll);
	// The monsters that spawn in the dark at a block: the list of the biome there (its natural_mob_spawns), or the
	// Overworld's usual ones where the chunk or the biome registry isn't there.
	TConstArrayView<FMcSpawnEntry> GetMonsterSpawns(const FIntVector& Cell) const;

	// Puts a mob of the kind at the position, with its drops and bite wired up. Null for a kind that is not a mob.
	FMcMob* SpawnMob(FName Kind, const FVector& Pos);

	// A carved pumpkin or jack o'lantern was just placed at Pumpkin: whether it tops an iron golem (a column of two iron
	// blocks with an iron block on each side of the upper one, the arms, along either axis). True gives the five blocks
	// to clear and where the golem stands, the feet of the lower block.
	bool MatchGolem(const FIntVector& Pumpkin, TArray<FIntVector>& OutBlocks, FVector& OutFeet) const;

private:
	// The zombie villagers that have shaken long enough turn back into the villagers they were.
	void FinishCures();
	void SpawnHostiles(const TArray<FVector>& Players);
	void SyncProxies();
	// The y of the feet of a mob that would stand on the ground of this column near NearY, or none.
	TOptional<int32> FindStandingY(int32 X, int32 Z, int32 NearY, int32 Range) const;
	void DropLoot(FMcMob& Mob);
	// A slime that died splits into smaller ones.
	void SplitSlime(FMcMob& Dead);
	// The look a villager born at the block has: its biome's.
	FName GetVillagerVariant(const FIntVector& Cell) const;
	// The biome at a block ("minecraft:plains"), none when it isn't known.
	FName GetBiomeName(const FIntVector& Cell) const;
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
	FMcGameRules Rules;

	TWeakObjectPtr<UWorld> GameWorld;
	TSubclassOf<AActor> ProxyClass;
	TMap<int32, TWeakObjectPtr<AActor>> Proxies;
};
