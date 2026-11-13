#include "McCacheSetup.h"
#include "McCache.h"
#include "McCore.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Compression.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	// How much of the whole progress bar each part gets.
	constexpr float UnpackShare = 0.30f;
	constexpr float AssetsShare = 0.50f;
	constexpr float GeneratorShare = 0.20f;

	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Object;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object))
		{
			return nullptr;
		}
		return Object;
	}

	// The first of Relative under any root that exists, or empty.
	FString FindUnder(const TArray<FString>& Roots, const FString& Relative)
	{
		for (const FString& Root : Roots)
		{
			const FString Path = Root / Relative;
			if (IFileManager::Get().FileExists(*Path))
			{
				return Path;
			}
		}
		return FString();
	}

	FString Sha1Of(const FString& Path)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			return FString();
		}
		FSHA1 Hash;
		Hash.Update(Bytes.GetData(), Bytes.Num());
		Hash.Final();
		uint8 Digest[FSHA1::DigestSize];
		Hash.GetHash(Digest);
		return BytesToHexLower(Digest, FSHA1::DigestSize);
	}

	// Whether a library applies to Windows, by the profile's rules (the last rule that matches wins).
	bool AppliesToWindows(const FJsonObject& Library)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rules;
		if (!Library.TryGetArrayField(TEXT("rules"), Rules))
		{
			return true;
		}
		bool bAllowed = false;
		for (const TSharedPtr<FJsonValue>& Value : *Rules)
		{
			const TSharedPtr<FJsonObject> Rule = Value->AsObject();
			const TSharedPtr<FJsonObject>* Os;
			const bool bMatches = !Rule->TryGetObjectField(TEXT("os"), Os) || (*Os)->GetStringField(TEXT("name")) == TEXT("windows");
			if (bMatches)
			{
				bAllowed = Rule->GetStringField(TEXT("action")) == TEXT("allow");
			}
		}
		return bAllowed;
	}

	// The Java major version of a java.exe, 0 if it won't run.
	int32 JavaMajor(const FString& Java)
	{
		int32 Code = 0;
		FString Out, Err;
		if (!FPlatformProcess::ExecProcess(*Java, TEXT("-version"), &Code, &Out, &Err) || Code != 0)
		{
			return 0;
		}
		const FString Text = Err + Out;
		int32 At = Text.Find(TEXT("version \""));
		return At == INDEX_NONE ? 0 : FCString::Atoi(*Text.Mid(At + 9));
	}

	// The launcher's Java runtime for 26.3 (java-runtime-epsilon, Java 25) in the places the launchers keep it, then any
	// installed JDK 25.
	FString FindJava(const TArray<FString>& Roots, const FString& Component, int32 Major)
	{
		TArray<FString> Folders;
		for (const FString& Root : Roots)
		{
			Folders.Add(Root / TEXT("runtime") / Component);  // the official launcher
			Folders.Add(Root / TEXT("java") / Component);     // Prism
		}
		// The Microsoft Store launcher keeps its files under the app's package.
		const FString UserSettings = FPlatformProcess::UserSettingsDir();
		TArray<FString> Packages;
		IFileManager::Get().FindFiles(Packages, *(UserSettings / TEXT("Packages/Microsoft.4297127D64EC6*")), false, true);
		for (const FString& Package : Packages)
		{
			Folders.Add(UserSettings / TEXT("Packages") / Package / TEXT("LocalCache/Local/runtime") / Component);
		}
		for (const TCHAR* Variable : { TEXT("ProgramFiles"), TEXT("ProgramW6432") })
		{
			const FString ProgramFiles = FPlatformMisc::GetEnvironmentVariable(Variable);
			for (const TCHAR* Vendor : { TEXT("Microsoft"), TEXT("Eclipse Adoptium"), TEXT("Java") })
			{
				if (!ProgramFiles.IsEmpty())
				{
					Folders.Add(ProgramFiles / Vendor);
				}
			}
		}

		for (const FString& Folder : Folders)
		{
			TArray<FString> Found;
			IFileManager::Get().FindFilesRecursive(Found, *Folder, TEXT("java.exe"), true, false);
			for (const FString& Java : Found)
			{
				if (JavaMajor(Java) == Major)
				{
					return Java;
				}
			}
		}
		return FString();
	}

	uint16 U16(const uint8* P) { return static_cast<uint16>(P[0] | (P[1] << 8)); }
	uint32 U32(const uint8* P) { return P[0] | (P[1] << 8) | (P[2] << 16) | (static_cast<uint32>(P[3]) << 24); }

	bool IsSafeRelative(const FString& Path)
	{
		return !Path.IsEmpty() && !Path.StartsWith(TEXT("/")) && !Path.Contains(TEXT("..")) && !Path.Contains(TEXT(":")) && !Path.Contains(TEXT("\\"));
	}
}

