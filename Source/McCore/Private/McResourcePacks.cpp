#include "McResourcePacks.h"
#include "McCache.h"
#include "McCore.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/Compression.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	uint16 ReadU16(const uint8* Data) { return static_cast<uint16>(Data[0] | (Data[1] << 8)); }
	uint32 ReadU32(const uint8* Data) { return static_cast<uint32>(Data[0]) | (static_cast<uint32>(Data[1]) << 8) | (static_cast<uint32>(Data[2]) << 16) | (static_cast<uint32>(Data[3]) << 24); }
	uint64 ReadU64(const uint8* Data) { return static_cast<uint64>(ReadU32(Data)) | (static_cast<uint64>(ReadU32(Data + 4)) << 32); }

	// The first number of a JSON value that is a number or an array of numbers ([major, minor] formats).
	bool ReadFormat(const TSharedPtr<FJsonValue>& Value, int32& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		double Number;
		if (Value->TryGetNumber(Number))
		{
			Out = static_cast<int32>(Number);
			return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if (Value->TryGetArray(Array) && Array->Num() > 0)
		{
			return (*Array)[0]->TryGetNumber(Number) && (Out = static_cast<int32>(Number), true);
		}
		return false;
	}

	// "§6Hello" -> "Hello": a section sign and the character after it.
	FString StripColourCodes(const FString& Text)
	{
		FString Out;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == TEXT('§'))
			{
				++Index;
				continue;
			}
			Out.AppendChar(Text[Index]);
		}
		return Out;
	}

	bool FullMatch(const FString& Pattern, const FString& Text)
	{
		const FRegexPattern Compiled(TEXT("^(?:") + Pattern + TEXT(")$"));
		FRegexMatcher Matcher(Compiled, Text);
		return Matcher.FindNext();
	}

	// "assets/minecraft/textures/a.png" -> namespace "minecraft", path "textures/a.png". False for files outside assets/ and data/.
	bool SplitNamespace(const FString& Relative, FString& Namespace, FString& Path)
	{
		if (!Relative.StartsWith(TEXT("assets/")) && !Relative.StartsWith(TEXT("data/")))
		{
			return false;
		}
		const int32 First = Relative.Find(TEXT("/"));
		const int32 Second = Relative.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, First + 1);
		if (Second == INDEX_NONE)
		{
			return false;
		}
		Namespace = Relative.Mid(First + 1, Second - First - 1);
		Path = Relative.Mid(Second + 1);
		return true;
	}

	FCriticalSection GLock;
	FCriticalSection GExtractLock;
	TSharedPtr<const FMcPackStack> GStack;
	bool GStackRead = false;
	uint32 GGeneration = 1;
	bool GReloadRequested = false;
	int32 GMaxSize = 0;
	int64 GBudget = 0;
	int32 GUsePbr = -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// FMcResourcePack

FString FMcResourcePack::FlattenText(const TSharedPtr<FJsonValue>& Component)
{
	if (!Component.IsValid())
	{
		return FString();
	}
	FString Text;
	if (Component->TryGetString(Text))
	{
		return StripColourCodes(Text);
	}
	const TArray<TSharedPtr<FJsonValue>>* Array;
	if (Component->TryGetArray(Array))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Array)
		{
			Text += FlattenText(Entry);
		}
		return Text;
	}
	const TSharedPtr<FJsonObject>* Object;
	if (Component->TryGetObject(Object))
	{
		FString Own;
		if ((*Object)->TryGetStringField(TEXT("text"), Own))
		{
			Text += StripColourCodes(Own);
		}
		const TArray<TSharedPtr<FJsonValue>>* Extra;
		if ((*Object)->TryGetArrayField(TEXT("extra"), Extra))
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Extra)
			{
				Text += FlattenText(Entry);
			}
		}
	}
	return Text;
}

