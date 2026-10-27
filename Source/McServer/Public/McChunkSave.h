// A chunk as saved to a region file. Unlike McNet's chunk codec (which sends raw state and biome numbers, good for
// one session), numbers here are only ever an index into a palette of names written with the chunk, so a save
// outlives a change to the registries' numbering (a new cache/, a different jar).
//
//   byte     version
//   int32    min Y, byte number of sections
//   palette  block states as text ("minecraft:oak_stairs[facing=east,...]"), then biomes as text
//   sections each uniform (one palette index) or full (4096 palette indexes)
//   biomes   one palette index per 4x4x4 cell
//   rest     populated flag, block entities, entity bytes (McEntityManager's, opaque here)
//
// all zlib-compressed behind a version byte and the uncompressed size.

#pragma once

#include "CoreMinimal.h"

class FMcChunk;

// What goes into a saved chunk besides its blocks and biomes.
struct MCSERVER_API FMcChunkSaveExtras
{
	// Whether the chunk's features (trees, ores and so on) have been placed. A chunk that is not will be decorated
	// when it is loaded, as when it is generated.
	bool bPopulated = false;
	// The mobs and items in the chunk, as FMcEntityManager wrote them. Kept as bytes so this file needn't know them.
	TArray<uint8> Entities;
};

namespace McChunkSave
{
	// Empty if the registries aren't loaded or compression failed.
	MCSERVER_API TArray<uint8> Encode(const FMcChunk& Chunk, const FMcChunkSaveExtras& Extras);

	// Fills Chunk (made with the same height as the one saved) and Extras. False if the bytes are damaged or were
	// saved for a different height, leaving Chunk unchanged. Names the registries don't know load as air and the
	// default biome.
	MCSERVER_API bool Decode(TConstArrayView<uint8> Bytes, FMcChunk& Chunk, FMcChunkSaveExtras& Extras);
}
