#include "McWorldRenderComponent.h"
#include "McBlockModels.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "McBlockWorld.h"
#include "McFluidMesher.h"
#include "McLightEngine.h"
#include "McRender.h"
#include "McSkyComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Texture2DArray.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Tasks/Task.h"
#include "UObject/ConstructorHelpers.h"

static TAutoConsoleVariable<bool> CVarMcFlipWinding(
	TEXT("mc.Render.FlipWinding"),
	false,
	TEXT("Reverse the triangle order of block faces. Only for checking the mesher's winding; remeshes everything."));

namespace
{
	// All eight: light reaches 15 blocks, so a chunk's light depends on its diagonal neighbours too.
	const FIntPoint NeighbourOffsets[8] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
		FIntPoint(1, 1), FIntPoint(-1, 1), FIntPoint(1, -1), FIntPoint(-1, -1),
	};
}

UMcWorldRenderComponent::UMcWorldRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Results = MakeShared<TQueue<TUniquePtr<FChunkResult>, EQueueMode::Mpsc>, ESPMode::ThreadSafe>();

	// Made by tools/make_materials.py.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Opaque(TEXT("/Game/Mc/Materials/M_McBlockOpaque.M_McBlockOpaque"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cutout(TEXT("/Game/Mc/Materials/M_McBlockCutout.M_McBlockCutout"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Translucent(TEXT("/Game/Mc/Materials/M_McBlockTranslucent.M_McBlockTranslucent"));
	OpaqueMaterial = Opaque.Object;
	CutoutMaterial = Cutout.Object;
	TranslucentMaterial = Translucent.Object;
}

void UMcWorldRenderComponent::BeginPlay()
{
	Super::BeginPlay();

	UTexture2DArray* Blocks = FMcBlockTextures::Get().EnsureLoaded() ? FMcBlockTextures::Get().GetTextureArray() : nullptr;
	PassMaterials.Reset();
	for (UMaterialInterface* Base : { OpaqueMaterial.Get(), CutoutMaterial.Get(), TranslucentMaterial.Get() })
	{
		if (!Base)
		{
			UE_LOG(LogMcRender, Error, TEXT("A block material is missing; run tools/make_materials.py"));
			Base = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, this);
		if (Blocks)
		{
			Instance->SetTextureParameterValue(TEXT("Blocks"), Blocks);
		}
		PassMaterials.Add(Instance);
	}
}

void UMcWorldRenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetWorld(nullptr);
	Super::EndPlay(EndPlayReason);
}

void UMcWorldRenderComponent::SetWorld(FMcBlockWorld* InWorld)
{
	if (World)
	{
		World->OnChunkAdded.Remove(AddedHandle);
		World->OnChunkRemoved.Remove(RemovedHandle);
		World->OnChunkChanged.Remove(ChangedHandle);
	}
	for (const TPair<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Mesh : Meshes)
	{
		if (Mesh.Value)
		{
			Mesh.Value->DestroyComponent();
		}
	}
	Meshes.Reset();
	Chunks.Reset();

	World = InWorld;
	if (!World)
	{
		return;
	}

	RebuildPalette();
	AddedHandle = World->OnChunkAdded.AddUObject(this, &UMcWorldRenderComponent::HandleChunkAdded);
	RemovedHandle = World->OnChunkRemoved.AddUObject(this, &UMcWorldRenderComponent::HandleChunkRemoved);
	ChangedHandle = World->OnChunkChanged.AddUObject(this, &UMcWorldRenderComponent::HandleChunkChanged);
	for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Chunk : World->GetChunks())
	{
		MarkDirty(Chunk.Key);
	}
}

void UMcWorldRenderComponent::RebuildPalette()
{
	FMcBlockModels& Models = FMcBlockModels::Get();
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	if (!Models.EnsureBaked() || !Textures.EnsureLoaded())
	{
		UE_LOG(LogMcRender, Error, TEXT("Block models or textures failed to load; see the McAssets errors above"));
	}
	Palette = FMcRenderPalette::Build(FMcBlockRegistry::Get(), Models, Textures, CVarMcFlipWinding.GetValueOnGameThread());
	FluidPalette = McFluidMesher::BuildPalette(FMcBlockRegistry::Get(), Textures);
	if (!FMcBlockLight::Get().EnsureLoaded())
	{
		UE_LOG(LogMcRender, Error, TEXT("Block light tables failed to load; see the McCore errors above"));
	}
	LightPalette = FMcLightPalette::Build();
}

