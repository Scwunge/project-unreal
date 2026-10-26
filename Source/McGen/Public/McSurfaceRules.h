// Surface rules from the jar's worldgen/material_rule and material_condition data: the decision tree that
// turns the stone of raw terrain into grass, dirt, sand, snow, terracotta bands, deepslate, bedrock and ore
// veins (minecraft.wiki, "Surface rule").
//
// Where the wiki leaves details open:
// - Stone depth counts from 1 at the first solid block under air or fluid (floor) or over a cavity
//   (ceiling); stone_depth passes at depths up to 1 + offset (+ surface depth) (+ secondary depth).
// - water passes without water above, or when y + offset (+ ...) reaches the water's height (the top
//   fluid block's y + 1).
// - y_above passes when y (+ stone depth) is at or above the anchor (+ surface depth times the multiplier).
// - noise_threshold samples the noise at (x, 0, z), both bounds inclusive.
// - steep: the column two to the south is 4 or more higher than the one to the north, or the one to the
//   west 4 or more higher than the one to the east (north and east faces of mountains).
// - ore_vein: where its density is above 0, a random roll under the density makes a vein block: ore (raw
//   ore by raw_ore_chance) when a roll is under the richness and filler_gap is below 0, else the filler.
// - bandlands: our own 192-block band pattern per seed, shifted by the clay_bands_offset noise.

#pragma once

#include "CoreMinimal.h"
#include "McBiomeRegistry.h"
#include "McCore.h"
#include "McNoise.h"
#include "McRandom.h"

class FJsonValue;
class FJsonObject;
class FMcDensityContext;
class FMcDensityGraph;

// What a rule can know about the block it is deciding.
struct FMcSurfacePoint
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;
	FMcBiomeId Biome = 0;
	int32 StoneDepthAbove = 0;
	int32 StoneDepthBelow = 0;
	// The y just above the water over this block, or MIN_int32 without water above.
	int32 WaterHeight = MIN_int32;
	// Per column.
	int32 SurfaceDepth = 0;
	double SecondaryDepth = 0.0;  // -1..1
	int32 MinSurfaceLevel = 0;
	bool bSteep = false;
	FMcDensityContext* Density = nullptr;
	// Per-column 2D noise values, one per NumNoises(), filled on first use; NaN until then. Reset (to NaN)
	// for each new column.
	TArray<double>* NoiseCache = nullptr;
};

class MCGEN_API FMcSurfaceRules
{
public:
	// Compiles a rule (e.g. "minecraft:overworld") into Graph, which keeps the density functions ore veins
	// use. MinY and Height bound vertical anchors. Needs the block and biome registries loaded.
	bool Load(const FString& Id, FMcDensityGraph& InGraph, uint64 Seed, int32 InMinY, int32 InHeight);

	// The block for a default-block (stone) position, or INDEX_NONE to leave it.
	int32 Apply(FMcSurfacePoint& Point) const;

	int32 NumNoises() const { return Noises.Num(); }
	// The column values every rule shares.
	int32 GetSurfaceDepth(int32 X, int32 Z) const;
	double GetSecondaryDepth(int32 X, int32 Z) const;

private:
	enum class ECondition : uint8
	{
		Biome, NoiseThreshold, VerticalGradient, YAbove, Water, Steep, Not, Hole, AbovePreliminarySurface, StoneDepth,
	};
	struct FCondition
	{
		ECondition Type = ECondition::Hole;
		TArray<FMcBiomeId> Biomes;
		int32 Noise = INDEX_NONE;
		double Min = 0.0;
		double Max = 0.0;
		int32 AnchorA = 0;   // y_above's anchor, gradient's true_at_and_below
		int32 AnchorB = 0;   // gradient's false_at_and_above
		uint64 RandomSeed = 0;
		int32 Offset = 0;
		int32 SurfaceDepthMultiplier = 0;
		int32 SecondaryRange = 0;
		bool bAddStoneDepth = false;
		bool bAddSurfaceDepth = false;
		bool bCeiling = false;
		bool bIs3d = false;
		int32 Inner = INDEX_NONE;  // not
	};

	enum class ERule : uint8
	{
		Block, Sequence, Condition, Bandlands, OreVein,
	};
	struct FRule
	{
		ERule Type = ERule::Block;
		FMcStateId State = 0;
		TArray<int32> Children;     // sequence
		int32 If = INDEX_NONE;      // condition
		int32 Then = INDEX_NONE;
		// Ore veins: density functions in the graph and blocks.
		int32 Density = INDEX_NONE;
		int32 Gap = INDEX_NONE;
		int32 Richness = INDEX_NONE;
		FMcStateId Ore = 0;
		FMcStateId RawOre = 0;
		FMcStateId Filler = 0;
		double RawChance = 0.0;
		uint64 RandomSeed = 0;
	};

	int32 ParseRule(const TSharedPtr<FJsonValue>& Json);
	int32 ParseCondition(const TSharedPtr<FJsonValue>& Json);
	int32 LoadNamed(const FString& Folder, const FString& Id, TMap<FString, int32>& Named, bool bRule);
	int32 GetNoise(const FString& Id);
	int32 ParseAnchor(const TSharedPtr<FJsonObject>& Json) const;
	TOptional<FMcStateId> ParseState(const TSharedPtr<FJsonValue>& Json) const;

	bool Test(int32 Index, FMcSurfacePoint& Point) const;
	int32 Run(int32 Index, FMcSurfacePoint& Point) const;
	double NoiseAt(int32 Noise, FMcSurfacePoint& Point) const;
	FMcStateId GetBand(int32 X, int32 Y, int32 Z) const;

	FMcDensityGraph* Graph = nullptr;
	uint64 WorldSeed = 0;
	int32 MinY = 0;
	int32 Height = 0;
	TArray<FCondition> Conditions;
	TArray<FRule> Rules;
	TMap<FString, int32> NamedRules;
	TMap<FString, int32> NamedConditions;
	TArray<FMcNoise> Noises;
	TMap<FString, int32> NoiseIds;
	int32 SurfaceNoise = INDEX_NONE;
	int32 SecondaryNoise = INDEX_NONE;
	int32 BandOffsetNoise = INDEX_NONE;
	TArray<FMcStateId> Bands;
	int32 Root = INDEX_NONE;
	bool bErrors = false;
};
