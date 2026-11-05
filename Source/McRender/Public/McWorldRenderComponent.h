// Draws an FMcBlockWorld: meshes chunk columns on worker threads and uploads them as procedural meshes.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Containers/Queue.h"
#include "McSectionMesher.h"
#include "McWorldRenderComponent.generated.h"

namespace McFluidMesher { struct FFluidPalette; }

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

	// How hard it rains where the player is, 0 to 1 (see AMcWorldActor::GetRainLevel): wets what it falls on in Unreal Mode.
	TFunction<float()> GetRain;

	// The pass materials for the current look, indexed by EMcRenderPass, with the texture array set (particles draw
	// with the cutout one).
	UMaterialInterface* GetPassMaterial(EMcRenderPass Pass) const { return PassMaterials.IsValidIndex(static_cast<int32>(Pass)) ? PassMaterials[static_cast<int32>(Pass)].Get() : nullptr; }
	uint32 GetMaterialGeneration() const { return MaterialGeneration; }
	const FMcRenderPalette* GetPalette() const { return Palette.Get(); }

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

	// The pass materials for the current look (McRenderMode), set on every mesh drawn so far.
	void MakePassMaterials();
	uint32 MaterialGeneration = 0;
	float Wetness = 0.0f;

	void LaunchMeshTasks();
	void UploadResults();
	void Upload(FChunkResult& Result);
	UProceduralMeshComponent* MakeMesh(TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Map, FIntPoint Pos, int32 SortPriority);

	FMcBlockWorld* World = nullptr;
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
	FDelegateHandle ChangedHandle;

	void RebuildPalette();

	TSharedPtr<const FMcRenderPalette> Palette;
	TSharedPtr<const struct FMcLightPalette> LightPalette;
	TSharedPtr<const McFluidMesher::FFluidPalette> FluidPalette;
	float Underwater = 0.0f;
	TMap<FIntPoint, FChunkState> Chunks;
	uint64 NextTicket = 1;
	int32 InFlight = 0;
	// Every triangle handed to the engine since the start, for the harness's record.
	int64 UploadedTriangles = 0;

	// Shared with the tasks so a task that finishes after the component is gone has somewhere to write.
	TSharedPtr<TQueue<TUniquePtr<FChunkResult>, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Results;

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>> Meshes;
	// The water of each chunk, in a mesh of its own that is drawn before all the other translucent things (ice,
	// glass), whichever chunk they are in; one mesh cannot order them against another chunk's.
	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>> WaterMeshes;

	// The translucent quads of each chunk's sections, kept to be put in order again, farthest first, when the camera has
	// moved: blending glass over glass only comes out right in that order.
	struct FTranslucentSection
	{
		int32 MeshSection = 0;
		FMcMeshBuffers Buffers;
	};
	struct FTranslucentChunk
	{
		TArray<FTranslucentSection> Sections;
		FVector SortedFor = FVector::ZeroVector;
	};
	TMap<FIntPoint, FTranslucentChunk> TranslucentChunks;
	void ResortTranslucent();
	FVector GetViewLocation() const;

	// The pass materials with the texture array set, indexed by EMcRenderPass.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> PassMaterials;
};
