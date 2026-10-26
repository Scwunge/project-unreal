// Structures (minecraft.wiki, "Structure", "Structure set"): the buildings, ruins and wrecks scattered over the
// world, from the jar's worldgen/structure_set (where they may start: a grid of cells with one start in each) and
// worldgen/structure (what and in which biomes).
//
// A structure starts in one chunk, decided from the world seed and that chunk alone, so any chunk can tell which
// starts reach it. When a chunk is decorated each start chunk in the 3x3 around it is looked at, and the pieces
// inside the centre chunk are put down. That is the game's "structures" stage, before the feature steps.
//
// Built so far: igloo, desert pyramid, jungle temple, swamp hut, shipwreck, ocean ruin, ruined portal, buried
// treasure. The rest (jigsaw villages and so on, mineshafts, strongholds ...) are read and left out, with one log
// line naming them. The desert pyramid, jungle temple and swamp hut have no template in the jar (the game builds
// them in code), so they are simplified buildings written from the wiki's descriptions.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "McBiomeRegistry.h"
#include "McCore.h"
#include "McJigsaw.h"

class FJsonObject;
class FMcFeatureLibrary;
class FMcGenRegion;

class MCGEN_API FMcStructureLibrary
{
public:
	FMcStructureLibrary(int32 InSeaLevel, int32 InMinY);

	// Reads the structure sets, structures and biome tags, and the template pools the jigsaw structures grow
	// from. Game thread; read only after that. Features gives the pools' feature pieces (flowers, trees) their
	// placed features; it may be null.
	bool Load(FMcFeatureLibrary* Features = nullptr);

	// How jigsaw structures find the ground over columns far from the chunk being decorated: the height (one above
	// the top block) the terrain has there, before features, e.g.
	// [Generator](int32 X, int32 Z) { return Generator.FindSurfaceY(X, Z) + 1; }. Without it they assume the ground
	// is as high as at the start. Set before decorating.
	void SetHeightSource(TFunction<int32(int32, int32)> Source) { HeightSource = MoveTemp(Source); }

	// The biome at a block, with the terrain's own biome source: what decides whether a structure that reaches
	// far from its start chunk (a village) is there at all, since the chunks round it aren't in the region. e.g.
	// [Generator](int32 X, int32 Y, int32 Z) { return Generator.GetBiome(X, Y, Z); }. Set after the height source
	// and before decorating; without it only starts within a chunk of the region are seen.
	void SetBiomeSource(TFunction<FMcBiomeId(int32, int32, int32)> Source) { BiomeSource = MoveTemp(Source); }

	// Puts down the parts of structures that lie in the region's centre chunk. The 3x3 chunks must be generated.
	void Place(FMcGenRegion& Region, uint64 Seed) const;

	// The structures (by id, "minecraft:igloo") whose set starts one in this chunk, whatever the biome and ground
	// (a chunk can have several).
	void GetCandidates(uint64 Seed, FIntPoint Chunk, TArray<FName>& Out) const;
	// The same with the biome checked at the chunk's middle, using the region's biomes (the chunk must be in it).
	void GetStarts(FMcGenRegion& Region, uint64 Seed, FIntPoint Chunk, TArray<FName>& Out) const;

	// Builds one structure with its start in this chunk, whatever the sets and biomes say (for tests and tools).
	// Returns false for an unknown or unbuilt structure.
	bool PlaceStructure(FMcGenRegion& Region, FName Structure, uint64 Seed, FIntPoint Start) const;
	// Whether the structure's biome tag lists the biome.
	bool BiomeAllows(FName Structure, FMcBiomeId Biome) const;

	// The jigsaw layout last made for a structure starting here (null if none was), for tests and tools.
	TSharedPtr<FMcJigsawLibrary::FLayout> FindLayout(FName Structure, uint64 Seed, FIntPoint Start) const;

	int32 NumSets() const { return Sets.Num(); }
	int32 NumStructures() const { return Structures.Num(); }
	// Structure types read but not built, with how many structures use each.
	const TMap<FString, int32>& GetUnsupported() const { return Unsupported; }

private:
	struct FStructure;
	struct FSet;

	bool IsStartChunk(const FSet& Set, uint64 Seed, FIntPoint Chunk) const;
	const FStructure* Choose(const FSet& Set, uint64 Seed, FIntPoint Chunk) const;
	bool BiomeFits(FMcGenRegion& Region, const FStructure& Structure, FIntPoint Chunk, bool bInRegion) const;
	bool IsFlatEnough(FIntPoint Start) const;
	void Build(FMcGenRegion& Region, const FStructure& Structure, uint64 Seed, FIntPoint Start, int32 Salt) const;
	bool IsExcluded(const FSet& Set, uint64 Seed, FIntPoint Chunk) const;
	bool ResolveTag(const FString& Tag, TBitArray<>& Out, int32 Depth) const;

	int32 SeaLevel;
	int32 MinY;
	TArray<TSharedPtr<FStructure>> Structures;
	TArray<TSharedPtr<FSet>> Sets;
	TMap<FString, int32> Unsupported;
	TSharedPtr<FMcJigsawLibrary> Jigsaws;
	FMcFeatureLibrary* FeatureLibrary = nullptr;
	TFunction<int32(int32, int32)> HeightSource;
	TFunction<FMcBiomeId(int32, int32, int32)> BiomeSource;
	// Jigsaw layouts by seed, start chunk and structure, newest last.
	mutable FCriticalSection LayoutLock;
	mutable TMap<FString, TSharedPtr<FMcJigsawLibrary::FLayout>> Layouts;
	mutable TArray<FString> LayoutOrder;
	// Template ids by use.
	TArray<FString> WarmRuins;
	TArray<FString> WarmBigRuins;
	TArray<FString> ColdRuins;
	TArray<FString> ColdBigRuins;
	TArray<FString> Shipwrecks;
};
