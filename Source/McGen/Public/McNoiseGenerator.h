// Terrain from a worldgen/noise_settings file: the density graph's final_density decides solid or not, cell
// by cell, and the sea fills what's open below sea level.
//
// Still to come (see PLAN.md, M4): aquifers and ore veins, biomes, surface rules, carvers, features and
// structures. Until aquifers, open space below sea level is water above the column's preliminary surface
// and air below it, with lava under y -54 like the game's deep lava level.

#pragma once

#include "CoreMinimal.h"
#include "McChunkGenerator.h"
#include "McCore.h"
#include "McDensityFunction.h"

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

	// Reads worldgen/noise_settings/<name>.json, e.g. "minecraft:overworld". Needs the block registry.
	static bool Load(const FString& Id, FMcNoiseSettings& Out);
};

class MCGEN_API FMcNoiseGenerator : public FMcChunkGenerator
{
public:
	FMcNoiseGenerator(const FMcNoiseSettings& InSettings, uint64 Seed);

	// False if any part of the graph failed to load.
	bool IsValid() const { return bValid; }

	virtual void Generate(FMcChunk& Chunk) const override;
	virtual int32 FindSurfaceY(int32 X, int32 Z) const override;

	const FMcDensityGraph& GetGraph() const { return Graph; }
	// A router function by name (e.g. "continents", "final_density"), or INDEX_NONE.
	int32 GetRouterFunction(const FString& Name) const;

private:
	FMcNoiseSettings Settings;
	FMcDensityGraph Graph;
	TMap<FString, int32> Router;
	int32 FinalDensity = INDEX_NONE;
	int32 SurfaceLevel = INDEX_NONE;
	FMcStateId Water = 0;
	FMcStateId Lava = 0;
	FMcStateId Air = 0;
	bool bValid = false;
};
