// The clouds (minecraft.wiki: "Cloud"): the jar's clouds.png laid over the sky at the dimension's cloud height, each of
// its pixels a 12 by 12 block cell that is a cloud where the pixel is, four blocks thick, as fancy clouds are. They drift
// west with the world's time, so every player sees them in the same place; shaded by face, see-through, dimmed at night
// with the sky light and greyed by rain.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "McCloudComponent.generated.h"

struct FMcDimensionType;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
class UTexture2DArray;

namespace McClouds
{
	// Blocks per pixel of clouds.png, and how tall a cloud is.
	constexpr float CellSize = 12.0f;
	constexpr float Thickness = 4.0f;
	// Blocks a tick the clouds drift west.
	constexpr double DriftPerTick = 0.03;
	// How far the pattern's rows sit past the world's z origin, measured under the reference (cloudph: the row boundary
	// crossed at z -3.9 +- 0.3 blocks, about a third of a cell).
	constexpr double ZOffset = 3.9;
	// Cells drawn out from the camera's, each way.
	constexpr int32 Radius = 24;

	// How far (blocks, towards the west) the pattern has drifted at a time of day, within one repeat of the picture
	// (Width pixels across).
	MCRENDER_API double GetDrift(double Ticks, int32 Width);
	// The pixel of a Width x Height picture a cell (counted from the picture's corner, any integer) takes: it repeats.
	MCRENDER_API FIntPoint WrapCell(FIntPoint Cell, int32 Width, int32 Height);
}

UCLASS(ClassGroup = (Mc))
class MCRENDER_API UMcCloudComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMcCloudComponent(const FObjectInitializer& ObjectInitializer);

	// The world's time in ticks, and how hard it rains and thunders (0 to 1 each).
	TFunction<double()> GetTime;
	TFunction<FVector2D()> GetWeather;
	// Takes the cloud height and colour; a dimension without clouds draws none.
	void SetDimension(const FMcDimensionType& Type);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	void Rebuild(FIntPoint Centre, const FLinearColor& Colour);
	bool IsCloud(FIntPoint Cell) const;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> CloudMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2DArray> White;
	// The pattern as a texture for the volumetric clouds of Unreal Mode: alpha 255 in a cloud, one texel per cell.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Mask;

	TArray<bool> Pattern;
	int32 PatternWidth = 0;
	int32 PatternHeight = 0;
	TOptional<float> Height;
	FLinearColor BaseColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.8f);
	// The cell the mesh was built round, and the colour it was built in.
	TOptional<FIntPoint> BuiltCentre;
	FLinearColor BuiltColour = FLinearColor::Transparent;
};