TArray<FString> FMcCacheBuilder::DefaultRoots()
{
	TArray<FString> Roots;
	const FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	if (!AppData.IsEmpty())
	{
		for (const TCHAR* Name : { TEXT(".minecraft"), TEXT("PrismLauncher") })
		{
			if (IFileManager::Get().DirectoryExists(*(AppData / Name)))
			{
				Roots.Add(AppData / Name);
			}
		}
	}
	return Roots;
}

bool FMcCacheBuilder::Find(const TArray<FString>& InRoots, FSources& Out, FString& Error)
{
	Out = FSources();
	for (FString Root : InRoots)
	{
		Root.TrimStartAndEndInline();
		Root.TrimQuotesInline();
		FPaths::NormalizeDirectoryName(Root);
		if (!Root.IsEmpty() && IFileManager::Get().DirectoryExists(*Root))
		{
			Out.Roots.AddUnique(Root);
		}
	}
	const FString V = Version();
	if (Out.Roots.IsEmpty())
	{
		Error = TEXT("No Minecraft folder was found. Install Minecraft Java Edition, start 26.3 once from its launcher, then try again, or type the folder it uses.");
		return false;
	}

	Out.Profile = FindUnder(Out.Roots, FString::Printf(TEXT("versions/%s/%s.json"), *V, *V));
	if (Out.Profile.IsEmpty())
	{
		Out.Profile = FindUnder(Out.Roots, FString::Printf(TEXT("meta/net.minecraft/%s.json"), *V));
	}
	if (Out.Profile.IsEmpty())
	{
		Error = FString::Printf(TEXT("Minecraft %s is not installed here. Pick 26.3 in the official launcher and press Play once, then try again."), *V);
		return false;
	}
	const TSharedPtr<FJsonObject> Profile = ReadJson(Out.Profile);
	if (!Profile)
	{
		Error = FString::Printf(TEXT("Can't read %s."), *Out.Profile);
		return false;
	}
	const TSharedPtr<FJsonObject>* Downloads;
	const TSharedPtr<FJsonObject>* Client;
	if (!Profile->TryGetObjectField(TEXT("downloads"), Downloads) || !(*Downloads)->TryGetObjectField(TEXT("client"), Client)
		|| !(*Client)->TryGetStringField(TEXT("sha1"), Out.ClientSha1))
	{
		Error = TEXT("The version file has no client download; it is not the official one.");
		return false;
	}
	const TSharedPtr<FJsonObject>* Index;
	if (!Profile->TryGetObjectField(TEXT("assetIndex"), Index) || !(*Index)->TryGetStringField(TEXT("id"), Out.AssetIndexId))
	{
		Error = TEXT("The version file has no asset index.");
		return false;
	}

	// The client jar: the launcher's copy, or Prism's (a library). Whichever one is the real 26.3 by its hash.
	for (const FString& Relative : { FString::Printf(TEXT("versions/%s/%s.jar"), *V, *V), FString::Printf(TEXT("libraries/com/mojang/minecraft/%s/minecraft-%s-client.jar"), *V, *V) })
	{
		for (const FString& Root : Out.Roots)
		{
			const FString Path = Root / Relative;
			if (IFileManager::Get().FileExists(*Path) && Sha1Of(Path) == Out.ClientSha1)
			{
				Out.ClientJar = Path;
				break;
			}
		}
		if (!Out.ClientJar.IsEmpty())
		{
			break;
		}
	}
	if (Out.ClientJar.IsEmpty())
	{
		Error = FString::Printf(TEXT("The Minecraft %s game file is missing or not the original. Press Play on 26.3 in the launcher once so it downloads it, then try again."), *V);
		return false;
	}

	Out.AssetIndex = FindUnder(Out.Roots, FString::Printf(TEXT("assets/indexes/%s.json"), *Out.AssetIndexId));
	if (Out.AssetIndex.IsEmpty())
	{
		Error = TEXT("Minecraft's sounds and pictures have not been downloaded yet. Press Play on 26.3 in the launcher and get to the title screen once, then try again.");
		return false;
	}

	const TSharedPtr<FJsonObject>* JavaVersion;
	FString Component = TEXT("java-runtime-epsilon");
	int32 Major = 25;
	if (Profile->TryGetObjectField(TEXT("javaVersion"), JavaVersion))
	{
		(*JavaVersion)->TryGetStringField(TEXT("component"), Component);
		(*JavaVersion)->TryGetNumberField(TEXT("majorVersion"), Major);
	}
	Out.Java = FindJava(Out.Roots, Component, Major);
	if (Out.Java.IsEmpty())
	{
		Error = FString::Printf(TEXT("The Java %d that Minecraft 26.3 uses was not found. Press Play on 26.3 in the launcher once so it installs it, then try again."), Major);
		return false;
	}
	return true;
}

