// A world of ours written back as a Java save, the way McJavaImport reads one (minecraft.wiki: "Java Edition level
// format", "Region file format", "Chunk format"): blocks and biomes of every saved chunk in the current layout
// (dimensions/<namespace>/<name>/region/r.X.Z.mca), and level.dat. Block entities, entities and the player are not
// written yet (see PLAN.md).

#pragma once

#include "CoreMinimal.h"

class FMcChunk;
class FMcNbt;

namespace McJavaExport
{
	// The game's world version for the cache's game version 26.3 (version.json: world_version).
	constexpr int32 DataVersion = 5023;

	struct FStats
	{
		int32 Chunks = 0;
		int32 Regions = 0;
		int32 Failed = 0;
	};

	// A chunk as the game's chunk NBT: sections with palettes of block states and biomes, packed as the game packs them.
	MCSERVER_API void MakeChunk(const FMcChunk& Chunk, FMcNbt& Out, int64 LastUpdate = 0);

	// Packs Indexes (each below the palette's size) into longs of Bits bits, none spanning two longs; the inverse of
	// McJavaImport::UnpackIndices.
	MCSERVER_API void PackIndices(TConstArrayView<int32> Indexes, int32 Bits, TArray<int64>& OutLongs);

	// A region file (anvil) from chunks' NBT as bytes (uncompressed, root and all), by their chunk coordinates.
	MCSERVER_API void MakeRegion(const TMap<FIntPoint, TArray<uint8>>& Chunks, TArray<uint8>& OutFile);

	// Writes WorldDir (a world of ours) as a Java save into OutFolder. False if the world can't be read or OutFolder
	// can't be written.
	MCSERVER_API bool ExportWorld(const FString& WorldDir, const FString& OutFolder, FStats& OutStats);
}
