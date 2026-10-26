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

	int32 MaxY() const { return MinY + Height - 1; }
	int32 NumSections() const { return Height / 16; }

	// Loads e.g. "minecraft:overworld" from cache/. Returns false (and leaves Out unchanged) on failure.
	static bool Load(const FString& Id, FMcDimensionType& Out);
};