FString FMcCacheBuilder::GetError() const
{
	FScopeLock ScopeLock(&Lock);
	return Error;
}

FString FMcCacheBuilder::GetStatus() const
{
	FScopeLock ScopeLock(&Lock);
	return Status;
}

void FMcCacheBuilder::SetStatus(const FString& Text)
{
	FScopeLock ScopeLock(&Lock);
	Status = Text;
}

void FMcCacheBuilder::Start(const FSources& Sources)
{
	bRunning = true;
	bDone = false;
	Progress = 0.0f;
	Async(EAsyncExecution::Thread, [this, Sources]()
	{
		FString Failure;
		const bool bOk = Run(Sources, Failure);
		{
			FScopeLock ScopeLock(&Lock);
			Error = bOk ? FString() : Failure;
		}
		if (!bOk)
		{
			UE_LOG(LogMcCore, Error, TEXT("Cache setup failed: %s"), *Failure);
		}
		bDone = bOk;
		bRunning = false;
	});
}

bool FMcCacheBuilder::Run(const FSources& Sources, FString& OutError)
{
	IFileManager& Files = IFileManager::Get();
	const FString Root = McCache::Root();
	Files.MakeDirectory(*Root, true);
	// Finished once the manifest is written, so a build that stops half way is simply done again.
	Files.Delete(*(Root / TEXT("manifest.json")));

	const FString Client = Root / TEXT("client");
	const FString ClientTemp = Root / TEXT("client.part");
	Files.DeleteDirectory(*ClientTemp, false, true);
	if (!UnpackJar(Sources, ClientTemp, OutError))
	{
		return false;
	}
	Files.DeleteDirectory(*Client, false, true);
	if (!Files.Move(*Client, *ClientTemp, true, true, false, true))
	{
		OutError = FString::Printf(TEXT("Can't move %s into place."), *ClientTemp);
		return false;
	}

	if (!CopyAssets(Sources, Root / TEXT("assets"), OutError))
	{
		return false;
	}

	const FString Data = Root / TEXT("data");
	const FString DataTemp = Root / TEXT("data.part");
	Files.DeleteDirectory(*DataTemp, false, true);
	if (!RunGenerator(Sources, DataTemp, OutError))
	{
		return false;
	}
	Files.DeleteDirectory(*Data, false, true);
	if (!Files.Move(*Data, *DataTemp, true, true, false, true))
	{
		OutError = FString::Printf(TEXT("Can't move %s into place."), *DataTemp);
		return false;
	}

	// The same manifest.json tools/fetch_cache.py writes.
	const TSharedRef<FJsonObject> Steps = MakeShared<FJsonObject>();
	for (const TCHAR* Step : { TEXT("client_jar"), TEXT("assets"), TEXT("reports") })
	{
		Steps->SetBoolField(Step, true);
	}
	const TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetStringField(TEXT("version"), Version());
	Manifest->SetStringField(TEXT("asset_index"), Sources.AssetIndexId);
	Manifest->SetStringField(TEXT("client_sha1"), Sources.ClientSha1);
	Manifest->SetStringField(TEXT("built_by"), TEXT("the game's first-run setup"));
	Manifest->SetObjectField(TEXT("steps"), Steps);
	Manifest->SetStringField(TEXT("updated"), FDateTime::UtcNow().ToIso8601());
	FString Text;
	FJsonSerializer::Serialize(Manifest, TJsonWriterFactory<>::Create(&Text));
	if (!FFileHelper::SaveStringToFile(Text, *(Root / TEXT("manifest.json"))))
	{
		OutError = TEXT("Can't write the cache's manifest.");
		return false;
	}
	Progress = 1.0f;
	SetStatus(TEXT("Done"));
	return true;
}

