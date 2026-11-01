#include "McCache.h"
#include "McCore.h"
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
		return Root() / TEXT("client/data/minecraft") / RelativePath;
	}

	FString Asset(const FString& RelativePath)
	{
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
