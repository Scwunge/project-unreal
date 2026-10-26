// Superflat worlds from the jar's worldgen/flat_level_generator_preset/ data.

#pragma once

#include "CoreMinimal.h"
#include "McChunkGenerator.h"
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
class MCGEN_API FMcFlatGenerator : public FMcChunkGenerator
{
public:
	FMcFlatGenerator(const FMcFlatSettings& InSettings, int32 InMinY);

	virtual void Generate(FMcChunk& Chunk) const override;
	virtual int32 FindSurfaceY(int32 X, int32 Z) const override;

private:
	FMcFlatSettings Settings;
	int32 MinY = 0;
};