bool FMcCacheBuilder::UnpackJar(const FSources& Sources, const FString& Out, FString& OutError)
{
	SetStatus(TEXT("Unpacking the game's pictures and data..."));
	TArray<uint8> Jar;
	if (!FFileHelper::LoadFileToArray(Jar, *Sources.ClientJar))
	{
		OutError = FString::Printf(TEXT("Can't read %s."), *Sources.ClientJar);
		return false;
	}

	// The zip's end record, then its central directory: the list of files, with where each is.
	int32 End = INDEX_NONE;
	for (int32 At = Jar.Num() - 22; At >= FMath::Max(0, Jar.Num() - 22 - 65535); --At)
	{
		if (U32(&Jar[At]) == 0x06054b50)
		{
			End = At;
			break;
		}
	}
	if (End == INDEX_NONE)
	{
		OutError = TEXT("The game file is not a zip.");
		return false;
	}
	const int32 Count = U16(&Jar[End + 10]);
	uint32 Directory = U32(&Jar[End + 16]);

	struct FEntry
	{
		FString Name;
		uint16 Method;
		uint32 CompressedSize;
		uint32 Size;
		uint32 HeaderAt;
	};
	TArray<FEntry> Entries;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Directory + 46 > static_cast<uint32>(Jar.Num()) || U32(&Jar[Directory]) != 0x02014b50)
		{
			OutError = TEXT("The game file's list of files is damaged.");
			return false;
		}
		const uint8* P = &Jar[Directory];
		const uint16 NameLength = U16(P + 28);
		FEntry Entry;
		Entry.Method = U16(P + 10);
		Entry.CompressedSize = U32(P + 20);
		Entry.Size = U32(P + 24);
		Entry.HeaderAt = U32(P + 42);
		const FUTF8ToTCHAR Name(reinterpret_cast<const ANSICHAR*>(P + 46), NameLength);
		Entry.Name = FString(Name.Length(), Name.Get());
		Directory += 46 + NameLength + U16(P + 30) + U16(P + 32);
		// assets/ is models, textures, fonts and lang; data/ is recipes, loot tables, tags, structures and worldgen.
		if (!Entry.Name.EndsWith(TEXT("/")) && (Entry.Name.StartsWith(TEXT("assets/")) || Entry.Name.StartsWith(TEXT("data/"))))
		{
			Entries.Add(MoveTemp(Entry));
		}
	}

	TArray<uint8> Buffer;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FEntry& Entry = Entries[Index];
		if (!IsSafeRelative(Entry.Name) || static_cast<int64>(Entry.HeaderAt) + 30 > Jar.Num() || U32(&Jar[Entry.HeaderAt]) != 0x04034b50)
		{
			OutError = FString::Printf(TEXT("The game file has a bad entry: %s"), *Entry.Name);
			return false;
		}
		const uint8* Header = &Jar[Entry.HeaderAt];
		const int64 DataAt = static_cast<int64>(Entry.HeaderAt) + 30 + U16(Header + 26) + U16(Header + 28);
		if (DataAt + Entry.CompressedSize > Jar.Num())
		{
			OutError = FString::Printf(TEXT("The game file is cut short at %s"), *Entry.Name);
			return false;
		}
		const uint8* Data = &Jar[DataAt];
		if (Entry.Size == 0)
		{
			// Empty marker files (.mcassetsroot) have nothing to inflate.
			Buffer.Reset();
		}
		else if (Entry.Method == 0)
		{
			Buffer.SetNumUninitialized(Entry.Size);
			FMemory::Memcpy(Buffer.GetData(), Data, Entry.Size);
		}
		else if (Entry.Method == 8)
		{
			Buffer.SetNumUninitialized(Entry.Size);
			// Raw deflate, with no zlib header: a negative window size.
			if (!FCompression::UncompressMemory(NAME_Zlib, Buffer.GetData(), Entry.Size, Data, Entry.CompressedSize, COMPRESS_NoFlags, static_cast<uintptr_t>(-15)))
			{
				OutError = FString::Printf(TEXT("Can't unpack %s"), *Entry.Name);
				return false;
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("%s is packed in a way this game does not know."), *Entry.Name);
			return false;
		}
		if (!FFileHelper::SaveArrayToFile(Buffer, *(Out / Entry.Name)))
		{
			OutError = FString::Printf(TEXT("Can't write %s. Is the disk full?"), *(Out / Entry.Name));
			return false;
		}
		if (Index % 64 == 0)
		{
			Progress = UnpackShare * Index / Entries.Num();
		}
	}
	Progress = UnpackShare;
	UE_LOG(LogMcCore, Log, TEXT("Cache setup: unpacked %d files from the client jar."), Entries.Num());
	return true;
}

