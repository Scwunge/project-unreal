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
		&& bFlying == Other.bFlying && bHasSurvival == Other.bHasSurvival && bCreative == Other.bCreative
		&& SelectedSlot == Other.SelectedSlot && Stats.OrderIndependentCompareEqual(Other.Stats) && Slots == Other.Slots;
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
	if (Player.bHasSurvival)
	{
		Object->SetBoolField(TEXT("creative"), Player.bCreative);
		Object->SetNumberField(TEXT("selected_slot"), Player.SelectedSlot);
		const TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
		for (const TPair<FString, double>& Pair : Player.Stats)
		{
			Stats->SetNumberField(Pair.Key, Pair.Value);
		}
		Object->SetObjectField(TEXT("stats"), Stats);
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FMcSavedSlot& Saved : Player.Slots)
		{
			if (Saved.Stack.IsEmpty())
			{
				continue;
			}
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("slot"), Saved.Slot);
			Entry->SetStringField(TEXT("id"), Saved.Stack.Item.ToString());
			Entry->SetNumberField(TEXT("count"), Saved.Stack.Count);
			if (!Saved.Stack.Components.IsEmpty())
			{
				const TSharedRef<FJsonObject> Components = MakeShared<FJsonObject>();
				for (const TPair<FName, TSharedPtr<FJsonValue>>& Component : Saved.Stack.Components)
				{
					Components->SetField(Component.Key.ToString(), Component.Value.IsValid() ? Component.Value : MakeShared<FJsonValueNull>());
				}
				Entry->SetObjectField(TEXT("components"), Components);
			}
			Items.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Object->SetArrayField(TEXT("inventory"), Items);
	}
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
	const TSharedPtr<FJsonObject>* Stats = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Object->TryGetObjectField(TEXT("stats"), Stats) && Object->TryGetArrayField(TEXT("inventory"), Items))
	{
		Player.bHasSurvival = true;
		Object->TryGetBoolField(TEXT("creative"), Player.bCreative);
		if (Object->TryGetNumberField(TEXT("selected_slot"), Value))
		{
			Player.SelectedSlot = FMath::Clamp(FMath::RoundToInt32(Value), 0, 8);
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Stats)->Values)
		{
			double Number = 0.0;
			// A number that isn't one (NaN from a hand-edited file) would poison the health bar.
			if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Number) && FMath::IsFinite(Number))
			{
				Player.Stats.Add(Pair.Key, Number);
			}
		}
		for (const TSharedPtr<FJsonValue>& ItemValue : *Items)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FString ItemId;
			double SlotNumber = 0.0;
			double CountNumber = 0.0;
			if (!ItemValue.IsValid() || !ItemValue->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("id"), ItemId) || ItemId.IsEmpty()
				|| !(*Entry)->TryGetNumberField(TEXT("slot"), SlotNumber) || !(*Entry)->TryGetNumberField(TEXT("count"), CountNumber) || CountNumber < 1.0)
			{
				continue;
			}
			FMcSavedSlot Saved;
			Saved.Slot = FMath::RoundToInt32(SlotNumber);
			Saved.Stack = FMcItemStack(FName(*ItemId), FMath::Min(FMath::RoundToInt32(CountNumber), 99));
			const TSharedPtr<FJsonObject>* Components = nullptr;
			if ((*Entry)->TryGetObjectField(TEXT("components"), Components))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Component : (*Components)->Values)
				{
					Saved.Stack.Set(FName(*Component.Key), Component.Value);
				}
			}
			Player.Slots.Add(MoveTemp(Saved));
		}
	}
	OutPlayer = Player;
	return true;
}
