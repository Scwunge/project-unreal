#include "McDataPacks.h"
#include "McCache.h"
#include "McCore.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace McData
{
	namespace
	{
		FCriticalSection GLock;
		TSharedPtr<const FMcPackStack> GStack;
		bool GStackRead = false;
		uint32 GGeneration = 1;
		FCriticalSection GExtractLock;

		FString GetSelectionFile()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McDataPacks.json"));
		}

		FString ResolveName(const FString& Name)
		{
			if (IFileManager::Get().FileExists(*Name) || IFileManager::Get().DirectoryExists(*Name))
			{
				return Name;
			}
			const FString InFolder = GetFolder() / Name;
			return IFileManager::Get().FileExists(*InFolder) || IFileManager::Get().DirectoryExists(*InFolder) ? InFolder : FString();
		}

		TSharedRef<const FMcPackStack> OpenNames(const TArray<FString>& Names)
		{
			TArray<TSharedPtr<FMcResourcePack>> Packs;
			for (const FString& Name : Names)
			{
				const FString Path = ResolveName(Name);
				FString Error;
				TSharedPtr<FMcResourcePack> Pack = Path.IsEmpty() ? nullptr : FMcResourcePack::Open(Path, &Error);
				if (Pack)
				{
					Pack->SetName(FPaths::GetCleanFilename(Path));
					Packs.Add(Pack);
				}
				else
				{
					UE_LOG(LogMcCore, Warning, TEXT("Data pack %s can't be used: %s"), *Name, Path.IsEmpty() ? TEXT("not found") : *Error);
				}
			}
			return MakeShared<FMcPackStack>(MoveTemp(Packs));
		}

		TArray<FString> LoadSavedSelection()
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

		// A pack's file as a path on disk: the folder's own, or the zip's entry taken out once into the cache.
		FString OnDisk(const FMcResourcePack& Pack, const FString& InPack)
		{
			if (!Pack.GetInfo().bZip)
			{
				return Pack.GetFolderFile(InPack);
			}
			const FString Target = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McDataPackCache")
				/ FMD5::HashAnsiString(*Pack.GetStamp()).Left(12) / InPack);
			FScopeLock Lock(&GExtractLock);
			if (IFileManager::Get().FileExists(*Target))
			{
				return Target;
			}
			TArray<uint8> Bytes;
			if (!Pack.Read(InPack, Bytes))
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
		}

		// "minecraft:a/b" -> namespace and path; no namespace is the game's.
		void SplitId(const FString& Id, FString& Namespace, FString& Path)
		{
			if (!Id.Split(TEXT(":"), &Namespace, &Path))
			{
				Namespace = TEXT("minecraft");
				Path = Id;
			}
		}
	}

	FString GetFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("datapacks"));
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
		if (FParse::Value(FCommandLine::Get(), TEXT("-McDataPacks="), List, false))
		{
			List.ParseIntoArray(Names, TEXT(","));
			for (FString& Name : Names)
			{
				Name.TrimStartAndEndInline();
			}
			Names.RemoveAll([](const FString& Name) { return Name.IsEmpty(); });
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

	void SetStack(TSharedRef<const FMcPackStack> Stack)
	{
		FScopeLock Lock(&GLock);
		GStack = Stack;
		GStackRead = true;
		++GGeneration;
	}

	uint32 GetGeneration()
	{
		FScopeLock Lock(&GLock);
		return GGeneration;
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
		SetStack(OpenNames(Names));
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Packs;
		for (const FString& Name : GetSelectedNames())
		{
			Packs.Add(MakeShared<FJsonValueString>(Name));
		}
		Json->SetArrayField(TEXT("packs"), Packs);
		FString Text;
		FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
		FFileHelper::SaveStringToFile(Text, *GetSelectionFile());
	}

	TArray<FFolderEntry> ScanFolder()
	{
		TArray<FFolderEntry> Result;
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(GetFolder() / TEXT("*")), true, true);
		Names.Sort();
		for (const FString& Name : Names)
		{
			FFolderEntry Entry;
			Entry.Name = Name;
			Entry.Path = GetFolder() / Name;
			if (!IFileManager::Get().DirectoryExists(*Entry.Path) && !Name.EndsWith(TEXT(".zip")))
			{
				continue;
			}
			Entry.Pack = FMcResourcePack::Open(Entry.Path, &Entry.Error);
			Result.Add(MoveTemp(Entry));
		}
		return Result;
	}

	TArray<FFile> List(const FString& Folder, const FString& Ext)
	{
		TMap<FString, TArray<FString>> ById;
		// The game's: data/minecraft/<Folder>/.
		{
			const FString Root = McCache::Root() / TEXT("client/data/minecraft") / Folder + TEXT("/");
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *Root, *(TEXT("*") + Ext), true, false);
			for (const FString& File : Files)
			{
				FString Relative = File;
				FPaths::MakePathRelativeTo(Relative, *Root);
				Relative = Relative.Replace(TEXT("\\"), TEXT("/"));
				Relative.LeftChopInline(Ext.Len());
				ById.FindOrAdd(TEXT("minecraft:") + Relative).Add(File);
			}
		}
		const TSharedRef<const FMcPackStack> Stack = GetStack();
		for (int32 Index = 0; Index < Stack->Num(); ++Index)
		{
			const FMcResourcePack& Pack = Stack->Get(Index);
			TArray<FString> Files;
			Pack.ListFiles(TEXT("data/"), Files);
			Files.Sort();
			for (const FString& File : Files)
			{
				// data/<ns>/<Folder>/<path><Ext>
				const FString Rest = File.Mid(5);
				FString Namespace, InNamespace;
				if (!Rest.Split(TEXT("/"), &Namespace, &InNamespace) || !InNamespace.StartsWith(Folder + TEXT("/")) || !InNamespace.EndsWith(Ext))
				{
					continue;
				}
				FString Relative = InNamespace.Mid(Folder.Len() + 1);
				Relative.LeftChopInline(Ext.Len());
				if (Relative.IsEmpty())
				{
					continue;
				}
				const FString Disk = OnDisk(Pack, File);
				if (!Disk.IsEmpty())
				{
					ById.FindOrAdd(Namespace + TEXT(":") + Relative).Add(Disk);
				}
			}
		}
		TArray<FString> Keys;
		ById.GetKeys(Keys);
		Keys.Sort();
		TArray<FFile> Out;
		for (const FString& Key : Keys)
		{
			FFile Entry;
			Entry.Id = Key;
			Entry.Paths = MoveTemp(ById[Key]);
			Out.Add(MoveTemp(Entry));
		}
		return Out;
	}

	TArray<FString> ReadTagValues(const TArray<FString>& Paths)
	{
		TArray<FString> Values;
		for (const FString& Path : Paths)
		{
			const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Path);
			if (!Json.IsValid())
			{
				continue;
			}
			bool bReplace = false;
			if (Json->TryGetBoolField(TEXT("replace"), bReplace) && bReplace)
			{
				Values.Reset();
			}
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!Json->TryGetArrayField(TEXT("values"), List))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				FString Entry;
				if (Value->TryGetString(Entry) || (Value->Type == EJson::Object && Value->AsObject()->TryGetStringField(TEXT("id"), Entry)))
				{
					Values.Add(Entry);
				}
			}
		}
		return Values;
	}

	bool TryResolve(const FString& Id, const FString& Folder, const FString& Ext, FString& OutPath)
	{
		const TSharedRef<const FMcPackStack> Stack = GetStack();
		if (Stack->Num() == 0)
		{
			return false;
		}
		FString Namespace, Path;
		SplitId(Id, Namespace, Path);
		const FString InPack = FString::Printf(TEXT("data/%s/%s/%s%s"), *Namespace, *Folder, *Path, *Ext);
		const int32 Index = Stack->Find(InPack);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		OutPath = OnDisk(Stack->Get(Index), InPack);
		return !OutPath.IsEmpty();
	}

	bool TryResolveGame(const FString& Relative, FString& OutPath)
	{
		const TSharedRef<const FMcPackStack> Stack = GetStack();
		if (Stack->Num() == 0)
		{
			return false;
		}
		const FString InPack = TEXT("data/minecraft/") + Relative;
		const int32 Index = Stack->Find(InPack);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		OutPath = OnDisk(Stack->Get(Index), InPack);
		return !OutPath.IsEmpty();
	}

	FString PathOf(const FString& Id, const FString& Folder, const FString& Ext)
	{
		FString Path;
		if (TryResolve(Id, Folder, Ext, Path))
		{
			return Path;
		}
		FString Namespace, Rest;
		SplitId(Id, Namespace, Rest);
		if (Namespace != TEXT("minecraft"))
		{
			return FString();
		}
		Path = McCache::Root() / TEXT("client/data/minecraft") / Folder / Rest + Ext;
		return IFileManager::Get().FileExists(*Path) ? Path : FString();
	}

	FSimpleMulticastDelegate& OnReload()
	{
		static FSimpleMulticastDelegate Delegate;
		return Delegate;
	}

	void Reload()
	{
		{
			FScopeLock Lock(&GLock);
			++GGeneration;
		}
		OnReload().Broadcast();
	}
}
