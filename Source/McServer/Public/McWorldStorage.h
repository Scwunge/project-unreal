// Where one dimension's saved chunks live: a folder of region files (McRegionFile). Chunks are saved on a
// worker thread, one at a time in the order they were queued, so the game thread only pays for encoding; a queued
// chunk can be loaded again before it's written (it's served from memory). Thread safe: Load may be called from
// generation tasks while the game thread queues saves.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "McRegionFile.h"
#include "Tasks/Pipe.h"

class MCSERVER_API FMcWorldStorage
{
public:
	// The dimension's folder (see McWorldSave::GetDimensionFolder); made when the first chunk is written.
	explicit FMcWorldStorage(FString InFolder);
	// Waits for the queued writes.
	~FMcWorldStorage();
	UE_NONCOPYABLE(FMcWorldStorage);

	// The saved bytes of a chunk (McChunkSave's), newest first: still waiting to be written, else in the file. False
	// if the chunk was never saved or is damaged.
	bool Load(FIntPoint Chunk, TArray<uint8>& OutBytes);
	// Where a chunk that was never saved can come from (a Java world still being imported): Load asks it, after the
	// queue and the file, for the chunk's bytes. It is called on whatever thread loads; it should QueueSaveIfAbsent what
	// it makes. Pass an empty function to stop.
	using FChunkSource = TFunction<bool(FIntPoint, TArray<uint8>&)>;
	void SetChunkSource(FChunkSource Source);
	// Like QueueSave, but only if the chunk has no save yet (queued or written): false if it had one and the bytes were
	// dropped. A chunk the player has already changed and saved is not replaced by a late import of it.
	bool QueueSaveIfAbsent(FIntPoint Chunk, TArray<uint8>&& Bytes);
	// Hands the bytes over to be written in the background. A newer save of the same chunk replaces an older one that
	// hasn't been written yet.
	void QueueSave(FIntPoint Chunk, TArray<uint8>&& Bytes);
	// Waits until everything queued has been written. Call before the process or the test ends.
	void Flush();

	bool Contains(FIntPoint Chunk);
	// Which chunks of a region (its coordinates, as McRegionFile::RegionOf gives them) the file holds, x + 32 * z.
	void GetSavedMask(FIntPoint Region, TBitArray<>& OutMask);
	// Chunks queued and not yet written.
	int32 NumQueued() const;
	// A bulk import writes without asking the disk to confirm each chunk.
	// Leaving bulk waits for the writes and lets go of the files kept open.
	void SetBulk(bool bInBulk);
	bool IsBulk() const { return bBulk; }
	// Time the writer has spent, for the save log: in the region files' Write, and in the whole task (lookups and copies too),
	// in microseconds since the storage was made.
	int64 GetWriteMicros() const { return WriteMicros; }
	int64 GetTaskMicros() const { return TaskMicros; }
	// Writes that failed, so a caller can tell the disk is full or the folder is read-only.
	int32 NumFailedWrites() const { return FailedWrites; }

	const FString& GetFolder() const { return Folder; }

private:
	struct FQueued
	{
		TArray<uint8> Bytes;
		// Which QueueSave this is, so a write task can tell whether a newer one has replaced it.
		uint32 Ticket = 0;
	};

	FMcRegionFile& FindRegion(FIntPoint Region);
	void WriteQueued(FIntPoint Chunk, uint32 Ticket);
	void Enqueue(FIntPoint Chunk, TArray<uint8>&& Bytes);

	FString Folder;
	FCriticalSection SourceLock;
	TSharedPtr<FChunkSource, ESPMode::ThreadSafe> ChunkSource;
	// Chunks saved since a chunk source was set, so a late import of one does not replace it (under QueueLock).
	TSet<FIntPoint> SavedSinceSource;
	bool bTrackSaves = false;
	TAtomic<bool> bBulk{ false };
	mutable FCriticalSection QueueLock;
	TMap<FIntPoint, FQueued> Queued;
	uint32 NextTicket = 1;
	FCriticalSection RegionsLock;
	TMap<FIntPoint, TUniquePtr<FMcRegionFile>> Regions;
	TAtomic<int32> FailedWrites{ 0 };
	TAtomic<int64> WriteMicros{ 0 };
	TAtomic<int64> TaskMicros{ 0 };
	UE::Tasks::FPipe Pipe{ UE_SOURCE_LOCATION };
};
