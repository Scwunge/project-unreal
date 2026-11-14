#include "McCache.h"
#include "McCore.h"
#include "McResourcePacks.h"
#include "McDataPacks.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace McCache
{
	bool IsCompleteAt(const FString& Folder)
	{
		FString Text;
		TSharedPtr<FJsonObject> Manifest;
		if (!FFileHelper::LoadFileToString(Text, *(Folder / TEXT("manifest.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest) || !Manifest.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Steps;
		if (!Manifest->TryGetObjectField(TEXT("steps"), Steps))
		{
			return false;
		}
		// The steps tools/fetch_cache.py and the game's own setup both write.
		for (const TCHAR* Step : { TEXT("client_jar"), TEXT("assets"), TEXT("reports") })
		{
			bool bDone = false;
			if (!(*Steps)->TryGetBoolField(Step, bDone) || !bDone)
			{
				return false;
			}
		}
		return true;
	}

	FString UserRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPlatformProcess::UserSettingsDir()) / TEXT("McUnreal/cache");
	}

	bool IsComplete()
	{
		return IsCompleteAt(Root());
	}

	FString Root()
	{
		static const FString Folder = []
		{
			FString Override;
			if (FParse::Value(FCommandLine::Get(), TEXT("-McCache="), Override))
			{
				return FPaths::ConvertRelativePathToFull(Override);
			}
			const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("cache"));
			return IsCompleteAt(Project) ? Project : UserRoot();
		}();
		return Folder;
	}

	FString Report(const FString& RelativePath)
	{
		return Root() / TEXT("data/reports") / RelativePath;
	}

	FString Data(const FString& RelativePath)
	{
		// A data pack that has the file (not a folder) wins over the cache.
		FString FromPack;
		if (!RelativePath.EndsWith(TEXT("/")) && FPaths::GetExtension(RelativePath).Len() > 0 && McData::TryResolveGame(RelativePath, FromPack))
		{
			return FromPack;
		}
		return Root() / TEXT("client/data/minecraft") / RelativePath;
	}

	FString Asset(const FString& RelativePath)
	{
		// A resource pack that has the file wins over the cache.
		FString FromPack;
		if (McPacks::TryResolve(RelativePath, FromPack))
		{
			return FromPack;
		}
		// A namespace of a pack's own has no copy in the jar: the path that follows does not exist.
		int32 Colon = INDEX_NONE;
		const int32 Slash = RelativePath.Find(TEXT("/"));
		if (RelativePath.FindChar(TEXT(':'), Colon) && (Slash == INDEX_NONE || Colon < Slash))
		{
			return Root() / TEXT("client/assets") / RelativePath.Left(Colon) / RelativePath.Mid(Colon + 1);
		}
		return Root() / TEXT("client/assets/minecraft") / RelativePath;
	}

	FString RelativeOf(const FString& Id, const FString& Directory, const FString& Extension)
	{
		int32 Colon = INDEX_NONE;
		if (Id.FindChar(TEXT(':'), Colon) && Id.Left(Colon) != TEXT("minecraft"))
		{
			return Id.Left(Colon) + TEXT(":") + Directory + Id.Mid(Colon + 1) + Extension;
		}
		return Directory + PathOf(Id) + Extension;
	}

	FString AssetOf(const FString& Id, const FString& Directory, const FString& Extension)
	{
		return Asset(RelativeOf(Id, Directory, Extension));
	}

	TSharedPtr<FJsonObject> LoadJsonObject(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogMcCore, Error, TEXT("Can't read %s. Run tools/fetch_cache.py to fill cache/."), *Path);
			return nullptr;
		}

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			UE_LOG(LogMcCore, Error, TEXT("%s is not a JSON object: %s"), *Path, *Reader->GetErrorMessage());
			return nullptr;
		}
		return Object;
	}

	FTextureMeta ReadTextureMeta(const FString& PngPath)
	{
		FTextureMeta Meta;
		FString Text;
		const FString MetaPath = PngPath + TEXT(".mcmeta");
		if (!FPaths::FileExists(MetaPath) || !FFileHelper::LoadFileToString(Text, *MetaPath))
		{
			return Meta;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedPtr<FJsonObject>* Texture = nullptr;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid() && Root->TryGetObjectField(TEXT("texture"), Texture))
		{
			(*Texture)->TryGetBoolField(TEXT("blur"), Meta.bBlur);
			Meta.bHasClamp = (*Texture)->TryGetBoolField(TEXT("clamp"), Meta.bClamp);
		}
		return Meta;
	}

	EVillagerHat ReadVillagerHat(const FString& PngPath)
	{
		FString Text;
		const FString MetaPath = PngPath + TEXT(".mcmeta");
		if (!FPaths::FileExists(MetaPath) || !FFileHelper::LoadFileToString(Text, *MetaPath))
		{
			return EVillagerHat::None;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedPtr<FJsonObject>* Villager = nullptr;
		FString Hat;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid() && Root->TryGetObjectField(TEXT("villager"), Villager)
			&& (*Villager)->TryGetStringField(TEXT("hat"), Hat))
		{
			if (Hat == TEXT("partial"))
			{
				return EVillagerHat::Partial;
			}
			if (Hat == TEXT("full"))
			{
				return EVillagerHat::Full;
			}
		}
		return EVillagerHat::None;
	}

	EVillagerHat VillagerHat(const FString& Category, const FString& Name)
	{
		return ReadVillagerHat(Asset(TEXT("textures/entity/villager/") + Category / PathOf(Name) + TEXT(".png")));
	}

	FString Namespaced(const FString& Id)
	{
		return Id.Contains(TEXT(":")) ? Id : TEXT("minecraft:") + Id;
	}

	FString PathOf(const FString& Id)
	{
		int32 Colon;
		return Id.FindChar(TEXT(':'), Colon) ? Id.Mid(Colon + 1) : Id;
	}
}
