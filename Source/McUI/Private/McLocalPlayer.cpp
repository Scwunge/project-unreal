#include "McLocalPlayer.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "McPerf.h"
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
		Object->TryGetBoolField(TEXT("showFps"), Options.bShowFps);
		Object->TryGetBoolField(TEXT("showSubtitles"), Options.bShowSubtitles);
		Object->TryGetBoolField(TEXT("rayTracing"), Options.bRayTracing);
		double Number = 0.0;
		if (Object->TryGetNumberField(TEXT("quality"), Number)) { Options.Quality = FMath::Clamp(static_cast<int32>(Number), 0, 3); }
		if (Object->TryGetNumberField(TEXT("fov"), Number)) { Options.FieldOfView = FMath::Clamp(static_cast<float>(Number), 30.0f, 110.0f); }
		if (Object->TryGetNumberField(TEXT("renderDistance"), Number)) { Options.RenderDistance = FMath::Clamp(static_cast<int32>(Number), 2, 32); }
		if (Object->TryGetNumberField(TEXT("sensitivity"), Number)) { Options.Sensitivity = FMath::Clamp(static_cast<float>(Number), 0.0f, 2.0f); }
		if (Object->TryGetNumberField(TEXT("masterVolume"), Number)) { Options.MasterVolume = FMath::Clamp(static_cast<float>(Number), 0.0f, 1.0f); }
		if (Object->TryGetNumberField(TEXT("maxFps"), Number)) { Options.MaxFps = FMath::Clamp(FMath::RoundToInt(Number / 10.0) * 10, 10, UnlimitedFps); }
		Object->TryGetBoolField(TEXT("vsync"), Options.bVSync);
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
	Object->SetBoolField(TEXT("showFps"), bShowFps);
	Object->SetBoolField(TEXT("showSubtitles"), bShowSubtitles);
	Object->SetBoolField(TEXT("rayTracing"), bRayTracing);
	Object->SetNumberField(TEXT("quality"), Quality);
	Object->SetNumberField(TEXT("fov"), FieldOfView);
	Object->SetNumberField(TEXT("renderDistance"), RenderDistance);
	Object->SetNumberField(TEXT("sensitivity"), Sensitivity);
	Object->SetNumberField(TEXT("masterVolume"), MasterVolume);
	Object->SetNumberField(TEXT("maxFps"), MaxFps);
	Object->SetBoolField(TEXT("vsync"), bVSync);
	FString Text;
	FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *GetOptionsPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FMcClientOptions::ApplyFrameRate() const
{
	if (McPerf::IsOn())
	{
		return;
	}
	if (IConsoleVariable* Cap = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		Cap->Set(MaxFps >= UnlimitedFps ? 0.0f : static_cast<float>(MaxFps), ECVF_SetByGameSetting);
	}
	if (IConsoleVariable* Sync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		Sync->Set(bVSync ? 1 : 0, ECVF_SetByGameSetting);
	}
	if (GEngine)
	{
		// The engine's own smoothing would cap the frame rate a second time.
		GEngine->bSmoothFrameRate = false;
	}
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
