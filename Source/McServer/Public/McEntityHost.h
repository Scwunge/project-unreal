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
class FMcSpawners;
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
	// One entity as bytes (the format of CaptureChunk with a count of one), to carry it to another dimension; empty if it
	// is not kept in saves. UnpackEntity puts it back here with a new id, at Pos, and returns it (null if the bytes are no
	// good). Added for portal travel (McTravel).
	TArray<uint8> PackEntity(FMcEntity& Entity);
	FMcEntity* UnpackEntity(TConstArrayView<uint8> Bytes, const FVector& Pos);
	// A chunk that came from a save has had its turn at animals: don't give it a herd when it joins the world.
	// Call before adding the chunk.
	void MarkSeeded(FIntPoint Chunk) { Seeded.Add(Chunk); }
	// Removes the entities that stand in no loaded chunk (items that rolled off the edge, mobs that walked out):
	// nothing would save them, and they would only pile up. Returns how many.
	int32 RemoveStrays();

	// Called with the position of the player and the damage when a mob bites. Until players have health this is
	// left unset and the bite is only logged.
	TFunction<void(const FVector&, float)> OnPlayerHit;
	// Called with the player's feet, the damage, the push and the centre when a blast reaches a player.
	TFunction<void(const FVector&, float, const FVector&, const FVector&)> OnPlayerBlast;
	// Whether the player at the position wears gold armour (piglins ignore them).
	TFunction<bool(const FVector&)> PlayerWearsGold;
	// A splash potion's effect on the player at the position (name, level less one, ticks).
	TFunction<void(const FVector&, FName, int32, int32)> OnPlayerEffect;

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
	// A vehicle (minecart, boat) is hit as well; in creative it breaks at once and drops nothing.
	bool Attack(const FVector& Eye, const FVector& Direction, float Reach, float Damage, bool bCreative = false);
	// The nearest vehicle the ray from the eyes reaches within Reach, or null.
	FMcEntity* FindVehicle(const FVector& Eye, const FVector& Direction, float Reach);
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
	// The monster spawners of the loaded chunks (McSpawners), run each tick.
	FMcSpawners& GetSpawners() { return *Spawners; }

	// A carved pumpkin or jack o'lantern was just placed at Pumpkin: whether it tops an iron golem (a column of two iron
	// blocks with an iron block on each side of the upper one, the arms, along either axis). True gives the five blocks
	// to clear and where the golem stands, the feet of the lower block.
	// A wandering trader arrives near a player, with two trader llamas on leads. False if there was no room.
	bool SpawnWanderingTrader(const FVector& Player);

	bool MatchGolem(const FIntVector& Pumpkin, TArray<FIntVector>& OutBlocks, FVector& OutFeet) const;

	// ---- Patrols and raids (minecraft.wiki, "Pillager patrol" and "Raid") ----
	// A raid: waves of raiders sent at a village. It waits 15 seconds, sends a wave, and sends the next once the last is
	// dead; the last one dead is a victory, and no village left (or 40 minutes) a defeat.
	bool CountVillagePoi(const FVector& Center) const;
	struct FRaid
	{
		enum class EStatus : uint8 { Waiting, Active, Victory, Defeat };
		int32 Id = 0;
		FVector Center = FVector::ZeroVector;
		// The level of the Bad Omen that called it, 1 to 5; a raid from level 2 up has an extra wave.
		int32 Level = 1;
		// Waves sent so far, and how many there are in all.
		int32 Wave = 0;
		int32 TotalWaves = 5;
		EStatus Status = EStatus::Waiting;
		int64 Age = 0;
		// Ticks to the next step: the first wave, the next wave, or the end of the celebration.
		int32 Timer = 0;
		// The health of the wave when it came, and the ids of those of it still about.
		float WaveHealth = 1.0f;
		TArray<int32> Raiders;
		// The raider carrying the banner (0 for none), and whether a bed, bell or job block was ever seen about the village.
		int32 CaptainId = 0;
		bool bSawPoi = false;
	};
	static constexpr int32 RaidWarmupTicks = 300;
	// Measured (raid6-9, vanilla, kill to next wave seen, sample grid 14-21 ticks): 293 to 321 between the last raider's death and the next wave, so the true gap is at most 293
	// (the wiki's 300 is above it). Within (272, 293] unfitted; 293 is the best upper bound (provenance gap).
	static constexpr int32 RaidWaveDelayTicks = 293;
	// Raiders further than this from the village are removed (wiki; checked every 20 ticks).
	static constexpr double RaiderLeashRange = 112.0;
	static constexpr int32 RaidCelebrationTicks = 600;
	static constexpr int64 RaidMaxTicks = 48000;
	static constexpr double RaidBarRange = 96.0;
	static constexpr double BellGlowRange = 48.0;
	static constexpr int32 BellGlowTicks = 60;
	static constexpr int64 PatrolIntervalTicks = 12000;
	static constexpr int64 PatrolFirstDay = 5;

	// The waves there are for a difficulty (1 easy to 3 hard) and omen level, extra one included.
	static int32 GetWaveCount(int32 Difficulty, int32 Level);
	// The raiders of one wave (1 to the last), by kind, drawn from Random.
	static void RollWave(int32 Difficulty, int32 Wave, FRandomStream& Random, TArray<TPair<FName, int32>>& Out);
	// Whether a village is round a point (a villager within 48 blocks) and where its middle is (the villagers' average).
	bool FindVillage(const FVector& Where, FVector& OutCenter) const;
	// Calls a raid on the village at Center that starts after DelayTicks (the raid omen); returns its id, 0 if refused
	// (peaceful, or one is running there already).
	int32 StartRaid(const FVector& Center, int32 Level, int32 DelayTicks);
	const TArray<FRaid>& GetRaids() const { return Raids; }
	const FRaid* FindRaid(int32 Id) const;
	// The raid bar a player at that point sees: the nearest raid within 96 blocks, its title and how full it is.
	bool GetRaidBar(const FVector& Player, FString& OutTitle, float& OutFraction) const;
	// A bell rung at the point makes the raiders within 48 blocks glow for 3 seconds. Returns how many.
	int32 RingBell(const FVector& Bell);
	// A raid party arrives near a player: a captain and some pillagers. False if there was no room or a village is near.
	bool SpawnPatrol(const FVector& Player);
	// Called with the middle of a raid that was won and its omen level: the players round it become Heroes of the Village.
	TFunction<void(const FVector&, int32)> OnRaidWon;