bool FMcCacheBuilder::CopyAssets(const FSources& Sources, const FString& Out, FString& OutError)
{
	SetStatus(TEXT("Copying sounds and the title screen..."));
	const TSharedPtr<FJsonObject> Index = ReadJson(Sources.AssetIndex);
	const TSharedPtr<FJsonObject>* Objects;
	if (!Index || !Index->TryGetObjectField(TEXT("objects"), Objects))
	{
		OutError = FString::Printf(TEXT("Can't read %s."), *Sources.AssetIndex);
		return false;
	}

	IFileManager& Files = IFileManager::Get();
	int32 Done = 0, Copied = 0, Missing = 0;
	const int32 Total = (*Objects)->Values.Num();
	FString FirstMissing;
	for (const auto& Pair : (*Objects)->Values)
	{
		const TSharedPtr<FJsonObject> Object = Pair.Value->AsObject();
		const FString Hash = Object->GetStringField(TEXT("hash"));
		const int64 Size = static_cast<int64>(Object->GetNumberField(TEXT("size")));
		if (!IsSafeRelative(Pair.Key) || Hash.Len() < 3)
		{
			OutError = FString::Printf(TEXT("The asset index has a bad entry: %s"), *Pair.Key);
			return false;
		}
		const FString Dest = Out / Pair.Key;
		if (Files.FileSize(*Dest) != Size)
		{
			const FString Source = FindUnder(Sources.Roots, FString(TEXT("assets/objects")) / Hash.Left(2) / Hash);
			if (Source.IsEmpty())
			{
				if (++Missing == 1)
				{
					FirstMissing = Pair.Key;
				}
			}
			else if (Files.MakeDirectory(*FPaths::GetPath(Dest), true), Files.Copy(*Dest, *Source) != COPY_OK)
			{
				OutError = FString::Printf(TEXT("Can't copy %s. Is the disk full?"), *Pair.Key);
				return false;
			}
			else
			{
				++Copied;
			}
		}
		if (++Done % 32 == 0)
		{
			Progress = UnpackShare + AssetsShare * Done / Total;
		}
	}
	// The sounds are what the game plays; a few other objects (Realms pictures) may be missing without harm.
	if (Files.FileSize(*(Out / TEXT("minecraft/sounds.json"))) <= 0)
	{
		OutError = TEXT("Minecraft's sounds have not been downloaded yet. Press Play on 26.3 in the launcher and get to the title screen once, then try again.");
		return false;
	}
	if (Missing > 0)
	{
		UE_LOG(LogMcCore, Warning, TEXT("Cache setup: %d of %d asset objects are not in the launcher's folder yet, e.g. %s."), Missing, Total, *FirstMissing);
	}
	Progress = UnpackShare + AssetsShare;
	UE_LOG(LogMcCore, Log, TEXT("Cache setup: %d asset objects, %d copied."), Total, Copied);
	return true;
}

