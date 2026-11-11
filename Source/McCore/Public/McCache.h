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

	// A file from the jar's assets/minecraft/, e.g. Asset("blockstates/stone.json"). A resource pack that has the file
	// wins (McResourcePacks.h): the path is then the pack's file, or for a zip the copy taken out into Saved/McPackCache.
	MCCORE_API FString Asset(const FString& RelativePath);
	// The file of an id with its folder and extension: AssetOf("zz:block/a", "models/", ".json") is assets/zz/models/block/a.json
	// from a pack, AssetOf("stone", "blockstates/", ".json") the game's blockstates/stone.json. An id's namespace is
	// "minecraft" when it has none; any other namespace is only in resource packs.
	// The name AssetOf looks up ("zz:models/block/a.json", or "models/block/a.json" for the game's), for McPacks::IsFromPack and the like.
	MCCORE_API FString RelativeOf(const FString& Id, const FString& Directory, const FString& Extension);
	MCCORE_API FString AssetOf(const FString& Id, const FString& Directory, const FString& Extension);

	// The "texture" section of a picture's .png.mcmeta, for pictures drawn on their own (not cut into an atlas): how the
	// game samples it. Blur filters it smoothly instead of nearest; clamp holds its edge pixels past 0 and 1, where
	// otherwise it repeats. A picture without the section, or a key not in it, leaves the sampling as the caller has it.
	struct FTextureMeta
	{
		bool bBlur = false;
		bool bHasClamp = false;
		bool bClamp = false;
	};
	MCCORE_API FTextureMeta ReadTextureMeta(const FString& PngPath);

	// How far a villager's profession or type picture hides the hat layer of the head, from the "villager" section of its
	// .png.mcmeta: "hat": "none" (the default), "partial" (the hat is drawn, the hair is hidden under it) or "full" (the whole
	// head is covered). The villager model reads this for the profession and the type and draws the head accordingly.
	enum class EVillagerHat : uint8
	{
		None,
		Partial,
		Full
	};
	MCCORE_API EVillagerHat ReadVillagerHat(const FString& PngPath);
	// The same for a picture of the villager folder: Category is "profession" or "type", Name e.g. "farmer" or "plains"
	// (an id's namespace, if any, is ignored). Looks in the resource packs first, like Asset().
	MCCORE_API EVillagerHat VillagerHat(const FString& Category, const FString& Name);

	// Reads and parses a JSON file whose top level is an object. Logs and returns null on failure.
	MCCORE_API TSharedPtr<FJsonObject> LoadJsonObject(const FString& Path);

	// "stone" -> "minecraft:stone"; ids that already have a namespace are returned unchanged.
	MCCORE_API FString Namespaced(const FString& Id);

	// "minecraft:stone" -> "stone", for building file paths from ids.
	MCCORE_API FString PathOf(const FString& Id);
}