bool FMcResourcePack::ParseMcmeta(const FString& Json, FMcPackInfo& Out)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Pack;
	if (!Root->TryGetObjectField(TEXT("pack"), Pack))
	{
		return false;
	}
	Out.PackFormat = Out.MinFormat = Out.MaxFormat = 0;
	ReadFormat((*Pack)->TryGetField(TEXT("pack_format")), Out.PackFormat);
	ReadFormat((*Pack)->TryGetField(TEXT("min_format")), Out.MinFormat);
	ReadFormat((*Pack)->TryGetField(TEXT("max_format")), Out.MaxFormat);

	// "supported_formats" (older packs): one number, [min, max], or { min_inclusive, max_inclusive }.
	if (Out.MinFormat == 0 && Out.MaxFormat == 0)
	{
		const TSharedPtr<FJsonValue> Supported = (*Pack)->TryGetField(TEXT("supported_formats"));
		const TArray<TSharedPtr<FJsonValue>>* Range;
		const TSharedPtr<FJsonObject>* RangeObject;
		double Number;
		if (Supported.IsValid() && Supported->TryGetNumber(Number))
		{
			Out.MinFormat = Out.MaxFormat = static_cast<int32>(Number);
		}
		else if (Supported.IsValid() && Supported->TryGetArray(Range) && Range->Num() >= 2)
		{
			Out.MinFormat = ReadFormat((*Range)[0], Out.MinFormat) ? Out.MinFormat : 0;
			Out.MaxFormat = ReadFormat((*Range)[1], Out.MaxFormat) ? Out.MaxFormat : 0;
		}
		else if (Supported.IsValid() && Supported->TryGetObject(RangeObject))
		{
			(*RangeObject)->TryGetNumberField(TEXT("min_inclusive"), Out.MinFormat);
			(*RangeObject)->TryGetNumberField(TEXT("max_inclusive"), Out.MaxFormat);
		}
	}
	Out.Description = FlattenText((*Pack)->TryGetField(TEXT("description")));

	Out.Overlays.Reset();
	const TSharedPtr<FJsonObject>* OverlaysObject;
	const TArray<TSharedPtr<FJsonValue>>* Entries;
	if (Root->TryGetObjectField(TEXT("overlays"), OverlaysObject) && (*OverlaysObject)->TryGetArrayField(TEXT("entries"), Entries))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject>* Entry;
			FMcPackInfo::FOverlay Overlay;
			if (!Value->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("directory"), Overlay.Directory) || Overlay.Directory.IsEmpty())
			{
				continue;
			}
			// "formats" like supported_formats (a number, [min, max] or { min_inclusive, max_inclusive }), or min_format / max_format.
			const TSharedPtr<FJsonValue> Formats = (*Entry)->TryGetField(TEXT("formats"));
			const TArray<TSharedPtr<FJsonValue>>* Range;
			const TSharedPtr<FJsonObject>* RangeObject;
			double Number;
			if (Formats.IsValid() && Formats->TryGetNumber(Number))
			{
				Overlay.MinFormat = Overlay.MaxFormat = static_cast<int32>(Number);
				Overlay.bHasRange = true;
			}
			else if (Formats.IsValid() && Formats->TryGetArray(Range) && Range->Num() >= 2)
			{
				Overlay.bHasRange = ReadFormat((*Range)[0], Overlay.MinFormat) | ReadFormat((*Range)[1], Overlay.MaxFormat);
			}
			else if (Formats.IsValid() && Formats->TryGetObject(RangeObject))
			{
				(*RangeObject)->TryGetNumberField(TEXT("min_inclusive"), Overlay.MinFormat);
				(*RangeObject)->TryGetNumberField(TEXT("max_inclusive"), Overlay.MaxFormat);
				Overlay.bHasRange = true;
			}
			else
			{
				Overlay.bHasRange = ReadFormat((*Entry)->TryGetField(TEXT("min_format")), Overlay.MinFormat) | ReadFormat((*Entry)->TryGetField(TEXT("max_format")), Overlay.MaxFormat);
			}
			Out.Overlays.Add(Overlay);
		}
	}

	Out.Blocks.Reset();
	const TSharedPtr<FJsonObject>* Filter;
	const TArray<TSharedPtr<FJsonValue>>* Blocks;
	if (Root->TryGetObjectField(TEXT("filter"), Filter) && (*Filter)->TryGetArrayField(TEXT("block"), Blocks))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Blocks)
		{
			const TSharedPtr<FJsonObject>* Object;
			if (Entry->TryGetObject(Object))
			{
				FMcPackInfo::FBlock Block;
				(*Object)->TryGetStringField(TEXT("namespace"), Block.Namespace);
				(*Object)->TryGetStringField(TEXT("path"), Block.Path);
				Out.Blocks.Add(Block);
			}
		}
	}
	return true;
}

