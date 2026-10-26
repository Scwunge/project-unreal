// The block and block state registry, built from the data generator's blocks.json report.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Misc/Optional.h"
#include "McCore.h"

struct FMcBlockProperty
{
	FName Name;            // e.g. "facing"
	TArray<FName> Values;  // e.g. "north", "south", "west", "east", in the report's order
};

struct MCCORE_API FMcBlock
{
	FName Name;                             // e.g. "minecraft:oak_stairs"
	int32 Index = INDEX_NONE;               // position in the registry, in the report's order
	TArray<FMcBlockProperty> Properties;
	TArray<FMcStateId> States;              // every state of this block, in the report's order
	FMcStateId DefaultState = 0;

	int32 FindProperty(FName PropertyName) const;
};

struct FMcBlockState
{
	FMcStateId Id = 0;
	int32 Block = INDEX_NONE;
	// For each of the block's properties, the index of this state's value in FMcBlockProperty::Values.
	TArray<uint8, TInlineAllocator<8>> Values;
};

// Loaded once on the game thread, then read only, so worker threads (meshing, generation) may read it freely.
class MCCORE_API FMcBlockRegistry
{
public:
	static FMcBlockRegistry& Get();

	// Loads blocks.json and the #minecraft:air block tag from cache/. Does nothing if already loaded.
	bool EnsureLoaded();
	bool LoadFromReport(const FString& BlocksJsonPath, const FString& AirTagPath);
	bool IsLoaded() const { return Blocks.Num() > 0; }

	int32 NumBlocks() const { return Blocks.Num(); }
	int32 NumStates() const { return States.Num(); }

	const FMcBlock* FindBlock(FName Name) const;
	const FMcBlock& GetBlock(int32 Index) const { return Blocks[Index]; }

	bool IsValidState(FMcStateId Id) const { return States.IsValidIndex(Id) && States[Id].Block != INDEX_NONE; }
	const FMcBlockState& GetState(FMcStateId Id) const { return States[Id]; }
	const FMcBlock& GetBlockOf(FMcStateId Id) const { return Blocks[States[Id].Block]; }

	// True for the blocks in the #minecraft:air tag: air, cave_air and void_air.
	bool IsAir(FMcStateId Id) const { return AirStates[Id]; }

	// The block's default state, or unset if there is no such block.
	TOptional<FMcStateId> FindDefaultState(FName BlockName) const;

	// The state with these property values; properties left out keep their default value.
	TOptional<FMcStateId> FindState(FName BlockName, const TMap<FName, FName>& Properties) const;

	// Parses the block state syntax commands use: "stone", "minecraft:oak_stairs[facing=east,half=top]".
	TOptional<FMcStateId> ParseState(const FString& Text) const;

	// The value of one property of a state, or NAME_None if the block doesn't have that property.
	FName GetPropertyValue(FMcStateId Id, FName PropertyName) const;

	// "minecraft:oak_stairs[facing=north,half=top,shape=straight,waterlogged=true]"
	FString Describe(FMcStateId Id) const;

private:
	TArray<FMcBlock> Blocks;
	TMap<FName, int32> BlocksByName;
	TArray<FMcBlockState> States;  // indexed by state id
	TBitArray<> AirStates;         // indexed by state id
};
