#include "McWorldSave.h"
#include "McServer.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FMcLevelData::operator==(const FMcLevelData& Other) const
{
	return Version == Other.Version && Seed == Other.Seed && Spawn == Other.Spawn && DayTime == Other.DayTime
		&& Dimension == Other.Dimension && NoiseSettings == Other.NoiseSettings && bFlat == Other.bFlat
		&& FlatPreset == Other.FlatPreset && GameRules.OrderIndependentCompareEqual(Other.GameRules);
}

bool FMcPlayerData::operator==(const FMcPlayerData& Other) const
{
	return Position == Other.Position && Yaw == Other.Yaw && Pitch == Other.Pitch && Dimension == Other.Dimension
		&& bFlying == Other.bFlying;
}

namespace
{
	bool WriteJson(const FString& Path, const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		if (!FJsonSerializer::Serialize(Object, Writer))
		{
			return false;
		}
		IFileManager& Files = IFileManager::Get();
		Files.MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
		const FString Temp = Path + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't write %s"), *Temp);
			return false;
		}
		if (!Files.Move(*Path, *Temp, /*Replace=*/true))
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't move %s over %s"), *Temp, *Path);
			Files.Delete(*Temp);
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return nullptr;
		}
		TSharedPtr<FJsonObject> Object;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object.IsValid())
		{
			UE_LOG(LogMcServer, Error, TEXT("%s isn't valid JSON."), *Path);
			return nullptr;
		}
		return Object;
	}

	// Numbers in JSON are doubles, which hold integers exactly only up to 2^53; seeds are 64-bit, so they're text.
	bool ParseInt64(const FString& Text, int64& Out)
	{
		if (Text.IsEmpty() || !Text.IsNumeric())
		{
			return false;
		}
		Out = FCString::Atoi64(*Text);
		return true;
	}
}

FString McWorldSave::GetWorldsRoot()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Worlds");
}

FString McWorldSave::SanitizeName(const FString& Name)
{
	FString Out;
	for (const TCHAR Char : Name.TrimStartAndEnd())
	{
		Out.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('-') || Char == TEXT('.') ? Char : TEXT('_'));
	}
	// "." and ".." (and any run of dots) would name a folder that isn't the world's.
	FString Trimmed = Out;
	Trimmed.ReplaceInline(TEXT("."), TEXT(""));
	return Trimmed.IsEmpty() ? FString() : Out;
}

FString McWorldSave::GetWorldDir(const FString& Name)
{
	return GetWorldsRoot() / SanitizeName(Name);
}

FString McWorldSave::GetDimensionFolder(const FString& Dimension)
{
	FString Namespace, Path;
	if (!Dimension.Split(TEXT(":"), &Namespace, &Path))
	{
		return SanitizeName(Dimension);
	}
	return SanitizeName(Namespace == TEXT("minecraft") ? Path : Namespace + TEXT("_") + Path);
}

bool McWorldSave::SaveLevel(const FString& WorldDir, const FMcLevelData& Level)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("version"), Level.Version);
	Object->SetStringField(TEXT("seed"), LexToString(Level.Seed));
	Object->SetArrayField(TEXT("spawn"), {
		MakeShared<FJsonValueNumber>(Level.Spawn.X), MakeShared<FJsonValueNumber>(Level.Spawn.Y), MakeShared<FJsonValueNumber>(Level.Spawn.Z) });
	Object->SetNumberField(TEXT("dayTime"), Level.DayTime);
	Object->SetStringField(TEXT("dimension"), Level.Dimension);
	Object->SetStringField(TEXT("noiseSettings"), Level.NoiseSettings);
	Object->SetBoolField(TEXT("flat"), Level.bFlat);
	Object->SetStringField(TEXT("flatPreset"), Level.FlatPreset);
	const TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Rule : Level.GameRules)
	{
		Rules->SetStringField(Rule.Key, Rule.Value);
	}
	Object->SetObjectField(TEXT("gameRules"), Rules);
	return WriteJson(WorldDir / TEXT("level.json"), Object);
}

