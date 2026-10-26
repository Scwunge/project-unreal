// Picks biomes from climate, the way the game's multi-noise biome source does (minecraft.wiki, "Biome"):
// every biome entry is a box in the six climate parameters plus an offset, and the biome whose box is
// nearest to the sampled climate wins, where distance is the squared distance to each parameter's range,
// summed, plus the offset squared.
//
// The entries come from the jar's report (cache/data/reports/biome_parameters), since the overworld's
// parameter list lives in code rather than data. A bounding-volume tree keeps lookups fast.

#pragma once

#include "CoreMinimal.h"
#include "McBiomeRegistry.h"

// Temperature, humidity (the router's vegetation), continentalness, erosion, depth, weirdness (ridges).
struct FMcClimate
{
	double Values[6] = {};
};

class MCGEN_API FMcBiomeSource
{
public:
	// Reads reports/biome_parameters/<namespace>/<name>.json, e.g. "minecraft:overworld". Needs the biome
	// registry loaded.
	bool Load(const FString& Preset);

	int32 NumEntries() const { return Entries.Num(); }

	// The nearest biome. Hint is the entry found last time (start with INDEX_NONE); neighbouring cells
	// usually share it, which makes the search much quicker.
	FMcBiomeId Find(const FMcClimate& Climate, int32& InOutHint) const;
	// The same by checking every entry, for tests.
	FMcBiomeId FindSlow(const FMcClimate& Climate) const;

private:
	static constexpr int32 NumAxes = 7;  // the six parameters and the offset

	struct FBox
	{
		float Min[NumAxes];
		float Max[NumAxes];
		double Distance(const double Point[NumAxes]) const;
	};
	struct FEntry
	{
		FBox Box;
		FMcBiomeId Biome = 0;
	};
	struct FNode
	{
		FBox Bounds;
		// Children are nodes [First, First + Count) when not a leaf, else entries.
		int32 First = 0;
		int32 Count = 0;
		bool bLeaf = false;
	};

	int32 Build(int32 First, int32 Count);

	TArray<FEntry> Entries;
	TArray<FNode> Nodes;
	int32 Root = INDEX_NONE;
};
