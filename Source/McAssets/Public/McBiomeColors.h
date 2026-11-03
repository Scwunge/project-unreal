// Biome tints: the grass and foliage colormaps, and each biome's climate and colour overrides from the jar's worldgen data.

#pragma once

#include "CoreMinimal.h"
#include "McAssets.h"

// A 256x256 colormap indexed by temperature (columns, hot on the left) and downfall (rows, wet at the top).
class MCASSETS_API FMcColormap
{
public:
	bool Load(const FString& PngPath);
	bool IsLoaded() const { return Pixels.Num() > 0; }

	// Both inputs are clamped to 0..1; the downfall is scaled by the temperature, as the wiki describes.
	FColor Sample(float Temperature, float Downfall) const;

private:
	int32 Width = 0;
	int32 Height = 0;
	TArray<FColor> Pixels;
};

enum class EMcGrassModifier : uint8
{
	None,
	DarkForest,  // already applied to FMcBiomeColors::Grass
	Swamp,       // the game picks one of two colours per position from noise; Grass holds one of them
};

struct FMcBiomeColors
{
	float Temperature = 0.8f;
	float Downfall = 0.4f;
	FColor Grass = FColor::White;
	FColor Foliage = FColor::White;
	FColor DryFoliage = FColor::White;
	FColor Water = FColor::White;
	EMcGrassModifier GrassModifier = EMcGrassModifier::None;
};

// Loaded once on the game thread, then read only.
class MCASSETS_API FMcBiomeColorTable
{
public:
	static FMcBiomeColorTable& Get();

	bool EnsureLoaded();
	bool IsLoaded() const { return Biomes.Num() > 0; }

	int32 NumBiomes() const { return Biomes.Num(); }
	// The resolved colours of a biome ("minecraft:plains" or "plains"), or null if there is no such biome.
	const FMcBiomeColors* Find(FName Biome) const;
	// Like Find, but unknown biomes get plains' colours.
	FMcBiomeColors Get(FName Biome) const;

	const FMcColormap& GetGrassColormap() const { return GrassMap; }
	const FMcColormap& GetFoliageColormap() const { return FoliageMap; }
	const FMcColormap& GetDryFoliageColormap() const { return DryFoliageMap; }

private:
	FMcColormap GrassMap;
	FMcColormap FoliageMap;
	FMcColormap DryFoliageMap;
	TMap<FName, FMcBiomeColors> Biomes;
	uint32 LoadedGeneration = 0;
};
