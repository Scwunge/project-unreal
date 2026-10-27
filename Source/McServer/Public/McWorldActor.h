// Hosts one dimension: owns its blocks, generates chunks around the players and unloads the rest. Replicated
// so clients have one too: theirs starts empty and is filled by McNet's chunk streaming.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "McBlockWorld.h"
#include "Containers/Queue.h"
#include "McChunkGenerator.h"
#include "McChunkSave.h"
#include "McEntityHost.h"
#include "McSimulation.h"
#include "McWorldSave.h"
#include "McWorldActor.generated.h"

class FMcFeatureLibrary;
class FMcWorldStorage;

UCLASS()
class MCSERVER_API AMcWorldActor : public AActor
{
	GENERATED_BODY()

public:
	AMcWorldActor();
	virtual ~AMcWorldActor() override;

	// Dimension type id, from the jar's dimension_type/.
	UPROPERTY(EditAnywhere, Category = "Mc")
	FString Dimension = TEXT("minecraft:overworld");

	// Terrain from the jar's worldgen/noise_settings/, unless the world is flat.
	UPROPERTY(EditAnywhere, Category = "Mc")
	FString NoiseSettings = TEXT("minecraft:overworld");

	// Flat worlds use a preset from the jar's worldgen/flat_level_generator_preset/. The command line can
	// ask for one with -McFlat (dev scenes with -McScene always are).
	UPROPERTY(EditAnywhere, Category = "Mc")
	bool bFlat = false;

	UPROPERTY(EditAnywhere, Category = "Mc")
	FString FlatPreset = TEXT("minecraft:classic_flat");

	// The world seed; 0 picks one at random. -McSeed=<number> sets it.
	UPROPERTY(EditAnywhere, Category = "Mc")
	int64 Seed = 0;

	// Chunks drawn around each player, like the View Distance video setting.
	UPROPERTY(EditAnywhere, Category = "Mc", meta = (ClampMin = 2, ClampMax = 32))
	int32 ViewDistance = 12;

	// Chunks being generated on worker threads at once.
	UPROPERTY(EditAnywhere, Category = "Mc", meta = (ClampMin = 1))
	int32 MaxChunksInFlight = 16;

	// Null until BeginPlay has loaded the registry, the dimension and the preset from cache/.
	FMcBlockWorld* GetBlockWorld() const { return BlockWorld.Get(); }

	// World height of the top block, for spawning players above it.
	int32 GetSurfaceY() const { return SurfaceY; }

	// The time of day in game ticks (20 a second, 24000 a day; 0 is sunrise, 6000 noon, 12000 sunset, 18000 midnight),
	// not wrapped: the day count, and with it the moon's phase, is DayTime / 24000. The server counts it and tells
	// clients once a second, as the game's time packet does; clients run their own clock in between. Fractional,
	// so the sky moves smoothly between ticks.
	double GetDayTime() const { return DayTicks; }
	// -McTime=<ticks> sets where a new world starts (default 1000, the start of the day); -McTimeFreeze stops time.
	void SetDayTime(double Ticks);
	bool IsTimeFrozen() const { return bTimeFrozen; }
	void SetTimeFrozen(bool bFrozen) { bTimeFrozen = bFrozen; }

	// The actor class shown for each server-side entity (set by the game mode; it implements IMcEntityProxy).
	void SetEntityProxyClass(TSubclassOf<AActor> Class);
	FMcEntityHost* GetEntityHost() const { return Entities.Get(); }
	// A player's swing from Eye along Look: hurts the mob it reaches. True if one was hit.
	bool AttackMobs(const FVector& Eye, const FVector& Look, float Reach, float Damage);

	// Changes a block, loaded or not. A change to a chunk that isn't loaded is kept and put on it when it arrives. In
	// a world without a save (no -McWorld) changes to loaded chunks are kept too, so they survive unloading; in a
	// saved world the chunk's save keeps them.
	void SetBlock(const FIntVector& Pos, FMcStateId State);

	// ---- Saving (M9) ----
	// -McWorld=<name> picks a world in Saved/Worlds, made if it isn't there yet: its level file gives the seed, time
	// and terrain (the command line's -McSeed, -McFlat and so on only apply to a world being made), its chunks are
	// loaded from the region files before any are generated, and the world is saved as it's played:
	//   - a chunk is written when it unloads (if it changed or was never saved), and all that changed every 6000 ticks
	//     (five minutes), a few a frame;
	//   - everything is written at EndPlay, and waited for.
	// Without the flag nothing is read or written.
	bool HasStorage() const { return Storage.IsValid(); }
	const FString& GetWorldName() const { return WorldName; }
	const FString& GetWorldDir() const { return WorldDir; }
	const FMcLevelData& GetLevel() const { return Level; }
	FMcWorldStorage* GetStorage() const { return Storage.Get(); }
	// Saves the level and every loaded chunk that changed, and waits until it's on disk. The game thread stalls for the
	// file writes only; EndPlay does this, and so can a command.
	void SaveWorld();
	// Marks a loaded chunk as changed when something altered it behind the block world's back (a chest's contents).
	void MarkChunkDirty(FIntPoint Chunk);
	// Called at every save, with the world about to be written: where the other things that belong to the world (the
	// players, see AMcGameMode) save themselves.
	FSimpleMulticastDelegate OnSaving;
	// Game ticks between automatic saves.
	static constexpr int32 AutosaveIntervalTicks = 6000;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_SyncedDayTime();

private:
	void StreamChunks();
	// True if a chunk came back from a worker.
	bool AddGeneratedChunks();
	// Places features in pending chunks and moves the finished ones into the world, within a time budget. True if
	// it decorated or finished a chunk.
	bool DecorateChunks();

