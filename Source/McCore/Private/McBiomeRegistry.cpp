#include "McBiomeRegistry.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "McCache.h"
#include "McCore.h"
#include "Misc/Paths.h"

FMcBiomeRegistry& FMcBiomeRegistry::Get()
{
	static FMcBiomeRegistry Instance;
	return Instance;
}

bool FMcBiomeRegistry::EnsureLoaded()
{
	check(IsInGameThread());
	if (IsLoaded())
	{
		return true;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *McCache::Data(TEXT("worldgen/biome/*.json")), true, false);
	if (Files.IsEmpty() || Files.Num() > 255)
	{
		UE_LOG(LogMcCore, Error, TEXT("Found %d biomes in cache/; run tools/fetch_cache.py."), Files.Num());
		return false;
	}
	Files.Sort();
	for (const FString& File : Files)
	{
		const FName Name(*McCache::Namespaced(FPaths::GetBaseFilename(File)));
		Ids.Add(Name, static_cast<FMcBiomeId>(Names.Num()));
		Names.Add(Name);
		const TSharedPtr<FJsonObject> Biome = McCache::LoadJsonObject(McCache::Data(TEXT("worldgen/biome/") + File));
		Spawns.Add(Biome.IsValid() ? ParseSpawns(*Biome) : TMap<FName, TArray<FMcSpawnEntry>>());
	}
	Plains = Find(TEXT("minecraft:plains")).Get(0);
	return true;
}

TConstArrayView<FMcSpawnEntry> FMcBiomeRegistry::GetSpawns(FMcBiomeId Id, FName Category) const
{
	if (!Spawns.IsValidIndex(Id))
	{
		return {};
	}
	const TArray<FMcSpawnEntry>* Entries = Spawns[Id].Find(Category);
	return Entries ? TConstArrayView<FMcSpawnEntry>(*Entries) : TConstArrayView<FMcSpawnEntry>();
}

TMap<FName, TArray<FMcSpawnEntry>> FMcBiomeRegistry::ParseSpawns(const FJsonObject& Biome)
{
	// 26.3 keeps them in an environment attribute: attributes."minecraft:gameplay/natural_mob_spawns".argument
	// .spawns_by_category.<category> = [{ type, weight, count }], count a number or a uniform range.
	TMap<FName, TArray<FMcSpawnEntry>> Result;
	const TSharedPtr<FJsonObject>* Attributes = nullptr;
	const TSharedPtr<FJsonObject>* Attribute = nullptr;
	const TSharedPtr<FJsonObject>* Argument = nullptr;
	const TSharedPtr<FJsonObject>* Categories = nullptr;
	if (!Biome.TryGetObjectField(TEXT("attributes"), Attributes)
		|| !(*Attributes)->TryGetObjectField(TEXT("minecraft:gameplay/natural_mob_spawns"), Attribute)
		|| !(*Attribute)->TryGetObjectField(TEXT("argument"), Argument)
		|| !(*Argument)->TryGetObjectField(TEXT("spawns_by_category"), Categories))
	{
		return Result;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Category : (*Categories)->Values)
	{
		const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
		if (!Category.Value->TryGetArray(Lines))
		{
			continue;
		}
		TArray<FMcSpawnEntry>& Entries = Result.Add(FName(*Category.Key));
		for (const TSharedPtr<FJsonValue>& LineValue : *Lines)
		{
			const TSharedPtr<FJsonObject>* Line = nullptr;
			FString Kind;
			if (!LineValue->TryGetObject(Line) || !(*Line)->TryGetStringField(TEXT("type"), Kind))
			{
				continue;
			}
			FMcSpawnEntry Entry;
			Entry.Kind = FName(*Kind);
			(*Line)->TryGetNumberField(TEXT("weight"), Entry.Weight);
			const TSharedPtr<FJsonValue> Count = (*Line)->TryGetField(TEXT("count"));
			const TSharedPtr<FJsonObject>* Range = nullptr;
			double Number = 0.0;
			if (Count.IsValid() && Count->TryGetNumber(Number))
			{
				Entry.MinCount = Entry.MaxCount = FMath::Max(1, static_cast<int32>(Number));
			}
			else if (Count.IsValid() && Count->TryGetObject(Range))
			{
				(*Range)->TryGetNumberField(TEXT("min_inclusive"), Entry.MinCount);
				(*Range)->TryGetNumberField(TEXT("max_inclusive"), Entry.MaxCount);
				Entry.MinCount = FMath::Max(1, Entry.MinCount);
				Entry.MaxCount = FMath::Max(Entry.MinCount, Entry.MaxCount);
			}
			if (Entry.Weight > 0)
			{
				Entries.Add(Entry);
			}
		}
	}
	return Result;
}

TOptional<FMcBiomeId> FMcBiomeRegistry::Find(FName Biome) const
{
	const FMcBiomeId* Found = Ids.Find(FName(*McCache::Namespaced(Biome.ToString())));
	return Found ? TOptional<FMcBiomeId>(*Found) : TOptional<FMcBiomeId>();
}

FName FMcBiomeRegistry::GetName(FMcBiomeId Id) const
{
	return Names.IsValidIndex(Id) ? Names[Id] : FName(TEXT("minecraft:plains"));
}
