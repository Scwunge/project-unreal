#include "McBlockHighlight.h"
#include "McCache.h"
#include "McChunk.h"
#include "McRenderMode.h"
#include "McRenderTextures.h"
#include "Engine/Texture2DArray.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace McHighlight
{
	int32 GetCrackStage(float Progress)
	{
		if (Progress <= 0.0f)
		{
			return -1;
		}
		return FMath::Clamp(FMath::FloorToInt(Progress * 10.0f), 0, 9);
	}

	namespace
	{
		// A quad seen from both sides: corners in order round it.
		void AddQuad(TArray<FVector>& Positions, TArray<int32>& Triangles, TArray<FVector2D>* UVs, const FVector* P)
		{
			const int32 First = Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Positions.Add(P[Corner]);
			}
			if (UVs)
			{
				UVs->Append({ FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) });
			}
			Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
		}

		void AddFaceQuads(const FBox& Box, TArray<FVector>& Positions, TArray<int32>& Triangles, TArray<FVector2D>* UVs)
		{
			const FVector A = Box.Min, B = Box.Max;
			const FVector Down[4] = { FVector(A.X, A.Y, A.Z), FVector(B.X, A.Y, A.Z), FVector(B.X, B.Y, A.Z), FVector(A.X, B.Y, A.Z) };
			const FVector Up[4] = { FVector(A.X, A.Y, B.Z), FVector(A.X, B.Y, B.Z), FVector(B.X, B.Y, B.Z), FVector(B.X, A.Y, B.Z) };
			const FVector North[4] = { FVector(A.X, A.Y, A.Z), FVector(A.X, A.Y, B.Z), FVector(B.X, A.Y, B.Z), FVector(B.X, A.Y, A.Z) };
			const FVector South[4] = { FVector(A.X, B.Y, A.Z), FVector(B.X, B.Y, A.Z), FVector(B.X, B.Y, B.Z), FVector(A.X, B.Y, B.Z) };
			const FVector West[4] = { FVector(A.X, A.Y, A.Z), FVector(A.X, B.Y, A.Z), FVector(A.X, B.Y, B.Z), FVector(A.X, A.Y, B.Z) };
			const FVector East[4] = { FVector(B.X, A.Y, A.Z), FVector(B.X, A.Y, B.Z), FVector(B.X, B.Y, B.Z), FVector(B.X, B.Y, A.Z) };
			for (const FVector* Face : { Down, Up, North, South, West, East })
			{
				AddQuad(Positions, Triangles, UVs, Face);
			}
		}
	}

	void AddOutline(const FBox& Box, double Width, double Grow, TArray<FVector>& Positions, TArray<int32>& Triangles)
	{
		const FBox Grown = Box.ExpandBy(Grow);
		const double H = Width * 0.5;
		// Each of the twelve edges is a thin box along it, centred on the edge.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				// The edge's position on the two other axes: one of the box's four corners looked along Axis.
				const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
				FVector Min = Grown.Min, Max = Grown.Max;
				const double Cu = (Corner & 1) ? Grown.Max[U] : Grown.Min[U];
				const double Cv = (Corner & 2) ? Grown.Max[V] : Grown.Min[V];
				Min[U] = Cu - H;
				Max[U] = Cu + H;
				Min[V] = Cv - H;
				Max[V] = Cv + H;
				Min[Axis] -= H;
				Max[Axis] += H;
				AddFaceQuads(FBox(Min, Max), Positions, Triangles, nullptr);
			}
		}
	}

	void AddFaces(const FBox& Box, double Grow, TArray<FVector>& Positions, TArray<int32>& Triangles, TArray<FVector2D>& UVs)
	{
		AddFaceQuads(Box.ExpandBy(Grow), Positions, Triangles, &UVs);
	}
}