	// What a worker brings back: a chunk read from the save, or one just generated.
	struct FFinishedChunk
	{
		TUniquePtr<FMcChunk> Chunk;
		bool bFromDisk = false;
		FMcChunkSaveExtras Extras;
	};
	// Puts a finished chunk into the world: its edits, its mobs, and what's needed to know later whether it changed.
	void AddToWorld(FFinishedChunk Finished);
	// Applies the edits made while the chunk wasn't loaded (and, in a world with no save, all of them).
	void ApplyEdits(FMcChunk& Chunk);
	// Queues a loaded chunk to be written, if it changed or was never saved. True if it was queued.
	bool SaveChunk(FIntPoint Pos);
	void BeginAutosave();
	void StartStorage(const FString& Name);

	// Lives as long as the actor, not just until EndPlay: components (the renderer) end play after the
	// actor does and still unbind from it then.
	TUniquePtr<FMcBlockWorld> BlockWorld;
	// The 20 ticks a second: fluids flow and sand falls. Server only; made when the world starts.
	TUniquePtr<FMcSimulation> Simulation;
	TSubclassOf<AActor> EntityProxyClass;
	// Mobs and dropped items. Server only.
	TUniquePtr<FMcEntityHost> Entities;
	double TickRemainder = 0.0;
	// Shared with the worker tasks, which may outlive the actor by a moment.
	TSharedPtr<const FMcChunkGenerator, ESPMode::ThreadSafe> Generator;
	int32 SurfaceY = 0;

	// The clock this machine runs: the server's own count, a client's estimate from the last sync.
	double DayTicks = 1000.0;
	double TicksSinceSync = 0.0;
	UPROPERTY(ReplicatedUsing = OnRep_SyncedDayTime)
	int64 SyncedDayTime = 1000;
	UPROPERTY(Replicated)
	bool bTimeFrozen = false;

	// Where the players were (and the view distance) when a frame last found nothing to load, unload or finish; while
	// they stay put there is nothing for StreamChunks to do. Empty when something is outstanding.
	TArray<FIntPoint> SettledCentres;
	int32 SettledViewDistance = 0;

	// Chunks out on worker threads, and the finished ones coming back.
	TSet<FIntPoint> InFlight;
	TSharedPtr<TQueue<FFinishedChunk, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Finished;

	// Noise worlds decorate chunks before players get them. A generated chunk waits here until its 8 neighbours
	// are generated too, gets its features, and joins the world once its neighbours have theirs (features reach
	// up to a chunk out, so it's done changing then). Null library for flat worlds, which have no features.
	// A chunk generated next to ones already in the world (loaded from the save, or generated earlier) can't add to
	// them, so features crossing that border are cut; saved chunks keep theirs.
	TSharedPtr<FMcFeatureLibrary> Features;
	TMap<FIntPoint, TUniquePtr<FMcChunk>> Pending;
	TSet<FIntPoint> Decorated;

	// Block changes by chunk, keyed by world position.
	TMap<FIntPoint, TMap<FIntVector, FMcStateId>> Edits;

	// ---- Saving ----
	FString WorldName;
	FString WorldDir;
	FMcLevelData Level;
	// Shared with the worker tasks that read chunks from it, like the generator.
	TSharedPtr<FMcWorldStorage, ESPMode::ThreadSafe> Storage;
	// What a loaded chunk's save looks like, to tell whether it needs writing again.
	struct FChunkState
	{
		// The chunk's revision when it was loaded or last written.
		uint32 SavedRevision = 0;
		// Whether the chunk has been written (or came from the save) at all.
		bool bOnDisk = false;
		// Whether what it was saved with included mobs or items, so a chunk that has lost them is written again.
		bool bHadEntities = false;
		bool bDirty = false;
	};
	TMap<FIntPoint, FChunkState> ChunkStates;
	// Chunks still to be looked at by the autosave in progress.
	TArray<FIntPoint> AutosaveQueue;
	int32 TicksSinceAutosave = 0;
};
