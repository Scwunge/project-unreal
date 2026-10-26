// Minecraft's sky light and block light, computed from scratch for one chunk column from the blocks of it and
// its eight neighbours. Light travels at most 15 blocks, so a 3x3 group of columns is enough to light the
// middle column, plus a one-block border, exactly.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "McBiomeRegistry.h"
#include "McCore.h"

class FMcBlockWorld;

// How each state takes part in lighting, copied into flat arrays for the worker threads.
struct MCWORLD_API FMcLightPalette
{
	TArray<uint8> Emission;    // indexed by state id, 0..15
	TArray<uint8> Dampening;   // indexed by state id, 0..15; 15 stops light
	TBitArray<> SkyDown;       // indexed by state id: full sky light passes straight down without loss

	// Game thread, after FMcBlockLight has loaded.
	static TSharedRef<const FMcLightPalette> Build();
};

struct MCWORLD_API FMcLightRegion
{
	static constexpr int32 Size = 48;    // three chunk columns across
	static constexpr int32 Border = 16;  // where the middle column starts, in region coordinates

	FIntPoint CenterChunk;
	int32 MinY = 0;    // world height of the region's bottom layer
	int32 Height = 0;  // layers held; everything above is open sky with no blocks

	TArray<FMcStateId> Blocks;  // Size * Size * Height, see Index()
	TArray<uint8> Sky;          // filled by Compute
	TArray<uint8> BlockLight;   // filled by Compute
	// Biome cells (4x4x4 blocks) over the whole world height, for tints. Missing columns hold NoBiome.
	TArray<FMcBiomeId> Biomes;
	int32 BiomeLayers = 0;
	static constexpr FMcBiomeId NoBiome = 255;

	// X and Z are region coordinates (0..47), Y is the layer above MinY.
	FORCEINLINE int32 Index(int32 X, int32 Y, int32 Z) const { return (Y * Size + Z) * Size + X; }

	// Game thread. Covers the world's full height up to one section above the highest non-empty section of
	// the nine columns. Missing columns count as air.
	static FMcLightRegion Capture(const FMcBlockWorld& World, FIntPoint CenterChunk);

	// Any thread.
	void Compute(const FMcLightPalette& Palette);

	// X and Z in region coordinates, Y a world height. Above the region is full sky; below the world, dark.
	FMcStateId GetBlock(int32 X, int32 Y, int32 Z) const;
	uint8 GetSkyLight(int32 X, int32 Y, int32 Z) const;
	uint8 GetBlockLight(int32 X, int32 Y, int32 Z) const;
	// X and Z in region coordinates, Y a world height (clamped to the world). NoBiome where nothing loaded.
	FMcBiomeId GetBiome(int32 X, int32 Y, int32 Z) const;

private:
	void Spread(TArray<uint8>& Light, TArray<int32>& Queue, const FMcLightPalette& Palette) const;
};
