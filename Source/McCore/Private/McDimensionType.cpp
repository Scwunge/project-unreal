#include "McDimensionType.h"
#include "McCache.h"
#include "McCore.h"
#include "Dom/JsonObject.h"
#include "Misc/Parse.h"

bool FMcDimensionType::Load(const FString& Id, FMcDimensionType& Out)
{
	const FString Path = McCache::Data(TEXT("dimension_type/") + McCache::PathOf(Id) + TEXT(".json"));
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Path);
	if (!Json.IsValid())
	{
		return false;
	}

	FMcDimensionType Result;
	Result.Name = FName(*McCache::Namespaced(Id));
	if (!Json->TryGetNumberField(TEXT("min_y"), Result.MinY) || !Json->TryGetNumberField(TEXT("height"), Result.Height))
	{
		UE_LOG(LogMcCore, Error, TEXT("%s has no min_y or height"), *Path);
		return false;
	}
	Json->TryGetBoolField(TEXT("has_skylight"), Result.bHasSkylight);
	Json->TryGetBoolField(TEXT("has_ceiling"), Result.bHasCeiling);
	Json->TryGetBoolField(TEXT("has_fixed_time"), Result.bHasFixedTime);
	Json->TryGetNumberField(TEXT("coordinate_scale"), Result.CoordinateScale);
	Result.LogicalHeight = Result.Height;
	Json->TryGetNumberField(TEXT("logical_height"), Result.LogicalHeight);
	Json->TryGetStringField(TEXT("skybox"), Result.Skybox);
	Json->TryGetNumberField(TEXT("ambient_light"), Result.AmbientLight);
	const TSharedPtr<FJsonObject>* Attributes = nullptr;
	if (Json->TryGetObjectField(TEXT("attributes"), Attributes))
	{
		double CloudHeight = 0.0;
		if ((*Attributes)->TryGetNumberField(TEXT("minecraft:visual/cloud_height"), CloudHeight))
		{
			Result.CloudHeight = static_cast<float>(CloudHeight);
		}
		// "#aarrggbb".
		FString CloudColor;
		if ((*Attributes)->TryGetStringField(TEXT("minecraft:visual/cloud_color"), CloudColor) && CloudColor.StartsWith(TEXT("#")) && CloudColor.Len() == 9)
		{
			const uint32 Argb = FParse::HexNumber(*CloudColor.Mid(1));
			Result.CloudColor = FLinearColor(((Argb >> 16) & 255) / 255.0f, ((Argb >> 8) & 255) / 255.0f, (Argb & 255) / 255.0f, ((Argb >> 24) & 255) / 255.0f);
		}
		const TSharedPtr<FJsonObject>* Music = nullptr;
		if ((*Attributes)->TryGetObjectField(TEXT("minecraft:audio/background_music"), Music))
		{
			Result.Music = ParseMusic(**Music);
		}
	}

	if (Result.MinY % 16 != 0 || Result.Height % 16 != 0 || Result.Height <= 0)
	{
		UE_LOG(LogMcCore, Error, TEXT("%s: min_y %d and height %d must be positive multiples of 16"), *Path, Result.MinY, Result.Height);
		return false;
	}

	Out = Result;
	return true;
}

TMap<FName, FMcDimensionType::FMusic> FMcDimensionType::ParseMusic(const FJsonObject& BackgroundMusic)
{
	TMap<FName, FMusic> Result;
	for (const auto& Pair : BackgroundMusic.Values)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Entry))
		{
			continue;
		}
		FMusic Music;
		FString Sound;
		if (!(*Entry)->TryGetStringField(TEXT("sound"), Sound))
		{
			continue;
		}
		// "minecraft:music.game" is the event music.game.
		Music.Sound = FName(*McCache::PathOf(Sound));
		(*Entry)->TryGetNumberField(TEXT("min_delay"), Music.MinDelay);
		(*Entry)->TryGetNumberField(TEXT("max_delay"), Music.MaxDelay);
		Result.Add(FName(*Pair.Key), Music);
	}
	return Result;
}