TSharedPtr<FMcResourcePack> FMcResourcePack::Open(const FString& PackPath, FString* OutError)
{
	auto Fail = [OutError](const FString& Why) -> TSharedPtr<FMcResourcePack>
	{
		if (OutError)
		{
			*OutError = Why;
		}
		return nullptr;
	};

	TSharedRef<FMcResourcePack> Pack = MakeShared<FMcResourcePack>();
	Pack->Info.Path = FPaths::ConvertRelativePathToFull(PackPath);
	Pack->Info.Name = FPaths::GetCleanFilename(Pack->Info.Path);
	FString Mcmeta;

	if (IFileManager::Get().DirectoryExists(*Pack->Info.Path))
	{
		const FString McmetaPath = Pack->Info.Path / TEXT("pack.mcmeta");
		if (!FFileHelper::LoadFileToString(Mcmeta, *McmetaPath))
		{
			return Fail(TEXT("no pack.mcmeta in the folder"));
		}
		Pack->Stamp = Pack->Info.Path + TEXT("|") + IFileManager::Get().GetTimeStamp(*McmetaPath).ToString();
		Pack->Info.bHasIcon = IFileManager::Get().FileExists(*(Pack->Info.Path / TEXT("pack.png")));
	}
	else if (IFileManager::Get().FileExists(*Pack->Info.Path))
	{
		Pack->Info.bZip = true;
		FString Error;
		if (!Pack->OpenZip(Pack->Info.Path, Error))
		{
			return Fail(Error);
		}
		TArray<uint8> Bytes;
		if (!Pack->Read(TEXT("pack.mcmeta"), Bytes))
		{
			return Fail(TEXT("no pack.mcmeta in the zip"));
		}
		FFileHelper::BufferToString(Mcmeta, Bytes.GetData(), Bytes.Num());
		Pack->Info.bHasIcon = Pack->Entries.Contains(TEXT("pack.png"));
		Pack->Stamp = FString::Printf(TEXT("%s|%lld|%s"), *Pack->Info.Path, IFileManager::Get().FileSize(*Pack->Info.Path),
			*IFileManager::Get().GetTimeStamp(*Pack->Info.Path).ToString());
	}
	else
	{
		return Fail(TEXT("not found"));
	}

	// A byte order mark in front of the JSON would stop the parser.
	Mcmeta.RemoveFromStart(TEXT("﻿"));
	if (!ParseMcmeta(Mcmeta, Pack->Info))
	{
		return Fail(TEXT("pack.mcmeta has no \"pack\" section"));
	}
	return Pack;
}

