#include "McCache.h"
#include "McCore.h"
#include "McResourcePacks.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace McCache
{
	FString Root()
	{
		FString Override;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McCache="), Override))
		{
			return FPaths::ConvertRelativePathToFull(Override);
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("cache"));
	}

	FString Report(const FString& RelativePath)
	{
		return Root() / TEXT("data/reports") / RelativePath;
	}

	FString Data(const FString& RelativePath)
	{
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
		return Root() / TEXT("client/assets/minecraft") / RelativePath;
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
