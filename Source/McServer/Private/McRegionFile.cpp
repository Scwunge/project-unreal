#include "McRegionFile.h"
#include "McServer.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Crc.h"

namespace
{
	constexpr uint8 Magic[4] = { 'M', 'C', 'R', 'G' };
	constexpr uint32 FormatVersion = 1;
	constexpr int32 FirstEntryByte = 8;
	constexpr int32 EntryBytes = 16;
	constexpr int32 NumEntries = FMcRegionFile::Width * FMcRegionFile::Width;

	// Opens for writing without truncating, so the file can be written at any offset.
	TUniquePtr<IFileHandle> OpenForUpdate(const FString& Path)
	{
		return TUniquePtr<IFileHandle>(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, /*bAppend=*/true, /*bAllowRead=*/true));
	}
}

FMcRegionFile::FMcRegionFile(FString InPath)
	: Path(MoveTemp(InPath))
{
}

bool FMcRegionFile::EnsureHeader()
{
	if (bHeaderLoaded)
	{
		return true;
	}
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	Entries.Reset();
	Entries.SetNum(NumEntries);
	EndSector = HeaderSectors;
	bFileExists = Platform.FileExists(*Path);
	if (!bFileExists)
	{
		bHeaderLoaded = true;
		return true;
	}

	TUniquePtr<IFileHandle> File(Platform.OpenRead(*Path, /*bAllowWrite=*/true));
	if (!File)
	{
		UE_LOG(LogMcServer, Error, TEXT("Can't open %s"), *Path);
		return false;
	}
	const int64 Size = File->Size();
	TArray<uint8> Header;
	Header.SetNumUninitialized(HeaderSectors * SectorSize);
	const bool bReadable = Size >= Header.Num() && File->Read(Header.GetData(), Header.Num());
	uint32 Version = 0;
	if (bReadable)
	{
		FMemory::Memcpy(&Version, &Header[4], sizeof(uint32));
	}
	if (!bReadable || FMemory::Memcmp(Header.GetData(), Magic, sizeof(Magic)) != 0 || Version != FormatVersion)
	{
		// Not ours, or cut short. Keep it for a look rather than writing over it, and start a new file.
		File.Reset();
		const FString Moved = Path + TEXT(".bad");
		UE_LOG(LogMcServer, Error, TEXT("%s isn't a region file this version can read; moving it to %s and starting a new one."), *Path, *Moved);
		IFileManager::Get().Move(*Moved, *Path, /*Replace=*/true);
		bFileExists = false;
		bHeaderLoaded = true;
		return true;
	}

	EndSector = FMath::Max<uint32>(HeaderSectors, static_cast<uint32>((Size + SectorSize - 1) / SectorSize));
	for (int32 Index = 0; Index < NumEntries; ++Index)
	{
		FEntry Entry;
		FMemory::Memcpy(&Entry, &Header[FirstEntryByte + Index * EntryBytes], sizeof(FEntry));
		if (Entry.Length == 0)
		{
			continue;
		}
		const bool bValid = Entry.Sectors > 0 && Entry.Offset >= static_cast<uint32>(HeaderSectors)
			&& static_cast<uint64>(Entry.Offset) + Entry.Sectors <= EndSector
			&& Entry.Length <= Entry.Sectors * static_cast<uint32>(SectorSize) && Entry.Length <= static_cast<uint32>(MaxPayload);
		if (!bValid)
		{
			UE_LOG(LogMcServer, Warning, TEXT("%s: chunk %d,%d has a damaged header entry; it will be generated again."),
				*Path, Index % Width, Index / Width);
			continue;
		}
		Entries[Index] = Entry;
	}
	bHeaderLoaded = true;
	return true;
}