bool FMcResourcePack::OpenZip(const FString& PackPath, FString& OutError)
{
	const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackPath));
	if (!Reader)
	{
		OutError = TEXT("can't open the file");
		return false;
	}
	const int64 Size = Reader->TotalSize();
	const int64 TailSize = FMath::Min<int64>(Size, 0xFFFF + 22 + 20);
	TArray<uint8> Tail;
	Tail.SetNumUninitialized(static_cast<int32>(TailSize));
	Reader->Seek(Size - TailSize);
	Reader->Serialize(Tail.GetData(), TailSize);

	int32 EndAt = INDEX_NONE;
	for (int32 At = static_cast<int32>(TailSize) - 22; At >= 0; --At)
	{
		if (ReadU32(Tail.GetData() + At) == 0x06054b50)
		{
			EndAt = At;
			break;
		}
	}
	if (EndAt == INDEX_NONE)
	{
		OutError = TEXT("not a zip file");
		return false;
	}
	const uint8* End = Tail.GetData() + EndAt;
	uint64 NumEntries = ReadU16(End + 10);
	uint64 DirectorySize = ReadU32(End + 12);
	uint64 DirectoryOffset = ReadU32(End + 16);

	if ((NumEntries == 0xFFFF || DirectoryOffset == 0xFFFFFFFF) && EndAt >= 20 && ReadU32(End - 20) == 0x07064b50)
	{
		const uint64 End64Offset = ReadU64(End - 20 + 8);
		uint8 End64[56];
		Reader->Seek(static_cast<int64>(End64Offset));
		Reader->Serialize(End64, sizeof(End64));
		if (ReadU32(End64) != 0x06064b50)
		{
			OutError = TEXT("bad zip64 directory");
			return false;
		}
		NumEntries = ReadU64(End64 + 32);
		DirectorySize = ReadU64(End64 + 40);
		DirectoryOffset = ReadU64(End64 + 48);
	}
	if (DirectorySize > 512ull * 1024 * 1024 || DirectoryOffset + DirectorySize > static_cast<uint64>(Size))
	{
		OutError = TEXT("bad zip directory");
		return false;
	}

	TArray<uint8> Directory;
	Directory.SetNumUninitialized(static_cast<int32>(DirectorySize));
	Reader->Seek(static_cast<int64>(DirectoryOffset));
	Reader->Serialize(Directory.GetData(), static_cast<int64>(DirectorySize));

	int64 At = 0;
	while (At + 46 <= Directory.Num() && ReadU32(Directory.GetData() + At) == 0x02014b50)
	{
		const uint8* Head = Directory.GetData() + At;
		const uint16 Flags = ReadU16(Head + 8);
		const uint16 Method = ReadU16(Head + 10);
		uint64 CompressedSize = ReadU32(Head + 20);
		uint64 UncompressedSize = ReadU32(Head + 24);
		const int32 NameLength = ReadU16(Head + 28);
		const int32 ExtraLength = ReadU16(Head + 30);
		const int32 CommentLength = ReadU16(Head + 32);
		uint64 LocalOffset = ReadU32(Head + 42);
		if (At + 46 + NameLength + ExtraLength + CommentLength > Directory.Num())
		{
			break;
		}
		FUTF8ToTCHAR Name(reinterpret_cast<const ANSICHAR*>(Head + 46), NameLength);
		FString EntryName(Name.Length(), Name.Get());

		// zip64: the fields that were 0xFFFFFFFF are in the extra block (id 1), in the order size, compressed, offset.
		const uint8* Extra = Head + 46 + NameLength;
		for (int32 ExtraAt = 0; ExtraAt + 4 <= ExtraLength;)
		{
			const uint16 Id = ReadU16(Extra + ExtraAt);
			const int32 Length = ReadU16(Extra + ExtraAt + 2);
			if (Id == 1)
			{
				const uint8* Field = Extra + ExtraAt + 4;
				if (UncompressedSize == 0xFFFFFFFF) { UncompressedSize = ReadU64(Field); Field += 8; }
				if (CompressedSize == 0xFFFFFFFF) { CompressedSize = ReadU64(Field); Field += 8; }
				if (LocalOffset == 0xFFFFFFFF) { LocalOffset = ReadU64(Field); }
			}
			ExtraAt += 4 + Length;
		}

		// Folders end in a slash; encrypted entries (bit 0) can't be read.
		if (!EntryName.EndsWith(TEXT("/")) && !(Flags & 1))
		{
			FZipEntry& Entry = Entries.Add(EntryName.Replace(TEXT("\\"), TEXT("/")));
			Entry.LocalOffset = static_cast<int64>(LocalOffset);
			Entry.CompressedSize = static_cast<int64>(CompressedSize);
			Entry.Size = static_cast<int64>(UncompressedSize);
			Entry.Method = Method;
		}
		At += 46 + NameLength + ExtraLength + CommentLength;
	}
	if (Entries.Num() == 0)
	{
		OutError = TEXT("the zip is empty");
		return false;
	}
	return true;
}

bool FMcResourcePack::ReadZipEntry(const FString& ZipPath, const FString& NameEnding, TArray<uint8>& Out)
{
	FMcResourcePack Zip;
	Zip.Info.bZip = true;
	Zip.Info.Path = ZipPath;
	FString Error;
	if (!Zip.OpenZip(ZipPath, Error))
	{
		return false;
	}
	for (const TPair<FString, FZipEntry>& Entry : Zip.Entries)
	{
		if (Entry.Key.EndsWith(NameEnding))
		{
			return Zip.Read(Entry.Key, Out);
		}
	}
	return false;
}

bool FMcResourcePack::HasExact(const FString& Path) const
{
	if (Info.bZip)
	{
		return Entries.Contains(Path);
	}
	return IFileManager::Get().FileExists(*(Info.Path / Path));
}

FString FMcResourcePack::Resolve(const FString& Relative) const
{
	if (Info.Overlays.Num() > 0 && Relative.StartsWith(TEXT("assets/")))
	{
		for (int32 Index = Info.Overlays.Num() - 1; Index >= 0; --Index)
		{
			const FMcPackInfo::FOverlay& Overlay = Info.Overlays[Index];
			if (!Overlay.bHasRange || (Overlay.MinFormat > 0 && McPacks::GameFormat < Overlay.MinFormat) || (Overlay.MaxFormat > 0 && McPacks::GameFormat > Overlay.MaxFormat))
			{
				continue;
			}
			const FString InOverlay = Overlay.Directory / Relative;
			if (HasExact(InOverlay))
			{
				return InOverlay;
			}
		}
	}
	return Relative;
}

bool FMcResourcePack::Has(const FString& Relative) const
{
	return HasExact(Resolve(Relative));
}

FString FMcResourcePack::GetFolderFile(const FString& Relative) const
{
	return Info.bZip ? FString() : Info.Path / Resolve(Relative);
}