private:
	// The zombie villagers that have shaken long enough turn back into the villagers they were.
	void FinishCures();
	void TickRaids();
	void SpawnWave(FRaid& Raid);
	void SpawnHostiles(const TArray<FVector>& Players);
	void SyncProxies();
	// The y of the feet of a mob that would stand on the ground of this column near NearY, or none.
	TOptional<int32> FindStandingY(int32 X, int32 Z, int32 NearY, int32 Range) const;
	// The feet cell for something that swims: just under the surface of the water at X, Z (feet and head both in water), if there is any near NearY.
	TOptional<int32> FindSwimmingY(int32 X, int32 Z, int32 NearY, int32 Range) const;
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
	int32 TraderChance = 25;
	int64 NextPatrolTick = 0;
	int32 NextRaidId = 1;
	TArray<FRaid> Raids;
	static constexpr int64 WanderingTraderIntervalTicks = 24000;
	FRandomStream Random;
	// The chunks that have had their turn at animals, or are queued for it; forgotten when the chunk unloads.
	TSet<FIntPoint> Seeded;
	TArray<FIntPoint> SeedQueue;
	bool bDevSpawnDone = false;
	FMcGameRules Rules;
	TUniquePtr<FMcSpawners> Spawners;
	FDelegateHandle BlockEntityHandle;

	TWeakObjectPtr<UWorld> GameWorld;
	TSubclassOf<AActor> ProxyClass;
	TMap<int32, TWeakObjectPtr<AActor>> Proxies;
};
