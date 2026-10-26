// Superflat worlds from the jar's worldgen/flat_level_generator_preset/ data.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcChunk;

struct FMcFlatLayer
{
	FMcStateId State = 0;
	int32 Height = 1;
};

struct MCGEN_API FMcFlatSettings
{
	FName Biome;
	TArray<FMcFlatLayer> Layers;  // bottom to top, starting at the dimension's lowest block

	// Loads a preset by id, e.g. "minecraft:classic_flat". Needs the block registry loaded.
	static bool LoadPreset(const FString& Id, FMcFlatSettings& Out);
};

// Stateless once built, so chunks can be generated on any thread.
class MCGEN_API FMcFlatGenerator
{
public:
	explicit FMcFlatGenerator(const FMcFlatSettings& InSettings);

	void Generate(FMcChunk& Chunk) const;

	// The height of the top block of the layers, given the dimension's lowest block.
	int32 GetSurfaceY(int32 MinY) const;

private:
	FMcFlatSettings Settings;
};