bool FMcCacheBuilder::RunGenerator(const FSources& Sources, const FString& Out, FString& OutError)
{
	SetStatus(TEXT("Listing every block with Minecraft's own data generator..."));
	const TSharedPtr<FJsonObject> Profile = ReadJson(Sources.Profile);
	const TArray<TSharedPtr<FJsonValue>>* Libraries;
	if (!Profile || !Profile->TryGetArrayField(TEXT("libraries"), Libraries))
	{
		OutError = FString::Printf(TEXT("Can't read the libraries in %s."), *Sources.Profile);
		return false;
	}
	// The class path is the game's own libraries for Windows, then the client jar.
	FString ClassPath;
	for (const TSharedPtr<FJsonValue>& Value : *Libraries)
	{
		const TSharedPtr<FJsonObject> Library = Value->AsObject();
		const TSharedPtr<FJsonObject>* Downloads;
		const TSharedPtr<FJsonObject>* Artifact;
		FString Path;
		if (!AppliesToWindows(*Library) || !Library->TryGetObjectField(TEXT("downloads"), Downloads)
			|| !(*Downloads)->TryGetObjectField(TEXT("artifact"), Artifact) || !(*Artifact)->TryGetStringField(TEXT("path"), Path))
		{
			continue;
		}
		const FString File = FindUnder(Sources.Roots, FString(TEXT("libraries")) / Path);
		if (File.IsEmpty())
		{
			OutError = FString::Printf(TEXT("The launcher is missing a game library (%s). Press Play on 26.3 once so it downloads it, then try again."), *FPaths::GetCleanFilename(Path));
			return false;
		}
		ClassPath += FPaths::ConvertRelativePathToFull(File).Replace(TEXT("/"), TEXT("\\")) + TEXT(";");
	}
	ClassPath += FPaths::ConvertRelativePathToFull(Sources.ClientJar).Replace(TEXT("/"), TEXT("\\"));

	const FString Params = FString::Printf(TEXT("-cp \"%s\" net.minecraft.data.Main --reports --output \"%s\""), *ClassPath, *FPaths::ConvertRelativePathToFull(Out).Replace(TEXT("/"), TEXT("\\")));
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	FPlatformProcess::CreatePipe(PipeRead, PipeWrite);
	FProcHandle Process = FPlatformProcess::CreateProc(*Sources.Java, *Params, false, true, true, nullptr, 0, *McCache::Root(), PipeWrite, nullptr);
	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		OutError = FString::Printf(TEXT("Can't start Java (%s)."), *Sources.Java);
		return false;
	}
	// It takes about 20 seconds and doesn't say how far along it is: the bar creeps towards the end of its share.
	FString Log;
	const double Started = FPlatformTime::Seconds();
	while (FPlatformProcess::IsProcRunning(Process))
	{
		Log += FPlatformProcess::ReadPipe(PipeRead);
		const float Elapsed = static_cast<float>(FPlatformTime::Seconds() - Started);
		Progress = UnpackShare + AssetsShare + GeneratorShare * (1.0f - FMath::Exp(-Elapsed / 15.0f)) * 0.95f;
		FPlatformProcess::Sleep(0.1f);
	}
	Log += FPlatformProcess::ReadPipe(PipeRead);
	int32 Code = -1;
	FPlatformProcess::GetProcReturnCode(Process, &Code);
	FPlatformProcess::CloseProc(Process);
	FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
	FFileHelper::SaveStringToFile(Log, *(McCache::Root() / TEXT("datagen.log")));

	if (Code != 0 || !IFileManager::Get().FileExists(*(Out / TEXT("reports/blocks.json"))))
	{
		OutError = FString::Printf(TEXT("Minecraft's data generator failed (code %d); see %s"), Code, *(McCache::Root() / TEXT("datagen.log")));
		return false;
	}
	UE_LOG(LogMcCore, Log, TEXT("Cache setup: the data generator finished."));
	return true;
}
