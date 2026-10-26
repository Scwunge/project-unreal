// Features: the trees, ores, flowers, springs and lakes placed after the terrain, from the jar's
// worldgen/feature (configured features) and worldgen/placed_feature (where and how often)
// (minecraft.wiki, "Configured feature", "Placed feature").
//
// A placed feature runs its placement modifiers on a start position (the chunk's corner) to get the
// positions to try, then places its configured feature at each. Feature types are created through a registry,
// so each type can live in its own file (RegisterType); a type we haven't written yet places nothing and is
// listed by GetUnsupported().

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "McBiomeRegistry.h"
#include "McCore.h"

class FJsonObject;
class FJsonValue;
class FMcFeatureLibrary;
class FMcGenRegion;
class FMcPlacedFeature;
class FMcNoise;
class FMcRandom;
class FMcStructureLibrary;

// Everything a feature may use while placing.
struct FMcFeatureContext
{
	FMcGenRegion& Region;
	FMcRandom& Random;
	const FMcFeatureLibrary& Library;
	int32 SeaLevel = 63;
	// The placed feature the decorator started with, for the biome filter; null to skip that filter.
	const FMcPlacedFeature* TopLevel = nullptr;
};

class MCGEN_API FMcFeature
{
public:
	virtual ~FMcFeature() = default;
	// True if anything was placed.
	virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const = 0;
};

class MCGEN_API FMcPlacementModifier
{
public:
	virtual ~FMcPlacementModifier() = default;
	// Adds the positions Pos turns into (none to drop it).
	virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const = 0;
};

class MCGEN_API FMcPlacedFeature
{
public:
	FString Id;  // empty for inline ones
	TSharedPtr<const FMcFeature> Feature;
	TArray<TSharedPtr<const FMcPlacementModifier>> Modifiers;

	// Runs the modifiers from Origin and places the feature at every position left. True if any placed.
	bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const;
};

// Parses and keeps configured and placed features by id, and decorates chunks with the features their biomes
// list. Load on the game thread (it reads files and the registries); after LoadBiomes it is read only, so
// worker threads may decorate with one library.
class MCGEN_API FMcFeatureLibrary
{
public:
	using FFactory = TFunction<TSharedPtr<const FMcFeature>(const FJsonObject& Json, FMcFeatureLibrary& Library)>;

	// Feature types by name ("minecraft:tree"). The built-in ones register themselves.
	static void RegisterType(const FString& Type, FFactory Factory);

	explicit FMcFeatureLibrary(int32 InSeaLevel = 63, int32 InMinY = -64, int32 InHeight = 384);

	// A placed feature by id ("minecraft:trees_plains"), or null.
	TSharedPtr<const FMcPlacedFeature> GetPlaced(const FString& Id);
	// A placed feature written as an id or inline ({ "feature": ..., "placement": [...] }).
	TSharedPtr<const FMcPlacedFeature> ParsePlaced(const TSharedPtr<FJsonValue>& Json);
	// A configured feature written as an id or inline ({ "type": ..., ... }).
	TSharedPtr<const FMcFeature> ParseFeature(const TSharedPtr<FJsonValue>& Json);

	// Reads every biome's feature lists (one per decoration step) and temperature, and parses those features.
	bool LoadBiomes();
	// Places the features of the biomes in and around the region's centre chunk, step by step, like the
	// game's decoration stage. The region's 3x3 chunks must all be generated.
	void Decorate(FMcGenRegion& Region, uint64 Seed) const;

	// Whether a biome lists a placed feature (the biome placement filter).
	bool BiomeHas(FMcBiomeId Biome, const FMcPlacedFeature* Feature) const;
	// The biome's temperature at a height: it falls by 0.00125 a block above y 80 (minecraft.wiki, "Biome").
	float GetTemperature(FMcBiomeId Biome, int32 Y) const;
	// The 2D noise the count placements read (noise_threshold_count, noise_based_count).
	const FMcNoise& GetInfoNoise() const { return *InfoNoise; }

	int32 GetSeaLevel() const { return SeaLevel; }
	int32 GetMinY() const { return MinY; }
	int32 GetHeight() const { return Height; }
	// Feature types that came up but aren't written yet, with how often.
	const TMap<FString, int32>& GetUnsupported() const { return Unsupported; }
	bool HasErrors() const { return bErrors; }

private:
	TSharedPtr<const FMcPlacementModifier> ParseModifier(const TSharedPtr<FJsonValue>& Json);

	int32 SeaLevel;
	int32 MinY;
	int32 Height;
	TMap<FString, TSharedPtr<const FMcPlacedFeature>> Placed;
	TMap<FString, TSharedPtr<const FMcFeature>> Configured;
	TMap<FString, int32> Unsupported;
	bool bErrors = false;

	// Per decoration step, every feature any biome lists there, in the order biomes list them.
	TArray<TArray<TSharedPtr<const FMcPlacedFeature>>> Steps;
	// Per placed feature, the biomes listing it (bit per biome id).
	TMap<const FMcPlacedFeature*, TBitArray<>> Biomes;
	TArray<float> Temperatures;
	TSharedPtr<FMcNoise> InfoNoise;
	// The structures placed before the features of a chunk (loaded with the biomes).
	TSharedPtr<FMcStructureLibrary> Structures;
};

// Helpers features share.
namespace McFeature
{
	// Places State, and the upper half above it for two-block-tall plants. False if it can't stay there.
	MCGEN_API bool PlacePlant(FMcGenRegion& Region, const FIntVector& Pos, FMcStateId State);
	// Whether a block may be replaced by features (not in #features_cannot_replace).
	MCGEN_API bool CanReplace(FMcStateId State);
}
