// One file of a dimension's saved chunks: a 32 x 32 square of them, found by a header of offsets, each stored as a
// length-prefixed, checksummed payload (McChunkSave's bytes). Our own layout, not vanilla's anvil:
//
//   bytes 0-3    "MCRG"
//   bytes 4-7    version (1)
//   bytes 8...   1024 entries of 16 bytes, one per chunk in z-then-x order: offset and size in 4 KiB sectors, the
//                payload's length in bytes and its CRC-32. A length of 0 is a chunk not saved.
//   sector 5...  payloads, each starting on a sector boundary.
//
// A chunk written again keeps its place if the new payload fits the sectors it has, else goes to the end of the
// file (its old sectors are left unused until the file is compacted; there is no compaction yet). Integers are
// little-endian. Safe to use from several threads: calls on one file take turns.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class MCSERVER_API FMcRegionFile
{
public:
	static constexpr int32 Width = 32;
	static constexpr int32 SectorSize = 4096;
	static constexpr int32 HeaderSectors = 5;
	static constexpr int32 MaxPayload = 16 * 1024 * 1024;

	// The region (of 32 x 32 chunks) a chunk falls in, rounding towards negative infinity.
	static FIntPoint RegionOf(FIntPoint Chunk) { return FIntPoint(Chunk.X >> 5, Chunk.Y >> 5); }
	static FString FileNameOf(FIntPoint Region) { return FString::Printf(TEXT("r.%d.%d.mcr"), Region.X, Region.Y); }

	// The file need not exist yet: it is made by the first write. Nothing is opened until it's used.
	explicit FMcRegionFile(FString InPath);
	UE_NONCOPYABLE(FMcRegionFile);

	// Chunk positions are world chunk coordinates; only their place within the region is used, so pass chunks of
	// this file's region. False if the chunk isn't saved here or its bytes fail their checksum.
	bool Read(FIntPoint Chunk, TArray<uint8>& OutPayload);
	bool Write(FIntPoint Chunk, TConstArrayView<uint8> Payload);
	bool Contains(FIntPoint Chunk);
	int32 NumChunks();

	const FString& GetPath() const { return Path; }

private:
	struct FEntry
	{
		uint32 Offset = 0;  // in sectors
		uint32 Sectors = 0;
		uint32 Length = 0;  // in bytes; 0 for none
		uint32 Crc = 0;
	};

	static int32 IndexOf(FIntPoint Chunk) { return (Chunk.Y & 31) * Width + (Chunk.X & 31); }
	// Reads the header the first time; with the lock held. False if the file exists but can't be opened.
	bool EnsureHeader();

	FString Path;
	FCriticalSection Lock;
	bool bHeaderLoaded = false;
	bool bFileExists = false;
	TArray<FEntry> Entries;
	// The first sector past the last payload.
	uint32 EndSector = HeaderSectors;
};
