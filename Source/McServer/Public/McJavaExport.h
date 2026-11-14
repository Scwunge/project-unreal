// A world of ours written back as a Java save, the way McJavaImport reads one (minecraft.wiki: "Java Edition level
// format", "Region file format", "Chunk format"): blocks and biomes of every saved chunk in the current layout
// (dimensions/<namespace>/<name>/region/r.X.Z.mca), their block entities and light, level.dat and the world generation
// settings, the players (players/data) and the game rules.

#pragma once

#include "CoreMinimal.h"

class FMcChunk;
class FMcNbt;
struct FMcPlayerData;
struct FMcLightRegion;

namespace McJavaExport
{
	// The game's world version for the cache's game version 26.3 (version.json: world_version).
	constexpr int32 DataVersion = 5023;

	struct FStats
	{
		int32 Chunks = 0;
		int32 Regions = 0;
		int32 Failed = 0;
		// Chunks whose entities went to the entities folder.
		int32 EntityChunks = 0;
		// Player files written (players/data/<uuid>.dat).
		int32 Players = 0;
		// Seconds spent in all, and of those lighting chunks.
		double Seconds = 0.0;
		double LightSeconds = 0.0;
		// Of the rest: building the chunk's tags (with lighting counted within), writing them out, and compressing a region.
		double MakeSeconds = 0.0;
		double WriteSeconds = 0.0;
		double RegionSeconds = 0.0;
	};

	// A player as the game's player file (Pos, Rotation, Inventory with the worn pieces under equipment, EnderItems,
	// recipeBook, ...), from the port's player data. Id is the name the port knew them by (their file name there).
	MCSERVER_API void MakePlayer(const FMcPlayerData& Player, FMcNbt& Out);
	// The UUID text vanilla gives a player of that name in a world with no account servers ("OfflinePlayer:<name>", version 3).
	MCSERVER_API FString OfflineUuid(const FString& Name);

	// A chunk as the game's chunk NBT: sections with palettes of block states and biomes, packed as the game packs them.
	MCSERVER_API void MakeChunk(const FMcChunk& Chunk, FMcNbt& Out, int64 LastUpdate = 0, const FMcLightRegion* Light = nullptr);

	// Packs Indexes (each below the palette's size) into longs of Bits bits, none spanning two longs; the inverse of
	// McJavaImport::UnpackIndices.
	MCSERVER_API void PackIndices(TConstArrayView<int32> Indexes, int32 Bits, TArray<int64>& OutLongs);

	// A region file (anvil) from chunks' NBT as bytes (uncompressed, root and all), by their chunk coordinates.
	MCSERVER_API void MakeRegion(const TMap<FIntPoint, TArray<uint8>>& Chunks, TArray<uint8>& OutFile);

	// Writes WorldDir (a world of ours) as a Java save into OutFolder. False if the world can't be read or OutFolder
	// can't be written. With bWriteLight each chunk gets the sky and block light our engine works out for it (as the game saves
	// it, so the game has no need to light the chunk again).
	MCSERVER_API bool ExportWorld(const FString& WorldDir, const FString& OutFolder, FStats& OutStats, bool bWriteLight = true);
}
