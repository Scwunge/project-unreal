// Block tags from the jar's data/minecraft/tags/block: named sets of blocks ("minecraft:logs",
// "minecraft:replaceable") that worldgen, heightmaps and survival rules test against. Tags may include other
// tags ("#minecraft:oak_logs"); those are resolved when loading.
//
// Loaded once on the game thread, then read only, so worker threads may read it freely.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "McCore.h"

class MCCORE_API FMcBlockTags
{
public:
	static FMcBlockTags& Get();

	// Loads every block tag in cache/ (and the block registry). Does nothing if already loaded.
	bool EnsureLoaded();
	bool IsLoaded() const { return bLoaded; }

	// A tag by name, with or without the namespace or a leading '#'; INDEX_NONE if there is no such tag.
	int32 Find(const FString& Tag) const;
	int32 NumTags() const { return Members.Num(); }

	// Whether a state's block is in the tag. False for INDEX_NONE.
	bool Has(int32 Tag, FMcStateId State) const;
	bool HasBlock(int32 Tag, int32 BlockIndex) const;
	// Convenience for code that names its tag; slower (a map lookup).
	bool Has(const FString& Tag, FMcStateId State) const { return Has(Find(Tag), State); }

private:
	bool Resolve(const FString& Name, TSet<FString>& Visiting);

	TMap<FString, int32> Ids;
	TArray<TBitArray<>> Members;  // by tag, indexed by block index
	TMap<FString, TArray<FString>> Pending;
	// The tags read since the last (re)load, and whether /reload is hooked.
	TSet<FString> Resolved;
	bool bHooked = false;
	bool bLoaded = false;
};
