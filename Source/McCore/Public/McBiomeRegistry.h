// Every biome in the jar's worldgen/biome data, numbered for compact storage in chunks. The numbers are
// ours (alphabetical by id), not Minecraft's, and only stable for one cache/.

#pragma once

#include "CoreMinimal.h"

using FMcBiomeId = uint8;

class MCCORE_API FMcBiomeRegistry
{
public:
	static FMcBiomeRegistry& Get();

	// Game thread, before anything reads it from other threads.
	bool EnsureLoaded();
	bool IsLoaded() const { return Names.Num() > 0; }

	int32 NumBiomes() const { return Names.Num(); }
	// "minecraft:plains" or "plains".
	TOptional<FMcBiomeId> Find(FName Biome) const;
	// Unknown ids give plains.
	FName GetName(FMcBiomeId Id) const;
	FMcBiomeId GetPlains() const { return Plains; }

private:
	TArray<FName> Names;
	TMap<FName, FMcBiomeId> Ids;
	FMcBiomeId Plains = 0;
};