bool McWorldSave::LoadLevel(const FString& WorldDir, FMcLevelData& OutLevel)
{
	const TSharedPtr<FJsonObject> Object = ReadJson(WorldDir / TEXT("level.json"));
	if (!Object)
	{
		return false;
	}
	FMcLevelData Level;
	int32 Version = 0;
	FString SeedText;
	if (!Object->TryGetNumberField(TEXT("version"), Version) || Version < 1 || Version > Level.Version
		|| !Object->TryGetStringField(TEXT("seed"), SeedText) || !ParseInt64(SeedText, Level.Seed))
	{
		UE_LOG(LogMcServer, Error, TEXT("%s/level.json has no usable version and seed (or is from a newer version)."), *WorldDir);
		return false;
	}
	Level.Version = Version;
	const TArray<TSharedPtr<FJsonValue>>* Spawn = nullptr;
	if (Object->TryGetArrayField(TEXT("spawn"), Spawn) && Spawn->Num() == 3)
	{
		Level.Spawn = FIntVector(FMath::RoundToInt32((*Spawn)[0]->AsNumber()), FMath::RoundToInt32((*Spawn)[1]->AsNumber()),
			FMath::RoundToInt32((*Spawn)[2]->AsNumber()));
	}
	Object->TryGetNumberField(TEXT("dayTime"), Level.DayTime);
	Object->TryGetStringField(TEXT("dimension"), Level.Dimension);
	Object->TryGetStringField(TEXT("noiseSettings"), Level.NoiseSettings);
	Object->TryGetBoolField(TEXT("flat"), Level.bFlat);
	Object->TryGetStringField(TEXT("flatPreset"), Level.FlatPreset);
	const TSharedPtr<FJsonObject>* Rules = nullptr;
	if (Object->TryGetObjectField(TEXT("gameRules"), Rules))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Rule : (*Rules)->Values)
		{
			Level.GameRules.Add(Rule.Key, Rule.Value->AsString());
		}
	}
	OutLevel = MoveTemp(Level);
	return true;
}

bool McWorldSave::SavePlayer(const FString& WorldDir, const FString& PlayerId, const FMcPlayerData& Player)
{
	const FString Id = SanitizeName(PlayerId);
	if (Id.IsEmpty())
	{
		return false;
	}
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetArrayField(TEXT("pos"), {
		MakeShared<FJsonValueNumber>(Player.Position.X), MakeShared<FJsonValueNumber>(Player.Position.Y), MakeShared<FJsonValueNumber>(Player.Position.Z) });
	Object->SetNumberField(TEXT("yaw"), Player.Yaw);
	Object->SetNumberField(TEXT("pitch"), Player.Pitch);
	Object->SetStringField(TEXT("dimension"), Player.Dimension);
	Object->SetBoolField(TEXT("flying"), Player.bFlying);
	return WriteJson(WorldDir / TEXT("playerdata") / Id + TEXT(".json"), Object);
}

bool McWorldSave::LoadPlayer(const FString& WorldDir, const FString& PlayerId, FMcPlayerData& OutPlayer)
{
	const FString Id = SanitizeName(PlayerId);
	if (Id.IsEmpty())
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Object = ReadJson(WorldDir / TEXT("playerdata") / Id + TEXT(".json"));
	const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
	if (!Object || !Object->TryGetArrayField(TEXT("pos"), Pos) || Pos->Num() != 3)
	{
		return false;
	}
	FMcPlayerData Player;
	Player.Position = FVector((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber(), (*Pos)[2]->AsNumber());
	// Someone's hand-edited file can hold anything; a position that isn't a number would send the pawn nowhere.
	if (Player.Position.ContainsNaN())
	{
		return false;
	}
	double Value = 0.0;
	if (Object->TryGetNumberField(TEXT("yaw"), Value))
	{
		Player.Yaw = static_cast<float>(Value);
	}
	if (Object->TryGetNumberField(TEXT("pitch"), Value))
	{
		Player.Pitch = static_cast<float>(Value);
	}
	Object->TryGetStringField(TEXT("dimension"), Player.Dimension);
	Object->TryGetBoolField(TEXT("flying"), Player.bFlying);
	OutPlayer = Player;
	return true;
}
