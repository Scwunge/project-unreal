// A dimension type from the jar's data/minecraft/dimension_type/: the world's height and sky rules.

#pragma once

#include "CoreMinimal.h"

struct MCCORE_API FMcDimensionType
{
	FName Name;
	int32 MinY = -64;   // lowest block, a multiple of 16
	int32 Height = 384; // blocks from MinY up, a multiple of 16
	bool bHasSkylight = true;
	bool bHasCeiling = false;
	// The Nether and the End keep one time of day for ever: no day or night, so nothing burns in the daylight.
	bool bHasFixedTime = false;
	// The height up to which things are built and players travel to (128 in the Nether, whose roof is above it).
	int32 LogicalHeight = 384;
	// How far one block of this dimension is in the Overworld (8 for the Nether), for travelling through portals.
	double CoordinateScale = 1.0;
	// "overworld", "end" or "none": what is drawn as the sky.
	FString Skybox = TEXT("overworld");
	// The least light the dimension has anywhere, 0 to 1.
	double AmbientLight = 0.0;

	int32 MaxY() const { return MinY + Height - 1; }
	int32 NumSections() const { return Height / 16; }

	// Loads e.g. "minecraft:overworld" from cache/. Returns false (and leaves Out unchanged) on failure.
	static bool Load(const FString& Id, FMcDimensionType& Out);
};