bool FMcResourcePack::Read(const FString& Name, TArray<uint8>& Out) const
{
	const FString Relative = Resolve(Name);
	if (!Info.bZip)
	{
		return FFileHelper::LoadFileToArray(Out, *(Info.Path / Relative));
	}
	const FZipEntry* Entry = Entries.Find(Relative);
	if (!Entry || Entry->Size > 0x7FFFFFFF || Entry->CompressedSize > 0x7FFFFFFF)
	{
		return false;
	}
	// A reader of its own per call keeps this safe from several threads.
	const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Info.Path));
	if (!Reader)
	{
		return false;
	}
	uint8 Local[30];
	Reader->Seek(Entry->LocalOffset);
	Reader->Serialize(Local, sizeof(Local));
	if (Reader->IsError() || ReadU32(Local) != 0x04034b50)
	{
		return false;
	}
	Reader->Seek(Entry->LocalOffset + 30 + ReadU16(Local + 26) + ReadU16(Local + 28));
	TArray<uint8> Packed;
	Packed.SetNumUninitialized(static_cast<int32>(Entry->CompressedSize));
	Reader->Serialize(Packed.GetData(), Packed.Num());
	if (Reader->IsError())
	{
		return false;
	}
	if (Entry->Method == 0)
	{
		Out = MoveTemp(Packed);
		return true;
	}
	if (Entry->Method != 8)
	{
		return false;  // only stored and deflated entries
	}
	Out.SetNumUninitialized(static_cast<int32>(Entry->Size));
	// Raw deflate, no zlib header: a negative window size.
	return FCompression::UncompressMemory(NAME_Zlib, Out.GetData(), Out.Num(), Packed.GetData(), Packed.Num(), COMPRESS_NoFlags,
		static_cast<uintptr_t>(static_cast<intptr_t>(-15)));
}

// ---------------------------------------------------------------------------------------------------------------------
// FMcPackStack

bool FMcPackStack::IsHiddenAbove(const FString& Relative, int32 Index) const
{
	FString Namespace, Path;
	if (!SplitNamespace(Relative, Namespace, Path))
	{
		return false;
	}
	for (int32 Above = Index + 1; Above < Packs.Num(); ++Above)
	{
		for (const FMcPackInfo::FBlock& Block : Packs[Above]->GetInfo().Blocks)
		{
			if ((Block.Namespace.IsEmpty() || FullMatch(Block.Namespace, Namespace)) && (Block.Path.IsEmpty() || FullMatch(Block.Path, Path)))
			{
				return true;
			}
		}
	}
	return false;
}