void UMcWorldRenderComponent::HandleChunkAdded(FIntPoint Pos)
{
	MarkDirty(Pos);
	for (const FIntPoint& Offset : NeighbourOffsets)
	{
		if (Chunks.Contains(Pos + Offset))
		{
			MarkDirty(Pos + Offset);
		}
	}
}

void UMcWorldRenderComponent::HandleChunkRemoved(FIntPoint Pos)
{
	Chunks.Remove(Pos);
	TObjectPtr<UProceduralMeshComponent> Mesh;
	if (Meshes.RemoveAndCopyValue(Pos, Mesh) && Mesh)
	{
		Mesh->DestroyComponent();
	}
}

void UMcWorldRenderComponent::HandleChunkChanged(FIntPoint Pos)
{
	// A change on the chunk's edge can show or hide faces of the neighbours too.
	HandleChunkAdded(Pos);
}

void UMcWorldRenderComponent::MarkDirty(FIntPoint Pos)
{
	Chunks.FindOrAdd(Pos).bDirty = true;
}

bool UMcWorldRenderComponent::HasAllNeighbours(FIntPoint Pos) const
{
	for (const FIntPoint& Offset : NeighbourOffsets)
	{
		if (!World->HasChunk(Pos + Offset))
		{
			return false;
		}
	}
	return true;
}

void UMcWorldRenderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!World)
	{
		return;
	}

	// Texture animations run on game time, 20 ticks a second.
	FMcBlockTextures::Get().Tick(GetWorld()->GetTimeSeconds() * 20.0);

	// The sky's light reaching the blocks follows the time of day; the chunks' stored light does not change.
	for (UMaterialInterface* Material : PassMaterials)
	{
		if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Material))
		{
			Instance->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		}
	}

	if (Palette->bFlipWinding != CVarMcFlipWinding.GetValueOnGameThread())
	{
		RebuildPalette();
		for (TPair<FIntPoint, FChunkState>& Chunk : Chunks)
		{
			Chunk.Value.bDirty = true;
		}
	}

	LaunchMeshTasks();
	UploadResults();
}

void UMcWorldRenderComponent::LaunchMeshTasks()
{
	// Chunks only mesh once their four neighbours are loaded, so their edge faces are culled correctly.
	TArray<FIntPoint> Ready;
	for (const TPair<FIntPoint, FChunkState>& Chunk : Chunks)
	{
		if (Chunk.Value.bDirty && HasAllNeighbours(Chunk.Key))
		{
			Ready.Add(Chunk.Key);
		}
	}
	if (Ready.Num() == 0)
	{
		return;
	}

	// Nearest the camera first.
	FVector2D ViewChunk = FVector2D::ZeroVector;
	if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		if (Controller->PlayerCameraManager)
		{
			const FVector View = Controller->PlayerCameraManager->GetCameraLocation();
			const double ChunkUnits = McCoords::SectionSize * McCoords::UnitsPerBlock;
			ViewChunk = FVector2D(View.X / ChunkUnits, View.Y / ChunkUnits);
		}
	}
	Ready.Sort([ViewChunk](const FIntPoint& A, const FIntPoint& B)
	{
		return FVector2D::DistSquared(FVector2D(A) + 0.5, ViewChunk) < FVector2D::DistSquared(FVector2D(B) + 0.5, ViewChunk);
	});

	const int32 NumToLaunch = FMath::Min(Ready.Num(), MaxMeshLaunchesPerTick);
	for (int32 Index = 0; Index < NumToLaunch; ++Index)
	{
		const FIntPoint Pos = Ready[Index];
		const FMcChunk* Chunk = World->FindChunk(Pos);
		check(Chunk);

		TArray<int32> Sections;
		for (int32 SectionIndex = 0; SectionIndex < Chunk->NumSections(); ++SectionIndex)
		{
			if (!Chunk->GetSection(SectionIndex).IsEmpty())
			{
				Sections.Add(SectionIndex);
			}
		}
		FMcLightRegion Region = FMcLightRegion::Capture(*World, Pos);

		FChunkState& State = Chunks[Pos];
		State.bDirty = false;
		State.Ticket = NextTicket++;
		++InFlight;

		UE::Tasks::Launch(UE_SOURCE_LOCATION,
			[Results = Results, Palette = Palette.ToSharedRef(), LightPalette = LightPalette.ToSharedRef(), FluidPalette = FluidPalette.ToSharedRef(), Region = MoveTemp(Region), Sections = MoveTemp(Sections), Pos, Ticket = State.Ticket]() mutable
			{
				Region.Compute(*LightPalette);

				TUniquePtr<FChunkResult> Result = MakeUnique<FChunkResult>();
				Result->ChunkPos = Pos;
				Result->Ticket = Ticket;
				for (const int32 SectionIndex : Sections)
				{
					const FMcSectionSnapshot Snapshot = FMcSectionSnapshot::FromRegion(Region, SectionIndex, *Palette);
					FMcSectionMesh Mesh = McBlockMesher::Build(Snapshot, *Palette);
					McFluidMesher::Build(Snapshot, *FluidPalette, *Palette, Mesh);
					if (!Mesh.IsEmpty())
					{
						Result->Sections.Add(MoveTemp(Mesh));
					}
				}
				Results->Enqueue(MoveTemp(Result));
			});
	}
}

