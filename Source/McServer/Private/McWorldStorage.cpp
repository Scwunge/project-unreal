#include "McWorldStorage.h"

FMcWorldStorage::FMcWorldStorage(FString InFolder)
	: Folder(MoveTemp(InFolder))
{
}

FMcWorldStorage::~FMcWorldStorage()
{
	// The pipe asserts it has no work left when it goes, and the tasks use this object.
	Flush();
}

FMcRegionFile& FMcWorldStorage::FindRegion(FIntPoint Region)
{
	FScopeLock Guard(&RegionsLock);
	TUniquePtr<FMcRegionFile>& Slot = Regions.FindOrAdd(Region);
	if (!Slot)
	{
		Slot = MakeUnique<FMcRegionFile>(Folder / FMcRegionFile::FileNameOf(Region));
	}
	// Regions are never removed, so the reference stays good after the lock is let go.
	return *Slot;
}

bool FMcWorldStorage::Load(FIntPoint Chunk, TArray<uint8>& OutBytes)
{
	{
		FScopeLock Guard(&QueueLock);
		if (const FQueued* Waiting = Queued.Find(Chunk))
		{
			OutBytes = Waiting->Bytes;
			return true;
		}
	}
	// A write that finishes between the two steps has put its bytes in the file before dropping them from the queue,
	// so the file always has them.
	if (FindRegion(FMcRegionFile::RegionOf(Chunk)).Read(Chunk, OutBytes))
	{
		return true;
	}
	TSharedPtr<FChunkSource, ESPMode::ThreadSafe> Source;
	{
		FScopeLock Guard(&SourceLock);
		Source = ChunkSource;
	}
	return Source && (*Source)(Chunk, OutBytes);
}

void FMcWorldStorage::SetChunkSource(FChunkSource Source)
{
	{
		FScopeLock Guard(&QueueLock);
		bTrackSaves = static_cast<bool>(Source);
		SavedSinceSource.Reset();
	}
	FScopeLock Guard(&SourceLock);
	ChunkSource.Reset();
	if (Source)
	{
		ChunkSource = MakeShared<FChunkSource, ESPMode::ThreadSafe>(MoveTemp(Source));
	}
}

bool FMcWorldStorage::QueueSaveIfAbsent(FIntPoint Chunk, TArray<uint8>&& Bytes)
{
	// Only the queue and what was saved since the source was set are looked at (not the file, whose lock the writer holds
	// while it writes): a chunk the file already had is not asked of the source, and callers that go through whole
	// regions look at GetSavedMask first.
	{
		FScopeLock Guard(&QueueLock);
		if (Queued.Contains(Chunk) || SavedSinceSource.Contains(Chunk))
		{
			return false;
		}
	}
	Enqueue(Chunk, MoveTemp(Bytes));
	return true;
}

void FMcWorldStorage::GetSavedMask(FIntPoint Region, TBitArray<>& OutMask)
{
	FindRegion(Region).GetPresent(OutMask);
}

void FMcWorldStorage::QueueSave(FIntPoint Chunk, TArray<uint8>&& Bytes)
{
	Enqueue(Chunk, MoveTemp(Bytes));
}

void FMcWorldStorage::Enqueue(FIntPoint Chunk, TArray<uint8>&& Bytes)
{
	if (Bytes.IsEmpty())
	{
		return;
	}
	uint32 Ticket = 0;
	{
		FScopeLock Guard(&QueueLock);
		Ticket = NextTicket++;
		FQueued& Entry = Queued.FindOrAdd(Chunk);
		Entry.Bytes = MoveTemp(Bytes);
		Entry.Ticket = Ticket;
		if (bTrackSaves)
		{
			SavedSinceSource.Add(Chunk);
		}
	}
	Pipe.Launch(UE_SOURCE_LOCATION, [this, Chunk, Ticket]() { WriteQueued(Chunk, Ticket); });
}

void FMcWorldStorage::WriteQueued(FIntPoint Chunk, uint32 Ticket)
{
	TArray<uint8> Bytes;
	{
		FScopeLock Guard(&QueueLock);
		const FQueued* Entry = Queued.Find(Chunk);
		// A newer save replaced this one; its own task writes that.
		if (!Entry || Entry->Ticket != Ticket)
		{
			return;
		}
		Bytes = Entry->Bytes;
	}
	if (!FindRegion(FMcRegionFile::RegionOf(Chunk)).Write(Chunk, Bytes, !bBulk))
	{
		// Keep the bytes queued: loading the chunk again still finds them, and the next save of it tries again.
		++FailedWrites;
		return;
	}
	FScopeLock Guard(&QueueLock);
	const FQueued* Entry = Queued.Find(Chunk);
	if (Entry && Entry->Ticket == Ticket)
	{
		Queued.Remove(Chunk);
	}
}

void FMcWorldStorage::Flush()
{
	Pipe.WaitUntilEmpty();
}

void FMcWorldStorage::SetBulk(bool bInBulk)
{
	bBulk = bInBulk;
	if (!bInBulk)
	{
		Flush();
		FScopeLock Guard(&RegionsLock);
		for (TPair<FIntPoint, TUniquePtr<FMcRegionFile>>& Pair : Regions)
		{
			Pair.Value->CloseWriter();
		}
	}
}

bool FMcWorldStorage::Contains(FIntPoint Chunk)
{
	{
		FScopeLock Guard(&QueueLock);
		if (Queued.Contains(Chunk))
		{
			return true;
		}
	}
	return FindRegion(FMcRegionFile::RegionOf(Chunk)).Contains(Chunk);
}

int32 FMcWorldStorage::NumQueued() const
{
	FScopeLock Guard(&QueueLock);
	return Queued.Num();
}
