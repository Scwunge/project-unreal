#include "McParticleDefs.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "McCache.h"

namespace
{
	struct FDefs
	{
		TMap<FName, TArray<FName>> Textures;
		TArray<FName> All;
		TArray<FName> Names;
	};

	const FDefs& Load()
	{
		static const FDefs Defs = []
		{
			FDefs Result;
			const FString Folder = McCache::Asset(TEXT("particles"));
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);
			Files.Sort();
			for (const FString& File : Files)
			{
				FString Text;
				TSharedPtr<FJsonObject> Root;
				if (!FFileHelper::LoadFileToString(Text, *(Folder / File)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
				{
					continue;
				}
				const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
				if (!Root->TryGetArrayField(TEXT("textures"), List))
				{
					continue;
				}
				const FName Name(*(TEXT("minecraft:") + FPaths::GetBaseFilename(File)));
				TArray<FName>& Frames = Result.Textures.Add(Name);
				for (const TSharedPtr<FJsonValue>& Value : *List)
				{
					FString Id = Value->AsString();
					Id.RemoveFromStart(TEXT("minecraft:"));
					const FName Texture(*(TEXT("minecraft:particle/") + Id));
					Frames.Add(Texture);
					Result.All.AddUnique(Texture);
				}
				Result.Names.Add(Name);
			}
			return Result;
		}();
		return Defs;
	}
}

const TArray<FName>& McParticleDefs::GetTextures(FName Particle)
{
	static const TArray<FName> None;
	FString Text = Particle.ToString();
	if (!Text.Contains(TEXT(":")))
	{
		Text = TEXT("minecraft:") + Text;
	}
	const TArray<FName>* Found = Load().Textures.Find(FName(*Text));
	return Found ? *Found : None;
}

const TArray<FName>& McParticleDefs::GetAllTextures()
{
	return Load().All;
}

const TArray<FName>& McParticleDefs::GetNames()
{
	return Load().Names;
}
