// Every biome in the jar's worldgen/biome data, numbered for compact storage in chunks. The numbers are
// ours (alphabetical by id), not Minecraft's, and only stable for one cache/.

#pragma once

#include "CoreMinimal.h"
#include "McDimensionType.h"

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

	// The biome's climate (minecraft.wiki: "Biome", "Rain"): its temperature, which falls by 0.00125 a block above y 80,
	// and what falls from its sky at a height: nothing where the biome has no precipitation (deserts, savannas), snow
	// below a temperature of 0.15, rain otherwise.
	enum class EPrecipitation : uint8 { None, Rain, Snow };
	float GetTemperature(FMcBiomeId Id, int32 Y) const;
	EPrecipitation GetPrecipitation(FMcBiomeId Id, int32 Y) const;

	// The biome's own background music of a kind ("default", "creative"), which takes the place of the dimension's;
	// null where it has none.
	const FMcDimensionType::FMusic* GetMusic(FMcBiomeId Id, FName Kind) const;
	// All of them (empty for a biome with none).
	const TMap<FName, FMcDimensionType::FMusic>& GetMusicLists(FMcBiomeId Id) const;

	// Reads the spawn lists out of a biome file's object (for tests; EnsureLoaded does this for every biome).
	static TMap<FName, TArray<FMcSpawnEntry>> ParseSpawns(const FJsonObject& Biome);

private:
	TArray<FName> Names;
	TArray<TMap<FName, TArray<FMcSpawnEntry>>> Spawns;
	TArray<float> Temperatures;
	TArray<bool> Precipitation;
	TArray<TMap<FName, FMcDimensionType::FMusic>> Music;
	TMap<FName, FMcBiomeId> Ids;
	FMcBiomeId Plains = 0;
};
