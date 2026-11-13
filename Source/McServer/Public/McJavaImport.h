// Importing a world saved by Minecraft Java Edition (minecraft.wiki: "Region file format", "Chunk format", "Java Edition
// level format", "NBT format") into one of our own: the chunks are read from the .mca files, their block states mapped onto
// the block registry by name and properties, and written back out through McChunkSave and FMcWorldStorage next to a
// level.json, so the imported world is an ordinary one that can be opened, played and saved. The vanilla folder is only
// ever read, never changed.
//
// Both folder layouts are read: the current one (dimensions/minecraft/overworld/region, data/minecraft/*.dat) and the
// older (region/, DIM-1, DIM1). Chunks are the modern kind (a `sections` list at the root; palettes of `Name` or `id`).

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcChunk;
class FMcEntityManager;
class FMcNbt;
class FMcWorldStorage;
struct FMcLevelData;
struct FMcPlayerData;

namespace McJavaImport
{
	struct FStats
	{
		int32 Regions = 0;
		int32 Chunks = 0;
		// Chunks that could not be read (damaged, or compressed in a way we don't read), and why, by compression byte.
		int32 ChunksFailed = 0;
		int64 Blocks = 0;
		// Of those, blocks that are not air (a check that nothing was read as air by mistake).
		int64 NonAirBlocks = 0;
		// Entities made into scenery in their chunk (item frames, glow item frames, paintings, armor stands: McDecor).
		int32 Scenery = 0;
		// Palette entries the registry does not know (name and properties as written), with how many blocks used them.
		TMap<FString, int64> UnknownStates;
		// Block entities that were too big to keep, by id.
		TMap<FString, int32> DroppedBlockEntities;
		int32 BlockEntities = 0;
		// Block entities kept whole as FMcBlockEntity::Raw, by id (signs, banners...), and entities of entities/*.mca
		// saved to <dimension>/javaentities, by id.
		TMap<FString, int32> KeptRawBlockEntities;
		TMap<FString, int32> Entities;
	};

	// The block that shows where something unknown was.
	MCSERVER_API FName GetPlaceholderBlock();

	// ---- Region files ----
	// The payload of chunk (LocalX, LocalZ) (0 to 31) of a region file already read into memory: the chunk's NBT, inflated.
	// RegionFolder is where an oversized chunk's c.<x>.<z>.mcc is looked for, ChunkX and ChunkZ its absolute chunk
	// coordinates. False for an empty slot or an unreadable chunk; bOutMissing tells the two apart (true: nothing stored).
	MCSERVER_API bool ReadChunkBytes(TConstArrayView<uint8> RegionFile, int32 LocalX, int32 LocalZ, const FString& RegionFolder,
		int32 ChunkX, int32 ChunkZ, TArray<uint8>& OutNbt, bool& bOutMissing);
	// Inflates a payload by its compression byte (1 gzip, 2 zlib, 3 none); false for others (LZ4 and custom are not read).
	MCSERVER_API bool Decompress(uint8 Compression, TConstArrayView<uint8> Data, TArray<uint8>& Out);

	// ---- Chunk contents ----
	// Bits per entry of a palette of this many entries: the smallest that holds the highest index, at least MinBits (4 for
	// blocks, 1 for biomes).
	MCSERVER_API int32 GetBitsPerEntry(int32 PaletteSize, int32 MinBits);
	// Unpacks Count entries of Bits bits each from packed longs: entry i is in long i / (64 / Bits), at bit
	// (i % (64 / Bits)) * Bits, never spanning two longs. False if there are too few longs.
	MCSERVER_API bool UnpackIndices(TConstArrayView<int64> Longs, int32 Bits, int32 Count, TArray<int32>& Out);

	// The state for a palette entry as written ("minecraft:oak_stairs" with properties facing=east and so on), or the
	// placeholder, counting it in Stats. A block the registry has with a property or value it doesn't know keeps the
	// properties that fit.
	MCSERVER_API FMcStateId MapState(const FString& Name, const TMap<FString, FString>& Properties, FStats& Stats, int64 Uses);

	// Fills Chunk (made for the same height) from a chunk's NBT. False if it isn't a chunk of this layout.
	MCSERVER_API bool ConvertChunk(const FMcNbt& Root, FMcChunk& Chunk, FStats& Stats);
	// The chunk's coordinates from its own NBT (xPos, zPos).
	MCSERVER_API bool GetChunkPos(const FMcNbt& Root, FIntPoint& OutPos);

