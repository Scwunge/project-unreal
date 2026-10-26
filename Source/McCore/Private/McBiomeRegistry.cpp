#include "McBiomeRegistry.h"
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
	}
	Plains = Find(TEXT("minecraft:plains")).Get(0);
	return true;
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