void UMcWorldRenderComponent::UploadResults()
{
	for (int32 Count = 0; Count < MaxMeshUploadsPerTick; ++Count)
	{
		TUniquePtr<FChunkResult> Result;
		if (!Results->Dequeue(Result))
		{
			break;
		}
		--InFlight;

		// Drop results for chunks that have unloaded or been meshed again since.
		const FChunkState* State = Chunks.Find(Result->ChunkPos);
		if (State && State->Ticket == Result->Ticket)
		{
			Upload(*Result);
		}
	}
}

void UMcWorldRenderComponent::Upload(FChunkResult& Result)
{
	TObjectPtr<UProceduralMeshComponent>* Existing = Meshes.Find(Result.ChunkPos);
	if (Result.Sections.Num() == 0)
	{
		if (Existing && *Existing)
		{
			(*Existing)->DestroyComponent();
		}
		Meshes.Remove(Result.ChunkPos);
		return;
	}

	UProceduralMeshComponent* Mesh = Existing ? Existing->Get() : nullptr;
	if (!Mesh)
	{
		Mesh = NewObject<UProceduralMeshComponent>(GetOwner());
		Mesh->SetupAttachment(this);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->RegisterComponent();
		const double ChunkUnits = McCoords::SectionSize * McCoords::UnitsPerBlock;
		Mesh->SetWorldLocation(FVector(Result.ChunkPos.X * ChunkUnits, Result.ChunkPos.Y * ChunkUnits, 0.0));
		Meshes.Add(Result.ChunkPos, Mesh);
	}

	// Mesh section SectionIndex * passes + pass holds that pass of that chunk section.
	Mesh->ClearAllMeshSections();
	const TArray<FVector2D> NoUVs;
	const TArray<FProcMeshTangent> NoTangents;
	for (const FMcSectionMesh& Section : Result.Sections)
	{
		for (int32 Pass = 0; Pass < McNumRenderPasses; ++Pass)
		{
			const FMcMeshBuffers& Buffers = Section.Passes[Pass];
			if (Buffers.IsEmpty())
			{
				continue;
			}
			const int32 MeshSection = Section.SectionIndex * McNumRenderPasses + Pass;
			Mesh->CreateMeshSection(MeshSection, Buffers.Positions, Buffers.Triangles, Buffers.Normals, Buffers.UV0, Buffers.UV1, Buffers.UV2, NoUVs, Buffers.Colors, NoTangents, false);
			Mesh->SetMaterial(MeshSection, PassMaterials.IsValidIndex(Pass) ? PassMaterials[Pass].Get() : nullptr);
		}
	}
}
