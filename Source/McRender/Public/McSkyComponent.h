// The sky: a dome that follows the camera and shades from the fog colour at the horizon to the sky colour overhead,
// with the sunrise and sunset glow, stars, and the sun and moon from the jar's pictures, all driven by the time of
// day (FMcDayCycle). It also fogs the distance in the fog colour and says how much of the sky's light reaches the
// blocks, which the block materials read.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "McSkyComponent.generated.h"

struct FMcDimensionType;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UVolumetricCloudComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTexture2DArray;

namespace McSky
{
	// How much of the sky's stored light (the 0-15 in the chunks) the materials let through now: 1 by day, 0.24 at
	// night. The chunks' light never changes with the time; this is applied when drawing.
	MCRENDER_API float GetSkyLightFactor();
	MCRENDER_API void SetSkyLightFactor(float Factor);

	// How open to the sky the camera is, 1 outdoors to 0 under a roof (the world renderer measures it): Unreal Mode opens
	// the exposure up in caves, over a second or two, as an eye adjusts.
	MCRENDER_API float GetCameraOpen();
	MCRENDER_API void SetCameraOpen(float Open);

	// 1 when the camera is under water (the world renderer measures it), else 0. Unreal Mode thickens the fog to the
	// sea's blue-green, dims the sun and lets its light dance as caustics.
	MCRENDER_API float GetCameraUnderwater();
	MCRENDER_API void SetCameraUnderwater(float Under);

	// The vanilla cloud layer for Unreal Mode's volumetric clouds, from the cloud component: the pattern as a texture
	// (one texel per 12 block cell, alpha 255 where there is cloud), how far (blocks) it has drifted west, and the layer's
	// height in blocks. A null mask (no clouds in this dimension, or no pattern) leaves the engine's own clouds.
	MCRENDER_API void SetCloudLayer(UTexture2D* Mask, float DriftBlocks, float HeightBlocks);

	// A sky or fog colour (display values) under rain and thunder (0 to 1 each; thunder as heavy as the rain is):
	// greyed and darkened towards its own brightness, as the sky goes grey in a storm (minecraft.wiki: "Rain").
	MCRENDER_API FLinearColor ApplyWeather(const FLinearColor& Colour, float Rain, float Thunder);
	// The sky light factor dimmed by the weather: rain takes 5/16 of the daylight above the night's, thunder as much
	// again of what is left (the wiki's full day light of 15 down to 12 in rain).
	MCRENDER_API float ApplyWeatherToLight(float Factor, float Rain, float Thunder);
}

UCLASS(ClassGroup = (Mc))
class MCRENDER_API UMcSkyComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMcSkyComponent(const FObjectInitializer& ObjectInitializer);

	// Where the time of day comes from, in ticks (see AMcWorldActor::GetDayTime).
	TFunction<double()> GetTime;

	// How hard it rains and thunders now, 0 to 1 each (see AMcWorldActor::GetRainLevel). Unset is clear.
	TFunction<FVector2D()> GetWeather;

	// A lightning flash: the sky and the blocks under it light up for a moment.
	void Flash();

	// The distance at which the fog is thick (the view distance), in blocks.
	float FogEndBlocks = 192.0f;

	// Which sky to draw (the dimension type's skybox): the day cycle in the Overworld; in the Nether a fog-coloured nothing
	// with no sun, moon or stars; in the End the end sky on a cube, in the dark. Call before or after it is in the world.
	void SetDimension(const FMcDimensionType& Type);

	// The colours as of the last tick, in display values: what the horizon looks like and what the fog is.
	FLinearColor GetCurrentSkyColor() const { return SkyColor; }
	FLinearColor GetCurrentFogColor() const { return FogColor; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void BuildDome();
	void BuildStars();
	void UpdateDome(double Ticks);
	void UpdateStars(double Ticks);
	void UpdateCelestial(double Ticks);
	void UpdateFog();
	void BuildEndSky();
	// The enhanced look (McRenderMode): the light, post-process and fog switched over, and the sun or moon's light
	// following them across the sky.
	void BuildEnhanced();
	void ApplyLook();
	void UpdateSunLight(double Ticks);
	UProceduralMeshComponent* MakeMesh(const TCHAR* Name);

	enum class EMode : uint8
	{
		Overworld,
		Nether,
		End,
	};
	EMode Mode = EMode::Overworld;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> EndSky;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2DArray> EndSkyTexture;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Dome;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Skirt;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Stars;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Sun;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Moon;
	UPROPERTY(Transient)
	TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> SunLight;

	// Unreal Mode's dusk and dawn: a soft blue light from the side away from the sun, so the ground is not black while the sky
	// still glows (the sun has set and the moon is not up yet).
	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> TwilightFill;
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> EnhancedPost;
	UPROPERTY(Transient)
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(Transient)
	TObjectPtr<UVolumetricCloudComponent> Clouds;
	UPROPERTY(Transient)
	TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CloudMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> McCloudMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> McCloudInstance;
	uint32 LookGeneration = 0;
	float BaseExposure = 0.0f;
	float CaveExposure = 0.0f;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DomeMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> AdditiveMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SunMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MoonMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2DArray>> MoonTextures;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2DArray> SunTexture;

	// The dome's unit directions in Minecraft axes, one per vertex, and the stars'.
	TArray<FVector> DomeDirections;
	int32 NumStarVertices = 0;
	TArray<float> StarWeights;  // 0 to 1 per star: how bright it is relative to the rest

	FLinearColor SkyColor = FLinearColor::Black;
	FLinearColor FogColor = FLinearColor::Black;
	float StarBrightness = 0.0f;
	float Rain = 0.0f;
	float Thunder = 0.0f;
	// Seconds of lightning flash left.
	float FlashTime = 0.0f;
	int32 ShownMoonPhase = INDEX_NONE;
	bool bReady = false;
};
