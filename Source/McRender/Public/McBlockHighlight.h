// The outline round the block the player aims at, and the cracks that grow over the block being dug. The player's
// pawn tells it each frame what is targeted and how far the dig has gone; it draws that in block coordinates (the
// world's, not the camera's).

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "McBlockHighlight.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

namespace McHighlight
{
	// The ten crack pictures are destroy_stage_0..9; the dig's progress (0 to 1) picks one. -1 for no digging.
	MCRENDER_API int32 GetCrackStage(float Progress);

	// The outline of a box as twelve thin bars, in block coordinates: positions and triangles appended to the arrays.
	// Bars are Width thick and the box is grown by Grow on every side, so they do not fight the block's own faces.
	MCRENDER_API void AddOutline(const FBox& Box, double Width, double Grow, TArray<FVector>& Positions, TArray<int32>& Triangles);

	// The six faces of a box, each with the whole picture, as quads with their own vertices.
	MCRENDER_API void AddFaces(const FBox& Box, double Grow, TArray<FVector>& Positions, TArray<int32>& Triangles, TArray<FVector2D>& UVs);
}

UCLASS()
class MCRENDER_API UMcBlockHighlightComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMcBlockHighlightComponent(const FObjectInitializer& ObjectInitializer);

	// What is aimed at: the block, and the boxes of its shape in the block's own space (0 to 1). Nothing to hide it.
	// ViewDistance is how far the eye is from it, in blocks, to keep the line the same width on screen.
	void SetTarget(const TOptional<FIntVector>& Block, const TArray<FBox>& Shape, double ViewDistance);
	// The block being dug and its crack stage (see GetCrackStage); stage -1 for none.
	void SetDig(const FIntVector& Block, int32 Stage);

private:
	void Rebuild();
	UMaterialInstanceDynamic* GetMaterial(const FString& Path);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OpaqueMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TranslucentMaterial;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> Materials;

	uint32 MaterialGeneration = 0;
	TOptional<FIntVector> Block;
	TArray<FBox> Shape;
	FIntVector DigBlock = FIntVector::ZeroValue;
	int32 Stage = -1;
	double BarWidth = 0.01;
	bool bDirty = true;
};