bool FMcRegionFile::Read(FIntPoint Chunk, TArray<uint8>& OutPayload)
{
	FScopeLock Guard(&Lock);
	if (!EnsureHeader())
	{
		return false;
	}
	const FEntry& Entry = Entries[IndexOf(Chunk)];
	if (Entry.Length == 0 || !bFileExists)
	{
		return false;
	}
	TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path, /*bAllowWrite=*/true));
	if (!File || !File->Seek(static_cast<int64>(Entry.Offset) * SectorSize))
	{
		return false;
	}
	OutPayload.SetNumUninitialized(Entry.Length);
	if (!File->Read(OutPayload.GetData(), Entry.Length) || FCrc::MemCrc32(OutPayload.GetData(), OutPayload.Num()) != Entry.Crc)
	{
		UE_LOG(LogMcServer, Error, TEXT("%s: chunk %d,%d is damaged; it will be generated again."), *Path, Chunk.X, Chunk.Y);
		OutPayload.Reset();
		return false;
	}
	return true;
}

bool FMcRegionFile::Write(FIntPoint Chunk, TConstArrayView<uint8> Payload)
{
	if (Payload.IsEmpty() || Payload.Num() > MaxPayload)
	{
		return false;
	}
	FScopeLock Guard(&Lock);
	if (!EnsureHeader())
	{
		return false;
	}
	const int32 Index = IndexOf(Chunk);
	const uint32 Needed = static_cast<uint32>((Payload.Num() + SectorSize - 1) / SectorSize);
	FEntry Entry = Entries[Index];
	const bool bInPlace = Entry.Length > 0 && Entry.Sectors >= Needed;
	if (!bInPlace)
	{
		Entry.Offset = EndSector;
		Entry.Sectors = Needed;
	}
	Entry.Length = Payload.Num();
	Entry.Crc = FCrc::MemCrc32(Payload.GetData(), Payload.Num());

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	TUniquePtr<IFileHandle> File = OpenForUpdate(Path);
	if (!File)
	{
		UE_LOG(LogMcServer, Error, TEXT("Can't write %s"), *Path);
		return false;
	}
	bool bOk = true;
	if (!bFileExists)
	{
		TArray<uint8> Header;
		Header.SetNumZeroed(HeaderSectors * SectorSize);
		FMemory::Memcpy(Header.GetData(), Magic, sizeof(Magic));
		FMemory::Memcpy(&Header[4], &FormatVersion, sizeof(uint32));
		bOk = File->Seek(0) && File->Write(Header.GetData(), Header.Num());
	}
	// The payload first and the header entry after, so a write cut short leaves the old entry (or none) pointing at
	// whole data, or a payload that fails its checksum, but never an entry pointing at nothing.
	if (bOk)
	{
		bOk = File->Seek(static_cast<int64>(Entry.Offset) * SectorSize) && File->Write(Payload.GetData(), Payload.Num());
	}
	if (bOk && !bInPlace)
	{
		// Pad to the end of the last sector so the next payload starts on a boundary.
		TArray<uint8> Padding;
		Padding.SetNumZeroed(static_cast<int32>(Entry.Sectors) * SectorSize - Payload.Num());
		bOk = Padding.IsEmpty() || File->Write(Padding.GetData(), Padding.Num());
	}
	if (bOk)
	{
		bOk = File->Seek(FirstEntryByte + static_cast<int64>(Index) * EntryBytes) && File->Write(reinterpret_cast<const uint8*>(&Entry), sizeof(FEntry));
	}
	if (bOk)
	{
		bOk = File->Flush();
	}
	if (!bOk)
	{
		UE_LOG(LogMcServer, Error, TEXT("Writing chunk %d,%d to %s failed."), Chunk.X, Chunk.Y, *Path);
		return false;
	}
	bFileExists = true;
	Entries[Index] = Entry;
	if (!bInPlace)
	{
		EndSector += Entry.Sectors;
	}
	return true;
}

bool FMcRegionFile::Contains(FIntPoint Chunk)
{
	FScopeLock Guard(&Lock);
	return EnsureHeader() && Entries[IndexOf(Chunk)].Length > 0;
}

int32 FMcRegionFile::NumChunks()
{
	FScopeLock Guard(&Lock);
	int32 Count = 0;
	if (EnsureHeader())
	{
		for (const FEntry& Entry : Entries)
		{
			Count += Entry.Length > 0 ? 1 : 0;
		}
	}
	return Count;
}
