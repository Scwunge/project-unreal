// Every biome in the jar's worldgen/biome data, numbered for compact storage in chunks. The numbers are
// ours (alphabetical by id), not Minecraft's, and only stable for one cache/.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

using FMcBiomeId = uint8;

// One line of a biome's natural spawns (the gameplay/natural_mob_spawns attribute): a kind, its weight against the
// others in the category, and how many come in a group.
struct FMcSpawnEntry
{
	FName Kind;
	int32 Weight = 0;
	int32 MinCount = 1;
	int32 MaxCount = 1;
};

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

	// What spawns naturally in a biome, by category ("monster", "creature", "ambient" and so on); empty for a
	// category the biome has none of.
	TConstArrayView<FMcSpawnEntry> GetSpawns(FMcBiomeId Id, FName Category) const;

	// Reads the spawn lists out of a biome file's object (for tests; EnsureLoaded does this for every biome).
	static TMap<FName, TArray<FMcSpawnEntry>> ParseSpawns(const FJsonObject& Biome);

private:
	TArray<FName> Names;
	TArray<TMap<FName, TArray<FMcSpawnEntry>>> Spawns;
	TMap<FName, FMcBiomeId> Ids;
	FMcBiomeId Plains = 0;
};
