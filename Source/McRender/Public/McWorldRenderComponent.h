// Draws an FMcBlockWorld: meshes chunk columns on worker threads and uploads them as procedural meshes.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Containers/Queue.h"
#include "McSectionMesher.h"
#include "McWorldRenderComponent.generated.h"

class FMcBlockWorld;
class UMaterialInterface;
class UProceduralMeshComponent;

UCLASS(ClassGroup = (Mc), meta = (BlueprintSpawnableComponent))
class MCRENDER_API UMcWorldRenderComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMcWorldRenderComponent();

	// Starts drawing this world (or stops, with null). The world must outlive the component or be unset first.
	void SetWorld(FMcBlockWorld* InWorld);

	// One material per EMcRenderPass. Each samples the block texture array through a "Blocks" parameter,
	// which the component points at FMcBlockTextures' array.
	UPROPERTY(EditAnywhere, Category = "Mc")
	TObjectPtr<UMaterialInterface> OpaqueMaterial;

	UPROPERTY(EditAnywhere, Category = "Mc")
	TObjectPtr<UMaterialInterface> CutoutMaterial;

	UPROPERTY(EditAnywhere, Category = "Mc")
	TObjectPtr<UMaterialInterface> TranslucentMaterial;

	UPROPERTY(EditAnywhere, Category = "Mc", meta = (ClampMin = 1))
	int32 MaxMeshLaunchesPerTick = 32;

	UPROPERTY(EditAnywhere, Category = "Mc", meta = (ClampMin = 1))
	int32 MaxMeshUploadsPerTick = 16;

	int32 NumMeshedChunks() const { return Meshes.Num(); }
	int32 NumPendingChunks() const { return InFlight; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FChunkResult
	{
		FIntPoint ChunkPos;
		uint64 Ticket = 0;
		TArray<FMcSectionMesh> Sections;  // only the sections that were not empty
	};

	struct FChunkState
	{
		bool bDirty = true;
		uint64 Ticket = 0;  // the newest mesh task launched for this chunk; older results are dropped
	};

	void HandleChunkAdded(FIntPoint Pos);
	void HandleChunkRemoved(FIntPoint Pos);
	void HandleChunkChanged(FIntPoint Pos);
	void MarkDirty(FIntPoint Pos);
	bool HasAllNeighbours(FIntPoint Pos) const;

	void LaunchMeshTasks();
	void UploadResults();
	void Upload(FChunkResult& Result);

	FMcBlockWorld* World = nullptr;
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
	FDelegateHandle ChangedHandle;

	void RebuildPalette();

	TSharedPtr<const FMcRenderPalette> Palette;
	TMap<FIntPoint, FChunkState> Chunks;
	uint64 NextTicket = 1;
	int32 InFlight = 0;

	// Shared with the tasks so a task that finishes after the component is gone has somewhere to write.
	TSharedPtr<TQueue<TUniquePtr<FChunkResult>, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Results;

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>> Meshes;

	// The pass materials with the texture array set, indexed by EMcRenderPass.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> PassMaterials;
};
