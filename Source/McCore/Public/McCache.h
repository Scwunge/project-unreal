// Paths into cache/, the gitignored folder that tools/fetch_cache.py fills from your own 26.3 install.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace McCache
{
	// The cache folder: the folder given with -McCache=<path> on the command line, else <project>/cache when that holds a
	// finished cache (the editor and a checkout), else McUnreal/cache in the user's local app data (a packaged game, whose
	// first run builds it, see McCacheSetup.h). Worked out once.
	MCCORE_API FString Root();

	// The local app data folder's cache: where a packaged game builds its own.
	MCCORE_API FString UserRoot();

	// True when the cache at Root() was finished: its manifest.json lists every step as done.
	MCCORE_API bool IsComplete();

	// A data generator report, e.g. Report("blocks.json") -> cache/data/reports/blocks.json.
	MCCORE_API FString Report(const FString& RelativePath);

	// A file from the jar's data/minecraft/, e.g. Data("dimension_type/overworld.json").
	MCCORE_API FString Data(const FString& RelativePath);

	// A file from the jar's assets/minecraft/, e.g. Asset("blockstates/stone.json").
	MCCORE_API FString Asset(const FString& RelativePath);

	// Reads and parses a JSON file whose top level is an object. Logs and returns null on failure.
	MCCORE_API TSharedPtr<FJsonObject> LoadJsonObject(const FString& Path);

	// "stone" -> "minecraft:stone"; ids that already have a namespace are returned unchanged.
	MCCORE_API FString Namespaced(const FString& Id);

	// "minecraft:stone" -> "stone", for building file paths from ids.
	MCCORE_API FString PathOf(const FString& Id);
}
