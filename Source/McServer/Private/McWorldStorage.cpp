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
	return FindRegion(FMcRegionFile::RegionOf(Chunk)).Read(Chunk, OutBytes);
}

void FMcWorldStorage::QueueSave(FIntPoint Chunk, TArray<uint8>&& Bytes)
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
	if (!FindRegion(FMcRegionFile::RegionOf(Chunk)).Write(Chunk, Bytes))
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
