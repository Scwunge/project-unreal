#include "McLocalPlayer.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString GetOptionsPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("options.json");
	}
}

FString UMcLocalPlayer::GetNickname() const
{
#if !UE_BUILD_SHIPPING
	// Development: -McName=<name>, for several players from one machine (they share options.json).
	FString DevName;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McName="), DevName) && FMcClientOptions::IsValidPlayerName(DevName))
	{
		return DevName;
	}
#endif
	const FMcClientOptions Options = FMcClientOptions::Load();
	return FMcClientOptions::IsValidPlayerName(Options.PlayerName) ? Options.PlayerName : Super::GetNickname();
}

FMcClientOptions FMcClientOptions::Load()
{
	FMcClientOptions Options;
	FString Text;
	TSharedPtr<FJsonObject> Object;
	if (FFileHelper::LoadFileToString(Text, *GetOptionsPath()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && Object)
	{
		Object->TryGetStringField(TEXT("name"), Options.PlayerName);
		Object->TryGetStringField(TEXT("lastServer"), Options.LastServer);
		FString Graphics;
		Options.bEnhancedGraphics = Object->TryGetStringField(TEXT("graphics"), Graphics) && Graphics == TEXT("enhanced");
		Object->TryGetBoolField(TEXT("hdTextures"), Options.bHdTextures);
	}
	return Options;
}

void FMcClientOptions::Save() const
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("name"), PlayerName);
	Object->SetStringField(TEXT("lastServer"), LastServer);
	Object->SetStringField(TEXT("graphics"), bEnhancedGraphics ? TEXT("enhanced") : TEXT("vanilla"));
	Object->SetBoolField(TEXT("hdTextures"), bHdTextures);
	FString Text;
	FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *GetOptionsPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FMcClientOptions::IsValidPlayerName(const FString& Name)
{
	if (Name.Len() < 3 || Name.Len() > 16)
	{
		return false;
	}
	for (const TCHAR Char : Name)
	{
		if (!((Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z') || (Char >= '0' && Char <= '9') || Char == '_'))
		{
			return false;
		}
	}
	return true;
}
