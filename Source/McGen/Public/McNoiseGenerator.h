// Terrain from a worldgen/noise_settings file: the density graph's final_density decides solid or not, cell
// by cell, and the sea fills what's open below sea level. Biomes come from the router's climate functions
// through a multi-noise biome source.
//
// Surface rules then turn the stone into grass, sand, deepslate, bedrock and ore veins.
//
// Aquifers fill open space underground with water, lava or air (and barrier stone between them).
//
// Still to come (see PLAN.md, M4): carvers, features and structures.

#pragma once

#include "CoreMinimal.h"
#include "McChunkGenerator.h"
#include "McCore.h"
#include "McAquifer.h"
#include "McBiomeSource.h"
#include "McDensityFunction.h"
#include "McSurfaceRules.h"

struct MCGEN_API FMcNoiseSettings
{
	FString Id;
	int32 MinY = -64;
	int32 Height = 384;
	int32 SeaLevel = 63;
	FMcStateId DefaultBlock = 0;
	FMcStateId DefaultFluid = 0;
	// The router's functions, as JSON (a name or an inline function).
	TMap<FString, TSharedPtr<FJsonValue>> Router;
	TSharedPtr<FJsonValue> SurfaceLevel;
	// The "aquifers" object, or null for none.
	TSharedPtr<FJsonValue> Aquifers;
	// The material_rule to apply, e.g. "minecraft:overworld"; empty for none.
	FString SurfaceRule;

	// Reads worldgen/noise_settings/<name>.json, e.g. "minecraft:overworld". Needs the block registry.
	static bool Load(const FString& Id, FMcNoiseSettings& Out);
};

class MCGEN_API FMcNoiseGenerator : public FMcChunkGenerator
{
public:
	// BiomePreset names the multi-noise parameter list, e.g. "minecraft:overworld". Needs the block and biome
	// registries loaded.
	FMcNoiseGenerator(const FMcNoiseSettings& InSettings, uint64 Seed, const FString& BiomePreset);

	// False if any part of the graph failed to load.
	bool IsValid() const { return bValid; }

	virtual void Generate(FMcChunk& Chunk) const override;
	virtual int32 FindSurfaceY(int32 X, int32 Z) const override;

	const FMcDensityGraph& GetGraph() const { return Graph; }
	// A router function by name (e.g. "continents", "final_density"), or INDEX_NONE.
	int32 GetRouterFunction(const FString& Name) const;

	// The climate and biome at a block, outside chunk generation (slower).
	FMcClimate SampleClimate(int32 X, int32 Y, int32 Z) const;
	FMcBiomeId GetBiome(int32 X, int32 Y, int32 Z) const;
	// The aquifers' and surface rules' estimate of the terrain height.
	double GetPreliminarySurface(int32 X, int32 Z) const;

private:
	FMcClimate SampleClimate(FMcDensityContext& Context) const;
	void FillBiomes(FMcChunk& Chunk, FMcDensityContext& Context) const;
	void ApplySurface(FMcChunk& Chunk, TArray<TArray<FMcStateId>>& Sections, const TArray<int32>& PreliminarySurface, FMcDensityContext& Context) const;

	FMcNoiseSettings Settings;
	FMcBiomeSource Biomes;
	int32 Climate[6] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	FMcDensityGraph Graph;
	FMcSurfaceRules SurfaceRules;
	FMcAquifer Aquifer;
	bool bAquifers = false;
	bool bSurfaceRules = false;
	TMap<FString, int32> Router;
	int32 FinalDensity = INDEX_NONE;
	int32 SurfaceLevel = INDEX_NONE;
	FMcStateId Water = 0;
	FMcStateId Lava = 0;
	FMcStateId Air = 0;
	bool bValid = false;
};