UMcBlockHighlightComponent::UMcBlockHighlightComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bUseAsyncCooking = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cutout(TEXT("/Game/Mc/Materials/M_McBlockCutout.M_McBlockCutout"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Translucent(TEXT("/Game/Mc/Materials/M_McBlockTranslucent.M_McBlockTranslucent"));
	OpaqueMaterial = Cutout.Object;
	TranslucentMaterial = Translucent.Object;
}

void UMcBlockHighlightComponent::SetTarget(const TOptional<FIntVector>& InBlock, const TArray<FBox>& InShape)
{
	if (Block != InBlock || Shape != InShape)
	{
		Block = InBlock;
		Shape = InShape;
		bDirty = true;
	}
}

void UMcBlockHighlightComponent::SetDig(const FIntVector& InBlock, int32 InStage)
{
	if (DigBlock != InBlock || Stage != InStage)
	{
		DigBlock = InBlock;
		Stage = InStage;
		bDirty = true;
	}
	if (bDirty || MaterialGeneration != McRenderMode::GetGeneration())
	{
		Rebuild();
	}
}

UMaterialInstanceDynamic* UMcBlockHighlightComponent::GetMaterial(const FString& Path)
{
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Path))
	{
		return *Found;
	}
	const bool bCrack = Path.Contains(TEXT("destroy_stage"));
	UMaterialInterface* Base = bCrack ? TranslucentMaterial.Get() : OpaqueMaterial.Get();
	int32 Width = 0, Height = 0;
	UTexture2DArray* Array = Base ? McRenderTextures::LoadPngAsArray(this, Path, Width, Height) : nullptr;
	if (!Array)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(McRenderMode::Pick(Base), this);
	Material->SetTextureParameterValue(TEXT("Blocks"), Array);
	McRenderMode::ApplyHdPicture(Material, Path);
	Materials.Add(Path, Material);
	return Material;
}

void UMcBlockHighlightComponent::Rebuild()
{
	if (MaterialGeneration != McRenderMode::GetGeneration())
	{
		Materials.Reset();
		MaterialGeneration = McRenderMode::GetGeneration();
	}
	bDirty = false;
	ClearAllMeshSections();

	const TArray<FVector2D> NoUVs;
	const TArray<FProcMeshTangent> NoTangents;
	auto Make = [&](int32 Index, const TArray<FVector>& Positions, const TArray<int32>& Triangles, const TArray<FVector2D>& UVs, const FColor& Color, const FString& Path)
	{
		if (Positions.Num() == 0)
		{
			return;
		}
		UMaterialInstanceDynamic* Material = GetMaterial(Path);
		if (!Material)
		{
			return;
		}
		TArray<FVector> World;
		World.Reserve(Positions.Num());
		TArray<FVector> Normals;
		Normals.Init(FVector::UpVector, Positions.Num());
		TArray<FVector2D> UV1, UV2;
		UV1.Init(FVector2D::ZeroVector, Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Positions.Num());
		TArray<FColor> VertexColors;
		VertexColors.Init(Color, Positions.Num());
		for (const FVector& Position : Positions)
		{
			World.Add(McCoords::BlockToUnreal(Position));
		}
		TArray<FVector2D> Texture = UVs;
		if (Texture.Num() != Positions.Num())
		{
			Texture.Init(FVector2D(0.5, 0.5), Positions.Num());
		}
		CreateMeshSection(Index, World, Triangles, Normals, Texture, UV1, UV2, NoUVs, VertexColors, NoTangents, false);
		SetMaterial(Index, Material);
	};

	// The outline: dark bars (a block's black, one pixel wide in vanilla) round each box of the targeted block's shape.
	if (Block.IsSet())
	{
		TArray<FVector> Positions;
		TArray<int32> Triangles;
		const FVector Origin(Block.GetValue());
		for (const FBox& Box : Shape)
		{
			McHighlight::AddOutline(FBox(Origin + Box.Min, Origin + Box.Max), 0.012, 0.003, Positions, Triangles);
		}
		Make(0, Positions, Triangles, NoUVs, FColor(0, 0, 0, 255), McCache::Asset(TEXT("textures/block/black_concrete.png")));
	}
	// The cracks: the stage's picture over every face of the block being dug.
	if (Stage >= 0 && Stage <= 9)
	{
		TArray<FVector> Positions;
		TArray<int32> Triangles;
		TArray<FVector2D> UVs;
		const FVector Origin(DigBlock);
		McHighlight::AddFaces(FBox(Origin, Origin + FVector(1.0)), 0.002, Positions, Triangles, UVs);
		Make(1, Positions, Triangles, UVs, FColor::White, McCache::Asset(FString::Printf(TEXT("textures/block/destroy_stage_%d.png"), Stage)));
	}
}
