// Data packs, stacked over the game's data/minecraft/ in cache/ the way Java Edition does it: a pack is a zip or a folder
// with a pack.mcmeta and a data/<namespace>/ tree; a later pack's file of the same id replaces the earlier one, tags
// add up (unless a tag says "replace": true), and anything no pack has comes from the cache.
//
// Format: https://minecraft.wiki/w/Data_pack (pack.mcmeta, the data/<namespace>/<folder>/<path> layout).
//
// Packs live in the datapacks/ folder next to the project (a world's own datapacks folder is not read). The selection is
// -McDataPacks=a.zip,b on the command line, else what /datapack enable and disable saved in Saved/McDataPacks.json;
// the last listed is on top. McCache::Data goes through here for single files, so every reader of data/minecraft/
// sees a pack's replacement file; readers that list folders (recipes, tags, advancements, functions) use List.

#pragma once

#include "CoreMinimal.h"
#include "McResourcePacks.h"

namespace McData
{
	// <project>/datapacks
	MCCORE_API FString GetFolder();

	// The packs in use, lowest first. The first call reads -McDataPacks, else the saved selection.
	MCCORE_API TSharedRef<const FMcPackStack> GetStack();
	MCCORE_API TArray<FString> GetSelectedNames();
	// Uses exactly these packs (lowest first) and remembers them. Names that can't be opened are logged and skipped.
	MCCORE_API void ApplySelection(const TArray<FString>& Names);
	// Replaces the stack (tests), without saving.
	MCCORE_API void SetStack(TSharedRef<const FMcPackStack> Stack);
	MCCORE_API uint32 GetGeneration();

	// Every zip or folder in the datapacks folder, by name. Pack is null (and Error says why) if it can't be used.
	struct FFolderEntry
	{
		FString Name;
		FString Path;
		TSharedPtr<FMcResourcePack> Pack;
		FString Error;
	};
	MCCORE_API TArray<FFolderEntry> ScanFolder();

	// An id and what files hold it, the game's first, then each pack's in order (the last is the one that applies;
	// tags read them all).
	struct FFile
	{
		FString Id;  // "minecraft:stone" or "zz:a/b", no folder and no extension
		TArray<FString> Paths;
	};
	// Every file "data/<ns>/<Folder>/<id><Ext>" of the game and the packs, by id. Ids of the game's namespace come with
	// "minecraft:" like the rest.
	MCCORE_API TArray<FFile> List(const FString& Folder, const FString& Ext = TEXT(".json"));

	// The values of a tag from its files in order (a List entry's Paths): each file adds its "values" (names, "#tags", or
	// { "id": name, "required": false }), and one that says "replace": true drops what the files before it gave.
	MCCORE_API TArray<FString> ReadTagValues(const TArray<FString>& Paths);

	// The pack file of one id (a pack's own, or for a zip the copy taken out into Saved/McDataPackCache), if a pack has it.
	MCCORE_API bool TryResolve(const FString& Id, const FString& Folder, const FString& Ext, FString& OutPath);
	// The file that applies to an id: a pack's, else for the game's namespace the cache's; empty if neither exists.
	MCCORE_API FString PathOf(const FString& Id, const FString& Folder, const FString& Ext);
	// A file of data/minecraft/ by its relative path ("loot_table/blocks/stone.json") from a pack, if a pack has it.
	MCCORE_API bool TryResolveGame(const FString& Relative, FString& OutPath);

	// Asks every reader that keeps what it read (recipes, tags, loot tables, advancements, functions) to read again;
	// /reload does it. OnReload is where they hook in.
	MCCORE_API FSimpleMulticastDelegate& OnReload();
	MCCORE_API void Reload();
}
