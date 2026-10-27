// Rain and snow round the camera (minecraft.wiki: "Rain", "Snow"): in each column within ten blocks where the sky is open
// and the biome has weather, a sheet of the jar's rain.png (or snow.png where it is cold enough) stands facing the camera,
// from ten blocks below the eyes (or the ground, if higher) to ten above, falling as its picture scrolls down and fading
// towards the edge of the circle. Rain falls straight and fast; snow slowly, drifting.
//
// It also makes the rain's sound: now and then a drop lands on a random open column near the camera, weather.rain on
// the ground (volume 0.2) or the quieter, lower weather.rain.above on a roof over the player's head.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "McWeatherComponent.generated.h"

class FMcBlockWorld;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2DArray;

namespace McWeather
{
	// Columns within this many blocks of the camera have rain drawn in them.
	constexpr int32 Radius = 10;

	// The heights a column's sheet spans for the camera at CameraY over ground whose top block is at Top: from ten below
	// the camera (or above the ground) to ten above it (or above the ground). Empty (Min == Max) where the ground is
	// above all of it.
	MCRENDER_API FInt32Interval GetColumnSpan(int32 CameraY, int32 Top);
	// How opaque a column's sheet is DistanceSquared (blocks squared, horizontally) from the camera, at full rain:
	// 1 at the camera, a half at the edge of the circle, none beyond it.
	MCRENDER_API float GetFade(float DistanceSquared);
}

UCLASS(ClassGroup = (Mc))
class MCRENDER_API UMcWeatherComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMcWeatherComponent(const FObjectInitializer& ObjectInitializer);

	// The blocks the rain falls on (the biome, and where the sky is open).
	void SetBlockWorld(const FMcBlockWorld* InWorld) { BlockWorld = InWorld; }
	// How hard it rains, 0 to 1 (see AMcWorldActor::GetRainLevel).
	TFunction<float()> GetRain;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	// The top block that stops rain in a column (anything with a collision box, or a fluid), or below the world.
	int32 FindTop(int32 X, int32 Z) const;
	void PlayRainSound(const FIntVector& Camera, float Rain);

	const FMcBlockWorld* BlockWorld = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> WeatherMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RainMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SnowMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2DArray> RainTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2DArray> SnowTexture;

	// Each column's top block, looked up again every few ticks (blocks change, the camera moves).
	TMap<FIntPoint, int32> Tops;
	float TopsAge = 0.0f;
	// Seconds since the rain began to be drawn: what the pictures scroll by.
	float Age = 0.0f;
	float SoundRemainder = 0.0f;
	bool bShown = false;
	FRandomStream Random{ 23 };
};
