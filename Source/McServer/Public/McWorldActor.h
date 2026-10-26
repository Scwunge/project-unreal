// Hosts one dimension: owns its blocks, generates chunks around the players and unloads the rest. Replicated
// so clients have one too: theirs starts empty and is filled by McNet's chunk streaming.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "McBlockWorld.h"
#include "Containers/Queue.h"
#include "McChunkGenerator.h"
#include "McWorldActor.generated.h"

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

	// Changes a block, loaded or not. Until saving exists (M9) every change is also kept in memory and
	// put back whenever its chunk is generated again, so edits survive unloading.
	void SetBlock(const FIntVector& Pos, FMcStateId State);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void StreamChunks();
	void AddGeneratedChunks();

	// Lives as long as the actor, not just until EndPlay: components (the renderer) end play after the
	// actor does and still unbind from it then.
	TUniquePtr<FMcBlockWorld> BlockWorld;
	// Shared with the worker tasks, which may outlive the actor by a moment.
	TSharedPtr<const FMcChunkGenerator, ESPMode::ThreadSafe> Generator;
	int32 SurfaceY = 0;

	// Chunks out on worker threads, and the finished ones coming back.
	TSet<FIntPoint> InFlight;
	TSharedPtr<TQueue<TUniquePtr<FMcChunk>, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Finished;

	// Block changes by chunk, keyed by world position.
	TMap<FIntPoint, TMap<FIntVector, FMcStateId>> Edits;
};
