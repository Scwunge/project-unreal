#include "McWorldRenderComponent.h"
#include "McBlockModels.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "McBlockWorld.h"
#include "McBlockTags.h"
#include "McFluidMesher.h"
#include "McLightEngine.h"
#include "McRender.h"
#include "McSkyComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Texture2DArray.h"
#include "McRenderMode.h"
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

	MakePassMaterials();
}

void UMcWorldRenderComponent::MakePassMaterials()
{
	UTexture2DArray* Blocks = FMcBlockTextures::Get().EnsureLoaded() ? FMcBlockTextures::Get().GetTextureArray() : nullptr;
	PassMaterials.Reset();
	for (UMaterialInterface* Base : { OpaqueMaterial.Get(), CutoutMaterial.Get(), TranslucentMaterial.Get(), TranslucentMaterial.Get() })
	{
		if (!Base)
		{
			UE_LOG(LogMcRender, Error, TEXT("A block material is missing; run tools/make_materials.py"));
			Base = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(McRenderMode::Pick(Base), this);
		if (Blocks)
		{
			Instance->SetTextureParameterValue(TEXT("Blocks"), Blocks);
		}
		McRenderMode::ApplyHdBlocks(Instance);
		PassMaterials.Add(Instance);
	}
	MaterialGeneration = McRenderMode::GetGeneration();

	// Meshes already drawn take the new ones: mesh section SectionIndex * passes + pass is that pass.
	for (const TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>>* Map : { &Meshes, &WaterMeshes })
	{
		for (const TPair<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Pair : *Map)
		{
			if (UProceduralMeshComponent* Mesh = Pair.Value)
			{
				for (int32 MeshSection = 0; MeshSection < Mesh->GetNumSections(); ++MeshSection)
				{
					const int32 Pass = MeshSection % McNumRenderPasses;
					Mesh->SetMaterial(MeshSection, PassMaterials.IsValidIndex(Pass) ? PassMaterials[Pass].Get() : nullptr);
				}
			}
		}
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
	for (const TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>>* Map : { &Meshes, &WaterMeshes })
	{
		for (const TPair<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Mesh : *Map)
		{
			if (Mesh.Value)
			{
				Mesh.Value->DestroyComponent();
			}
		}
	}
	Meshes.Reset();
	WaterMeshes.Reset();
	TranslucentChunks.Reset();
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
	// The palette reads which blocks give light (for Unreal Mode's glow).
	if (!FMcBlockLight::Get().EnsureLoaded())
	{
		UE_LOG(LogMcRender, Error, TEXT("Block light tables failed to load; see the McCore errors above"));
	}
	Palette = FMcRenderPalette::Build(FMcBlockRegistry::Get(), Models, Textures, CVarMcFlipWinding.GetValueOnGameThread());
	FluidPalette = McFluidMesher::BuildPalette(FMcBlockRegistry::Get(), Textures);
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
	TranslucentChunks.Remove(Pos);
	TObjectPtr<UProceduralMeshComponent> Mesh;
	if (Meshes.RemoveAndCopyValue(Pos, Mesh) && Mesh)
	{
		Mesh->DestroyComponent();
	}
	if (WaterMeshes.RemoveAndCopyValue(Pos, Mesh) && Mesh)
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

	if (MaterialGeneration != McRenderMode::GetGeneration())
	{
		MakePassMaterials();
	}

	// The sky's light reaching the blocks follows the time of day; the chunks' stored light does not change. In Unreal
	// Mode what the rain falls on goes wet in about five seconds, and dries in about thirty once it stops.
	const float Rain = GetRain ? FMath::Clamp(GetRain(), 0.0f, 1.0f) : 0.0f;
	Wetness = Rain > Wetness ? FMath::Min(Rain, Wetness + DeltaTime / 5.0f) : FMath::Max(Rain, Wetness - DeltaTime / 30.0f);
	// How open to the sky the camera is: nothing but air (or leaves, which let half through) in the 24 blocks above it.
	if (World)
	{
		const FVector Eye = McCoords::UnrealToBlock(GetViewLocation());
		const FIntVector Pos(FMath::FloorToInt(Eye.X), FMath::FloorToInt(Eye.Y), FMath::FloorToInt(Eye.Z));
		static const int32 Leaves = FMcBlockTags::Get().Find(TEXT("leaves"));
		float Open = 1.0f;
		for (int32 Up = 1; Up <= 24 && Open > 0.0f; ++Up)
		{
			const FMcStateId Above = World->GetBlock(Pos + FIntVector(0, Up, 0));
			if (Above != 0 && !FMcBlockTags::Get().Has(Leaves, Above))
			{
				Open = 0.0f;
			}
		}
		McSky::SetCameraOpen(Open);
		// Eyes in a water block: the sea's fog and dim light.
		const FMcStateId Here = World->GetBlock(Pos);
		const bool bWet = FluidPalette.IsValid() && FluidPalette->States.IsValidIndex(Here) && FluidPalette->States[Here].Kind == McFluidMesher::EFluid::Water;
		Underwater = FMath::FInterpTo(Underwater, bWet ? 1.0f : 0.0f, DeltaTime, 8.0f);
		McSky::SetCameraUnderwater(Underwater);
	}
	for (UMaterialInterface* Material : PassMaterials)
	{
		if (UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Material))
		{
			Instance->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
			Instance->SetScalarParameterValue(TEXT("Wetness"), Wetness);
				Instance->SetScalarParameterValue(TEXT("Underwater"), Underwater);
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
	ResortTranslucent();
}

FVector UMcWorldRenderComponent::GetViewLocation() const
{
	if (const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (Controller->PlayerCameraManager)
		{
			return Controller->PlayerCameraManager->GetCameraLocation();
		}
	}
	return FVector::ZeroVector;
}

void UMcWorldRenderComponent::ResortTranslucent()
{
	if (TranslucentChunks.Num() == 0)
	{
		return;
	}
	// One chunk a frame: the one whose order is the most out of date, once the camera has gone a block and a half.
	const FVector View = GetViewLocation();
	const double ChunkUnits = McCoords::SectionSize * McCoords::UnitsPerBlock;
	FIntPoint Worst(0, 0);
	double WorstMoved = FMath::Square(1.5 * McCoords::UnitsPerBlock);
	bool bFound = false;
	for (const TPair<FIntPoint, FTranslucentChunk>& Entry : TranslucentChunks)
	{
		const double Moved = FVector::DistSquared(View, Entry.Value.SortedFor);
		if (Moved > WorstMoved)
		{
			WorstMoved = Moved;
			Worst = Entry.Key;
			bFound = true;
		}
	}
	if (!bFound)
	{
		return;
	}
	FTranslucentChunk& Chunk = TranslucentChunks[Worst];
	const TObjectPtr<UProceduralMeshComponent>* Found = Meshes.Find(Worst);
	if (!Found || !Found->Get())
	{
		TranslucentChunks.Remove(Worst);
		return;
	}
	UProceduralMeshComponent* Mesh = Found->Get();
	const TArray<FVector2D> NoUVs;
	const TArray<FProcMeshTangent> NoTangents;
	const FVector Local = View - FVector(Worst.X * ChunkUnits, Worst.Y * ChunkUnits, 0.0);
	for (FTranslucentSection& Section : Chunk.Sections)
	{
		McTranslucent::SortQuadsBackToFront(Section.Buffers, Local);
		const FMcMeshBuffers& Buffers = Section.Buffers;
		Mesh->CreateMeshSection(Section.MeshSection, Buffers.Positions, Buffers.Triangles, Buffers.Normals, Buffers.UV0, Buffers.UV1, Buffers.UV2, NoUVs, Buffers.Colors, NoTangents, false);
		const int32 Pass = static_cast<int32>(EMcRenderPass::Translucent);
		Mesh->SetMaterial(Section.MeshSection, PassMaterials.IsValidIndex(Pass) ? PassMaterials[Pass].Get() : nullptr);
	}
	Chunk.SortedFor = View;
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

UProceduralMeshComponent* UMcWorldRenderComponent::MakeMesh(TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Map, FIntPoint Pos, int32 SortPriority)
{
	if (const TObjectPtr<UProceduralMeshComponent>* Existing = Map.Find(Pos))
	{
		if (UProceduralMeshComponent* Mesh = Existing->Get())
		{
			Mesh->ClearAllMeshSections();
			return Mesh;
		}
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(GetOwner());
	Mesh->SetupAttachment(this);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetTranslucentSortPriority(SortPriority);
	Mesh->RegisterComponent();
	const double ChunkUnits = McCoords::SectionSize * McCoords::UnitsPerBlock;
	Mesh->SetWorldLocation(FVector(Pos.X * ChunkUnits, Pos.Y * ChunkUnits, 0.0));
	Map.Add(Pos, Mesh);
	return Mesh;
}

void UMcWorldRenderComponent::Upload(FChunkResult& Result)
{
	auto Destroy = [&Result](TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Map)
	{
		TObjectPtr<UProceduralMeshComponent> Mesh;
		if (Map.RemoveAndCopyValue(Result.ChunkPos, Mesh) && Mesh)
		{
			Mesh->DestroyComponent();
		}
	};
	TranslucentChunks.Remove(Result.ChunkPos);
	if (Result.Sections.Num() == 0)
	{
		Destroy(Meshes);
		Destroy(WaterMeshes);
		return;
	}

	// Mesh section SectionIndex * passes + pass holds that pass of that chunk section; the water pass is in the
	// chunk's water mesh, the other passes in its block mesh.
	bool bAnyWater = false, bAnyBlocks = false;
	for (const FMcSectionMesh& Section : Result.Sections)
	{
		for (int32 Pass = 0; Pass < McNumRenderPasses; ++Pass)
		{
			if (!Section.Passes[Pass].IsEmpty())
			{
				(Pass == static_cast<int32>(EMcRenderPass::Water) ? bAnyWater : bAnyBlocks) = true;
			}
		}
	}
	UProceduralMeshComponent* BlockMesh = nullptr;
	UProceduralMeshComponent* WaterMesh = nullptr;
	if (bAnyBlocks)
	{
		BlockMesh = MakeMesh(Meshes, Result.ChunkPos, 0);
	}
	else
	{
		Destroy(Meshes);
	}
	if (bAnyWater)
	{
		WaterMesh = MakeMesh(WaterMeshes, Result.ChunkPos, -1);
	}
	else
	{
		Destroy(WaterMeshes);
	}

	const TArray<FVector2D> NoUVs;
	const TArray<FProcMeshTangent> NoTangents;
	for (const FMcSectionMesh& Section : Result.Sections)
	{
		for (int32 Pass = 0; Pass < McNumRenderPasses; ++Pass)
		{
			const FMcMeshBuffers& Built = Section.Passes[Pass];
			if (Built.IsEmpty())
			{
				continue;
			}
			UProceduralMeshComponent* Mesh = Pass == static_cast<int32>(EMcRenderPass::Water) ? WaterMesh : BlockMesh;
			const int32 MeshSection = Section.SectionIndex * McNumRenderPasses + Pass;
			FMcMeshBuffers Ordered;
			if (Pass == static_cast<int32>(EMcRenderPass::Translucent))
			{
				// Glass and ice go in farthest first, and are kept to be put in order again as the camera moves.
				const double ChunkUnits = McCoords::SectionSize * McCoords::UnitsPerBlock;
				const FVector View = GetViewLocation();
				Ordered = Built;
				McTranslucent::SortQuadsBackToFront(Ordered, View - FVector(Result.ChunkPos.X * ChunkUnits, Result.ChunkPos.Y * ChunkUnits, 0.0));
				FTranslucentChunk& Kept = TranslucentChunks.FindOrAdd(Result.ChunkPos);
				Kept.SortedFor = View;
				Kept.Sections.Add({ MeshSection, Ordered });
			}
			const FMcMeshBuffers& Buffers = Pass == static_cast<int32>(EMcRenderPass::Translucent) ? Ordered : Built;
			Mesh->CreateMeshSection(MeshSection, Buffers.Positions, Buffers.Triangles, Buffers.Normals, Buffers.UV0, Buffers.UV1, Buffers.UV2, NoUVs, Buffers.Colors, NoTangents, false);
			Mesh->SetMaterial(MeshSection, PassMaterials.IsValidIndex(Pass) ? PassMaterials[Pass].Get() : nullptr);
		}
	}
}
