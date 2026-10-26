// Hosts one dimension: owns its blocks, generates chunks around the players and unloads the rest. Replicated
// so clients have one too: theirs starts empty and is filled by McNet's chunk streaming.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "McBlockWorld.h"
#include "McFlatGenerator.h"
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

	// M1 only makes flat worlds: a preset id from the jar's worldgen/flat_level_generator_preset/.
	UPROPERTY(EditAnywhere, Category = "Mc")
	FString FlatPreset = TEXT("minecraft:classic_flat");

	// Chunks drawn around each player, like the View Distance video setting.
	UPROPERTY(EditAnywhere, Category = "Mc", meta = (ClampMin = 2, ClampMax = 32))
	int32 ViewDistance = 12;

	UPROPERTY(EditAnywhere, Category = "Mc", meta = (ClampMin = 1))
	int32 MaxChunksGeneratedPerTick = 16;

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

	// Lives as long as the actor, not just until EndPlay: components (the renderer) end play after the
	// actor does and still unbind from it then.
	TUniquePtr<FMcBlockWorld> BlockWorld;
	TUniquePtr<FMcFlatGenerator> Generator;
	int32 SurfaceY = 0;

	// Block changes by chunk, keyed by world position.
	TMap<FIntPoint, TMap<FIntVector, FMcStateId>> Edits;
};