	// ---- Worlds ----
	// Whether Folder looks like a Java world (has a level.dat).
	MCSERVER_API bool IsJavaWorld(const FString& Folder);
	// level.dat and its neighbours into the world's own level data. False if level.dat can't be read.
	MCSERVER_API bool ReadLevel(const FString& Folder, FMcLevelData& OutLevel);
	// A Java player file (the root of players/data/<id>.dat) into the world's own player data: place, look, mode, health,
	// food, experience, the inventory and ender chest by the game's slot numbers. False if it has no position.
	MCSERVER_API bool ConvertPlayer(const FMcNbt& Root, FMcPlayerData& Out);
	// A player's players/advancements/<id>.json into the player data: "advancement:" for each one done, "criterion:" for each
	// criterion met and "ctime:" for the game's date of it (Unix seconds). False if the text is not that file.
	MCSERVER_API bool ConvertAdvancements(const FString& Json, FMcPlayerData& Out);
	// A player's players/stats/<id>.json into "stat:<category>:<name>" counters (the category without "minecraft:"; a custom
	// statistic's name too). False if the text is not that file.
	MCSERVER_API bool ConvertStats(const FString& Json, FMcPlayerData& Out);
	// Writes the most recently played player of the Java folder into WorldDir as "imported-player" (the one a world's
	// players take when they have no file of their own). True if there was one.
	MCSERVER_API bool ImportPlayer(const FString& Source, const FString& WorldDir);
	// Imports the world in Source into Saved/Worlds/<new folder>, named Name (the vanilla level name if empty). Returns
	// the folder name made, or an empty string on failure.
	MCSERVER_API FString ImportWorld(const FString& Source, const FString& Name, FStats& OutStats);
	// Adds Other's counts to Stats.
	MCSERVER_API void AddStats(FStats& Stats, const FStats& Other);
	// A world to play at once: the same copy as ImportWorld, but only its level and a note of where the Java folder is are
	// made now (a moment, whatever the size). The chunks follow while the world is open (FLiveImport), the ones near the
	// spawn first and any the game asks for sooner at once. Returns the folder made, or an empty string on failure.
	MCSERVER_API FString BeginImport(const FString& Source, const FString& Name);

	struct FLiveState;

	// The rest of a world begun with BeginImport, for one dimension of it, on a thread of its own: regions nearest the
	// spawn first, each across all cores, into the dimension's storage. The storage also asks it for a chunk that is not
	// saved yet, so a chunk the player walks to before its turn comes at once. Finishing is noted in the dimension's
	// folder; a world closed before that picks up where it was left (chunks already saved are skipped).
	class MCSERVER_API FLiveImport : public TSharedFromThis<FLiveImport, ESPMode::ThreadSafe>
	{
	public:
		// Null when the world has no import going on for the dimension.
		static TSharedPtr<FLiveImport, ESPMode::ThreadSafe> Start(const FString& WorldDir, const FString& Dimension,
			TSharedPtr<FMcWorldStorage, ESPMode::ThreadSafe> Storage, FIntPoint SpawnChunk);
		~FLiveImport();
		// Ends the thread after its current region and stops answering for the storage. Call before the storage goes.
		void Stop();
		bool IsFinished() const;
		// Chunks seen and to see, over every dimension being imported; false if nothing is.
		static bool GetProgress(int32& OutDone, int32& OutTotal);

	private:
		FLiveImport();
		TSharedPtr<FLiveState, ESPMode::ThreadSafe> State;
	};
	// ---- Using what was kept ----
	// Stands a flag in every banner block of the chunk that has none yet, patterned as the imported banner was (the
	// patterns of its kept block entity, which the game does not read otherwise). Returns how many it stood.
	MCSERVER_API int32 StandBanners(const FMcChunk& Chunk, FMcEntityManager& Manager);
	// What an entity of the game's NBT is as scenery: the McDecor line for an item frame, glow item frame, painting or armor
	// stand, where it stands and its yaw. False for any other entity.
	MCSERVER_API bool GetSceneryLine(const FMcNbt& Entity, FString& OutLine, FVector& OutPos, float& OutYaw);
	// A summary for the log.
	MCSERVER_API FString Describe(const FStats& Stats);
}
