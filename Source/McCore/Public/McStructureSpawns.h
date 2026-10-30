// What a structure says about the mobs in and around it (the "spawn_overrides" of worldgen/structure, and the
// mobs a piece puts there when it is built: a swamp hut's witch). The generator, which knows where structures
// stand, writes it as it builds them; the mob spawner, which knows nothing about structures, reads it.

#pragma once

#include "CoreMinimal.h"
#include "McBiomeRegistry.h"

// A box in which a structure's own mobs replace the biome's of a category.
struct FMcStructureZone
{
	FName Structure;
	// Inclusive block corners.
	FIntVector Min = FIntVector::ZeroValue;
	FIntVector Max = FIntVector::ZeroValue;
	TArray<FMcSpawnEntry> Monsters;
	TArray<FMcSpawnEntry> Creatures;

	bool Contains(const FIntVector& Cell) const
	{
		return Cell.X >= Min.X && Cell.X <= Max.X && Cell.Y >= Min.Y && Cell.Y <= Max.Y && Cell.Z >= Min.Z && Cell.Z <= Max.Z;
	}
};

// A mob that comes with a structure when it is built, once.
struct FMcStructureMob
{
	FName Kind;
	FVector Position = FVector::ZeroVector;
};

class MCCORE_API FMcStructureSpawns
{
public:
	static FMcStructureSpawns& Get();

	// Adds a zone; one with the same structure and corners is kept once (a structure is built a chunk at a time).
	void AddZone(const FMcStructureZone& Zone);
	// Adds a mob unless the key (structure and start) has put its mobs here already.
	void AddMob(const FString& Key, const FMcStructureMob& Mob);

	// The zone whose monster list is the one for this block, null if none.
	bool FindMonsterZone(const FIntVector& Cell, FMcStructureZone& Out) const;
	// The zones with monsters whose box is within Radius blocks (across) of the point.
	void GetMonsterZonesNear(double X, double Z, double Radius, TArray<FMcStructureZone>& Out) const;
	// Hands each waiting mob to Take, which returns true when it has been used (so it is dropped).
	void TakeMobs(TFunctionRef<bool(const FMcStructureMob&)> Take);
	// Forgets everything (a new world, or a test).
	void Clear();

private:
	mutable FCriticalSection Lock;
	TArray<FMcStructureZone> Zones;
	TArray<FMcStructureMob> Mobs;
	TSet<FString> MobKeys;
};