int32 FMcPackStack::Find(const FString& Relative) const
{
	for (int32 Index = Packs.Num() - 1; Index >= 0; --Index)
	{
		if (Packs[Index]->Has(Relative) && !IsHiddenAbove(Relative, Index))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FMcPackStack::IsVanillaHidden(const FString& Relative) const
{
	return Packs.Num() > 0 && IsHiddenAbove(Relative, INDEX_NONE);
}

bool FMcPackStack::Read(const FString& Relative, TArray<uint8>& Out, int32* OutPack) const
{
	const int32 Index = Find(Relative);
	if (OutPack)
	{
		*OutPack = Index;
	}
	return Index != INDEX_NONE && Packs[Index]->Read(Relative, Out);
}

// ---------------------------------------------------------------------------------------------------------------------
// McPacks

namespace McPacks
{
	TArray<FString> ParseList(const FString& List)
	{
		TArray<FString> Names;
		List.ParseIntoArray(Names, TEXT(","), true);
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			Name.TrimQuotesInline();
		}
		Names.RemoveAll([](const FString& Name) { return Name.IsEmpty(); });
		return Names;
	}

	FString GetFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("resourcepacks"));
	}

	FString ResolveName(const FString& Name)
	{
		const FString InFolder = GetFolder() / Name;
		if (IFileManager::Get().FileExists(*InFolder) || IFileManager::Get().DirectoryExists(*InFolder))
		{
			return InFolder;
		}
		if (IFileManager::Get().FileExists(*Name) || IFileManager::Get().DirectoryExists(*Name))
		{
			return FPaths::ConvertRelativePathToFull(Name);
		}
		return FString();
	}

	static TSharedRef<const FMcPackStack> OpenNames(const TArray<FString>& Names)
	{
		TArray<TSharedPtr<FMcResourcePack>> Packs;
		for (const FString& Name : Names)
		{
			const FString Path = ResolveName(Name);
			if (Path.IsEmpty())
			{
				UE_LOG(LogMcCore, Warning, TEXT("Resource pack %s not found (looked in %s)"), *Name, *GetFolder());
				continue;
			}
			FString Error;
			if (const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Path, &Error))
			{
				const FMcPackInfo& Info = Pack->GetInfo();
				UE_LOG(LogMcCore, Log, TEXT("Resource pack %s: pack_format %d (supports %d to %d), %s, \"%s\""), *Info.Name, Info.PackFormat,
					Info.MinFormat, Info.MaxFormat, Info.bZip ? *FString::Printf(TEXT("zip of %d files"), Pack->NumZipEntries()) : TEXT("folder"), *Info.Description);
				Packs.Add(Pack);
			}
			else
			{
				UE_LOG(LogMcCore, Warning, TEXT("Resource pack %s can't be used: %s"), *Path, *Error);
			}
		}
		return MakeShared<FMcPackStack>(MoveTemp(Packs));
	}

	static FString GetSelectionFile()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McResourcePacks.json"));
	}

	static TArray<FString> LoadSavedSelection()
	{
		TArray<FString> Names;
		FString Text;
		TSharedPtr<FJsonObject> Json;
		const TArray<TSharedPtr<FJsonValue>>* Packs = nullptr;
		if (FFileHelper::LoadFileToString(Text, *GetSelectionFile()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json)
			&& Json.IsValid() && Json->TryGetArrayField(TEXT("packs"), Packs))
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Packs)
			{
				FString Name;
				if (Entry->TryGetString(Name))
				{
					Names.Add(Name);
				}
			}
		}
		return Names;
	}

	TSharedRef<const FMcPackStack> GetStack()
	{
		{
			FScopeLock Lock(&GLock);
			if (GStackRead)
			{
				return GStack.ToSharedRef();
			}
		}
		FString List;
		TArray<FString> Names;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McPacks="), List))
		{
			Names = ParseList(List);
		}
		else
		{
			Names = LoadSavedSelection();
		}
		TSharedRef<const FMcPackStack> Made = OpenNames(Names);
		FScopeLock Lock(&GLock);
		if (!GStackRead)
		{
			GStack = Made;
			GStackRead = true;
		}
		return GStack.ToSharedRef();
	}

	void SetPacks(const TArray<FString>& Names)
	{
		SetStack(OpenNames(Names));
	}

	void SetStack(TSharedRef<const FMcPackStack> Stack)
	{
		FScopeLock Lock(&GLock);
		GStack = Stack;
		GStackRead = true;
		++GGeneration;
	}

	TArray<FFolderEntry> ScanFolder()
	{
		TArray<FFolderEntry> Entries;
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(GetFolder() / TEXT("*.zip")), true, false);
		IFileManager::Get().FindFiles(Names, *(GetFolder() / TEXT("*")), false, true);
		Names.Sort();
		for (const FString& Name : Names)
		{
			FFolderEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Name = Name;
			Entry.Path = GetFolder() / Name;
			Entry.Pack = FMcResourcePack::Open(Entry.Path, &Entry.Error);
		}
		return Entries;
	}

	FDrop SortDropped(const TArray<FString>& Paths)
	{
		FDrop Drop;
		for (const FString& Path : Paths)
		{
			const bool bFolder = IFileManager::Get().DirectoryExists(*Path);
			const bool bZip = !bFolder && Path.EndsWith(TEXT(".zip"), ESearchCase::IgnoreCase) && IFileManager::Get().FileExists(*Path);
			if (bZip || (bFolder && IFileManager::Get().FileExists(*(Path / TEXT("pack.mcmeta")))))
			{
				Drop.Packs.Add(Path);
				Drop.Names.Add(FPaths::GetCleanFilename(Path));
			}
			else
			{
				Drop.Others.Add(FPaths::GetCleanFilename(Path));
			}
		}
		return Drop;
	}

	TArray<FString> CopyDropped(const FDrop& Drop, TArray<FString>& Failed, const FString& IntoFolder)
	{
		const FString Folder = IntoFolder.IsEmpty() ? GetFolder() : IntoFolder;
		IFileManager::Get().MakeDirectory(*Folder, true);
		TArray<FString> Copied;
		for (int32 Index = 0; Index < Drop.Packs.Num(); ++Index)
		{
			const FString To = Folder / Drop.Names[Index];
			const bool bFolder = IFileManager::Get().DirectoryExists(*Drop.Packs[Index]);
			if (IFileManager::Get().FileExists(*To) || IFileManager::Get().DirectoryExists(*To))
			{
				Failed.Add(Drop.Names[Index]);
			}
			else if (bFolder ? FPlatformFileManager::Get().GetPlatformFile().CopyDirectoryTree(*To, *Drop.Packs[Index], false)
				: IFileManager::Get().Copy(*To, *Drop.Packs[Index]) == COPY_OK)
			{
				Copied.Add(Drop.Names[Index]);
			}
			else
			{
				Failed.Add(Drop.Names[Index]);
			}
		}
		return Copied;
	}

	TArray<FString> GetSelectedNames()
	{
		const TSharedRef<const FMcPackStack> Stack = GetStack();
		TArray<FString> Names;
		for (int32 Index = 0; Index < Stack->Num(); ++Index)
		{
			Names.Add(Stack->Get(Index).GetInfo().Name);
		}
		return Names;
	}

	void ApplySelection(const TArray<FString>& Names)
	{
		SetPacks(Names);
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Packs;
		for (const FString& Name : Names)
		{
			Packs.Add(MakeShared<FJsonValueString>(Name));
		}
		Json->SetArrayField(TEXT("packs"), Packs);
		FString Text;
		FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
		FFileHelper::SaveStringToFile(Text, *GetSelectionFile());
	}

	void ReadAll(const FString& Relative, TArray<TArray<uint8>>& Out)
	{
		const TSharedRef<const FMcPackStack> Stack = GetStack();
		const FString InPack = TEXT("assets/minecraft/") + Relative;
		for (int32 Index = 0; Index < Stack->Num(); ++Index)
		{
			TArray<uint8> Bytes;
			if (Stack->Get(Index).Has(InPack) && Stack->Get(Index).Read(InPack, Bytes))
			{
				Out.Add(MoveTemp(Bytes));
			}
		}
	}

	void RequestReload()
	{
		FScopeLock Lock(&GLock);
		GReloadRequested = true;
	}

	bool TakeReloadRequest()
	{
		FScopeLock Lock(&GLock);
		const bool bAsked = GReloadRequested;
		GReloadRequested = false;
		return bAsked;
	}

	uint32 GetGeneration()
	{
		FScopeLock Lock(&GLock);
		return GGeneration;
	}

	bool HasPacks()
	{
		return GetStack()->Num() > 0;
	}

	bool TryResolve(const FString& Relative, FString& OutPath)
	{
		const TSharedRef<const FMcPackStack> Stack = GetStack();
		if (Stack->Num() == 0)
		{
			return false;
		}
		const FString InPack = TEXT("assets/minecraft/") + Relative;
		const int32 Index = Stack->Find(InPack);
		if (Index == INDEX_NONE)
		{
			if (Stack->IsVanillaHidden(InPack) || IsAtlasFiltered(Relative))
			{
				OutPath = McCache::Root() / TEXT("hidden-by-resource-pack") / Relative;
				return true;
			}
			return FindAtlasSprite(Relative, OutPath);
		}
		const FMcResourcePack& Pack = Stack->Get(Index);
		if (!Pack.GetInfo().bZip)
		{
			OutPath = Pack.GetFolderFile(InPack);
			return true;
		}

		// A zip's file is taken out into the cache once, and the same file next time. The folder is named by the zip's stamp, so a
		// changed zip starts afresh.
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("McPackCache") / FMD5::HashAnsiString(*Pack.GetStamp()).Left(12);
		auto Extract = [&](const FString& Name) -> FString
		{
			const FString Target = FPaths::ConvertRelativePathToFull(Folder / Name);
			FScopeLock Lock(&GExtractLock);
			if (IFileManager::Get().FileExists(*Target))
			{
				return Target;
			}
			TArray<uint8> Bytes;
			if (!Pack.Read(Name, Bytes))
			{
				return FString();
			}
			const FString Temp = Target + TEXT(".part");
			if (!FFileHelper::SaveArrayToFile(Bytes, *Temp) || !IFileManager::Get().Move(*Target, *Temp, true))
			{
				UE_LOG(LogMcCore, Warning, TEXT("Can't write %s"), *Target);
				return FString();
			}
			return Target;
		};
		OutPath = Extract(InPack);
		if (OutPath.IsEmpty())
		{
			return false;
		}
		if (InPack.EndsWith(TEXT(".png")) && Pack.Has(InPack + TEXT(".mcmeta")))
		{
			Extract(InPack + TEXT(".mcmeta"));
		}
		return true;
	}

	bool IsFromPack(const FString& Relative)
	{
		const TSharedRef<const FMcPackStack> Stack = GetStack();
		FString Unused;
		return Stack->Num() > 0 && (Stack->Find(TEXT("assets/minecraft/") + Relative) != INDEX_NONE || FindAtlasSprite(Relative, Unused));
	}

	FString GetSignature()
	{
		const TSharedRef<const FMcPackStack> Stack = GetStack();
		FString Signature;
		for (int32 Index = 0; Index < Stack->Num(); ++Index)
		{
			Signature += Stack->Get(Index).GetStamp() + TEXT(";");
		}
		return Signature;
	}

	int32 GetMaxTextureSize()
	{
		FScopeLock Lock(&GLock);
		if (GMaxSize == 0)
		{
			int32 Size = 512;
			FParse::Value(FCommandLine::Get(), TEXT("-McPackMaxSize="), Size);
			GMaxSize = FMath::Clamp(static_cast<int32>(FMath::RoundUpToPowerOfTwo(FMath::Max(Size, 16))), 16, 8192);
		}
		return GMaxSize;
	}

	void SetMaxTextureSize(int32 Size)
	{
		FScopeLock Lock(&GLock);
		GMaxSize = FMath::Clamp(static_cast<int32>(FMath::RoundUpToPowerOfTwo(FMath::Max(Size, 16))), 16, 8192);
	}

	int64 GetBudgetBytes()
	{
		FScopeLock Lock(&GLock);
		if (GBudget == 0)
		{
			int32 Megabytes = 1536;
			FParse::Value(FCommandLine::Get(), TEXT("-McPackBudgetMB="), Megabytes);
			GBudget = static_cast<int64>(FMath::Max(Megabytes, 1)) * 1024 * 1024;
		}
		return GBudget;
	}

	void SetBudgetMegabytes(int32 Megabytes)
	{
		FScopeLock Lock(&GLock);
		GBudget = static_cast<int64>(FMath::Max(Megabytes, 1)) * 1024 * 1024;
	}

	bool GetUsePbr()
	{
		FScopeLock Lock(&GLock);
		if (GUsePbr < 0)
		{
			// A pack's _n/_s maps are used when asked for (-McPackPbr), and whenever HD Textures are on (-McHd, or the option
			// saved in options.json), since those are what the maps are for.
			bool bHd = FParse::Param(FCommandLine::Get(), TEXT("McHd"));
			FString Text;
			TSharedPtr<FJsonObject> Options;
			if (!bHd && FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir() / TEXT("options.json")))
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Options) && Options.IsValid())
			{
				Options->TryGetBoolField(TEXT("hdTextures"), bHd);
			}
			GUsePbr = (bHd || FParse::Param(FCommandLine::Get(), TEXT("McPackPbr"))) ? 1 : 0;
		}
		return GUsePbr != 0;
	}

	void SetUsePbr(bool bUse)
	{
		FScopeLock Lock(&GLock);
		GUsePbr = bUse ? 1 : 0;
	}

	int32 GetCompatibility(const FMcPackInfo& Info)
	{
		if (Info.MinFormat > 0 || Info.MaxFormat > 0)
		{
			const int32 Max = Info.MaxFormat > 0 ? Info.MaxFormat : MAX_int32;
			return GameFormat < Info.MinFormat ? 1 : GameFormat > Max ? -1 : 0;
		}
		// A pack_format over 64 with no min_format and max_format does not parse in the game (measured: reference verb "log packs"
		// says UNKNOWN for such packs, TOO_OLD for pack_format 3 and for the Faithful PBR demo).
		if (Info.PackFormat <= 0 || Info.PackFormat > 64)
		{
			return 2;
		}
		return Info.PackFormat == GameFormat ? 0 : Info.PackFormat < GameFormat ? -1 : 1;
	}

	int32 ChooseTextureSize(int32 Wanted, int32 NumLayers, int32 MaxSize, int64 BudgetBytes)
	{
		int32 Size = FMath::Max<int32>(FMath::RoundUpToPowerOfTwo(FMath::Max(Wanted, 16)), 16);
		const int32 Cap = FMath::Max<int32>(FMath::RoundUpToPowerOfTwo(FMath::Max(MaxSize, 16)), 16);
		Size = FMath::Min(Size, Cap);
		while (Size > 16)
		{
			int64 Bytes = 0;
			for (int32 Side = Size; Side >= 1; Side /= 2)
			{
				Bytes += static_cast<int64>(Side) * Side * 4;
			}
			if (Bytes * NumLayers <= BudgetBytes)
			{
				break;
			}
			Size /= 2;
		}
		return Size;
	}
}
