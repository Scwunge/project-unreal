#include "McGameRules.h"

bool FMcGameRules::GetBool(const TMap<FString, FString>& Rules, const FString& Name, bool bDefault)
{
	const FString* Value = Rules.Find(Name);
	if (!Value)
	{
		return bDefault;
	}
	if (Value->Equals(TEXT("true"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	if (Value->Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	return bDefault;
}

int32 FMcGameRules::GetInt(const TMap<FString, FString>& Rules, const FString& Name, int32 Default)
{
	const FString* Value = Rules.Find(Name);
	if (!Value || !Value->IsNumeric())
	{
		return Default;
	}
	return FCString::Atoi(**Value);
}

FMcGameRules FMcGameRules::FromLevel(const TMap<FString, FString>& Rules, int32 InDifficulty)
{
	FMcGameRules Result;
	Result.Difficulty = FMath::Clamp(InDifficulty, 0, 3);
	Result.bDoMobSpawning = GetBool(Rules, TEXT("doMobSpawning"), Result.bDoMobSpawning);
	Result.bDoWardenSpawning = GetBool(Rules, TEXT("doWardenSpawning"), Result.bDoWardenSpawning);
	Result.bDoMobLoot = GetBool(Rules, TEXT("doMobLoot"), Result.bDoMobLoot);
	Result.bDoTileDrops = GetBool(Rules, TEXT("doTileDrops"), Result.bDoTileDrops);
	Result.bKeepInventory = GetBool(Rules, TEXT("keepInventory"), Result.bKeepInventory);
	Result.bNaturalRegeneration = GetBool(Rules, TEXT("naturalRegeneration"), Result.bNaturalRegeneration);
	Result.bFallDamage = GetBool(Rules, TEXT("fallDamage"), Result.bFallDamage);
	Result.bFireDamage = GetBool(Rules, TEXT("fireDamage"), Result.bFireDamage);
	Result.bDrowningDamage = GetBool(Rules, TEXT("drowningDamage"), Result.bDrowningDamage);
	Result.bDoWeatherCycle = GetBool(Rules, TEXT("doWeatherCycle"), Result.bDoWeatherCycle);
	Result.bSpawnerBlocksWork = GetBool(Rules, TEXT("spawnerBlocksWork"), Result.bSpawnerBlocksWork);
	Result.RandomTickSpeed = FMath::Max(0, GetInt(Rules, TEXT("randomTickSpeed"), Result.RandomTickSpeed));
	return Result;
}
