// Hosts one dimension: owns its blocks, generates chunks around the players and unloads the rest. Replicated
// so clients have one too: theirs starts empty and is filled by McNet's chunk streaming.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "McBlockWorld.h"
#include "Containers/Queue.h"
#include "McChunkGenerator.h"
#include "McEntityHost.h"
#include "McSimulation.h"
#include "McWorldActor.generated.h"

class FMcFeatureLibrary;

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

	// Changes a block, loaded or not. Until saving exists (M9) every change is also kept in memory and
	// put back whenever its chunk is generated again, so edits survive unloading.
	void SetBlock(const FIntVector& Pos, FMcStateId State);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_SyncedDayTime();

private:
	void StreamChunks();
	void AddGeneratedChunks();
	// Places features in pending chunks and moves the finished ones into the world, within a time budget.
	void DecorateChunks();

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

	// Chunks out on worker threads, and the finished ones coming back.
	TSet<FIntPoint> InFlight;
	TSharedPtr<TQueue<TUniquePtr<FMcChunk>, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Finished;

	// Noise worlds decorate chunks before players get them. A generated chunk waits here until its 8 neighbours
	// are generated too, gets its features, and joins the world once its neighbours have theirs (features reach
	// up to a chunk out, so it's done changing then). Null library for flat worlds, which have no features.
	// A chunk generated again next to ones already in the world can't add to them, so features crossing that
	// border are cut until saving (M9) keeps decorated chunks.
	TSharedPtr<FMcFeatureLibrary> Features;
	TMap<FIntPoint, TUniquePtr<FMcChunk>> Pending;
	TSet<FIntPoint> Decorated;

	// Block changes by chunk, keyed by world position.
	TMap<FIntPoint, TMap<FIntVector, FMcStateId>> Edits;
};
