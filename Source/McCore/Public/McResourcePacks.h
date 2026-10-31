// Resource packs (texture packs), stacked over the vanilla assets in cache/ the way Java Edition does it: a pack is a zip
// or a folder with a pack.mcmeta and an assets/ tree, later packs win file by file, and anything no pack has comes from
// the cache. McCache::Asset goes through here, so every reader of assets/minecraft/ sees the packs without changes.
//
// Format: https://minecraft.wiki/w/Resource_pack (pack.mcmeta, pack.png, the assets/<namespace>/ layout, the "filter"
// section) and https://minecraft.wiki/w/Resource_pack#Animation (the .png.mcmeta files travel with the pack's png).
//
// Selecting packs: -McPacks=a.zip,b on the command line. A name is looked up in the resourcepacks/ folder next to the
// project (or taken as a path); the last one listed is on top. Packs that are in the folder but not named do nothing.

#pragma once

#include "CoreMinimal.h"

class FJsonValue;

// What pack.mcmeta and pack.png say.
struct MCCORE_API FMcPackInfo
{
	FString Name;         // the zip's or folder's file name
	FString Path;
	bool bZip = false;
	int32 PackFormat = 0;       // "pack_format", 0 when missing
	int32 MinFormat = 0;        // the lowest and highest formats the pack says it supports; 0 when it names none
	int32 MaxFormat = 0;
	FString Description;        // plain text: the text component flattened, colour codes removed
	bool bHasIcon = false;      // pack.png is there
	// "filter": { "block": [ { "namespace": regex, "path": regex } ] }. Files of the packs below this one (and the
	// vanilla assets) whose namespace and path match are hidden.
	struct FBlock
	{
		FString Namespace;
		FString Path;
	};
	TArray<FBlock> Blocks;
};

class MCCORE_API FMcResourcePack
{
public:
	// Opens a zip file or a folder. Null (and OutError) if it can't be read or has no pack.mcmeta.
	static TSharedPtr<FMcResourcePack> Open(const FString& PackPath, FString* OutError = nullptr);

	// Reads pack.mcmeta text. False if it isn't JSON with a "pack" object. A missing description or format is not an error.
	static bool ParseMcmeta(const FString& Json, FMcPackInfo& Out);

	// A text component (a string, an object with "text" and "extra", or an array of those) as plain text.
	static FString FlattenText(const TSharedPtr<FJsonValue>& Component);

	const FMcPackInfo& GetInfo() const { return Info; }

	// Whether the pack holds this file, by its path inside the pack ("assets/minecraft/textures/block/stone.png").
	bool Has(const FString& Relative) const;
	// The file's bytes. Safe from any thread.
	bool Read(const FString& Relative, TArray<uint8>& Out) const;
	// The folder pack's file on disk, or empty for a zip.
	FString GetFolderFile(const FString& Relative) const;

	// How many files a zip pack lists (0 for a folder).
	int32 NumZipEntries() const { return Entries.Num(); }
	// Identifies the pack's content for caches: path, size and time of the zip, or the folder's path and pack.mcmeta time.
	const FString& GetStamp() const { return Stamp; }

private:
	struct FZipEntry
	{
		int64 LocalOffset = 0;
		int64 CompressedSize = 0;
		int64 Size = 0;
		uint16 Method = 0;
	};

	bool OpenZip(const FString& PackPath, FString& OutError);

	FMcPackInfo Info;
	FString Stamp;
	TMap<FString, FZipEntry> Entries;
};

// Packs in order, lowest first. Vanilla sits below all of them.
class MCCORE_API FMcPackStack
{
public:
	explicit FMcPackStack(TArray<TSharedPtr<FMcResourcePack>> InPacks = {}) : Packs(MoveTemp(InPacks)) {}

	int32 Num() const { return Packs.Num(); }
	const FMcResourcePack& Get(int32 Index) const { return *Packs[Index]; }

	// The index of the topmost pack that has the file and isn't hidden by the filter of a pack above it; INDEX_NONE
	// when no pack does (the file comes from vanilla, unless IsVanillaHidden).
	int32 Find(const FString& Relative) const;
	// True if a pack's filter hides the vanilla copy of the file.
	bool IsVanillaHidden(const FString& Relative) const;
	// Reads the file from the topmost pack that has it.
	bool Read(const FString& Relative, TArray<uint8>& Out, int32* OutPack = nullptr) const;

private:
	bool IsHiddenAbove(const FString& Relative, int32 Index) const;

	TArray<TSharedPtr<FMcResourcePack>> Packs;
};

namespace McPacks
{
	// The names in a -McPacks value: split at commas, trimmed, empty ones dropped.
	MCCORE_API TArray<FString> ParseList(const FString& List);
	// <project>/resourcepacks
	MCCORE_API FString GetFolder();
	// A pack name to a path: an existing path as given, else the name inside the resourcepacks folder. Empty if neither exists.
	MCCORE_API FString ResolveName(const FString& Name);

	// The packs now in use. The first call reads -McPacks from the command line.
	MCCORE_API TSharedRef<const FMcPackStack> GetStack();
	// Replaces the packs with these names (lowest first). Bumps GetGeneration. Names that can't be opened are logged and skipped.
	MCCORE_API void SetPacks(const TArray<FString>& Names);
	// Replaces the stack itself (tests).
	MCCORE_API void SetStack(TSharedRef<const FMcPackStack> Stack);
	MCCORE_API uint32 GetGeneration();
	MCCORE_API bool HasPacks();

	// Looks for the file "assets/minecraft/<Relative>" in the packs. True with a path on disk when a pack has it (a folder
	// pack's own file, or for a zip the file taken out into Saved/McPackCache, along with its .mcmeta); true with a
	// path that doesn't exist when a pack's filter hides it. False when the vanilla copy applies.
	MCCORE_API bool TryResolve(const FString& Relative, FString& OutPath);
	// True if a pack provides the file.
	MCCORE_API bool IsFromPack(const FString& Relative);

	// ---- The picker's side ----
	// A zip or folder in the resourcepacks folder. Pack is null (and Error says why) if it can't be used.
	struct FFolderEntry
	{
		FString Name;  // the file or folder name, which is also what -McPacks and the saved selection call it
		FString Path;
		TSharedPtr<FMcResourcePack> Pack;
		FString Error;
	};
	// Everything in the folder that is a zip or a directory, by name.
	MCCORE_API TArray<FFolderEntry> ScanFolder();
	// The names of the packs in use, lowest first.
	MCCORE_API TArray<FString> GetSelectedNames();
	// Uses exactly these packs (lowest first) and remembers them in Saved/McResourcePacks.json for the next start. A
	// -McPacks argument on the command line wins over the remembered list.
	MCCORE_API void ApplySelection(const TArray<FString>& Names);

	// What the stack is, as text, to key caches: changes whenever a pack, its order or its files change.
	MCCORE_API FString GetSignature();

	// Texture options. -McPackMaxSize=<pixels> (a power of two; default 512) is the largest block texture side kept; larger
	// pack textures are shrunk. -McPackBudgetMB=<MB> (default 1536) caps the block texture array: the side is lowered
	// until the array and its mips fit. -McPackPbr turns on the LabPBR _n and _s maps (off by default).
	MCCORE_API int32 GetMaxTextureSize();
	MCCORE_API int64 GetBudgetBytes();
	MCCORE_API bool GetUsePbr();
	MCCORE_API void SetUsePbr(bool bUse);
	MCCORE_API void SetMaxTextureSize(int32 Size);
	MCCORE_API void SetBudgetMegabytes(int32 Megabytes);

	// The largest power-of-two side, from 16 up to Wanted (itself rounded up to a power of two) and MaxSize, for which
	// NumLayers layers with their whole mip chain fit in BudgetBytes at 4 bytes a pixel. Never below 16.
	MCCORE_API int32 ChooseTextureSize(int32 Wanted, int32 NumLayers, int32 MaxSize, int64 BudgetBytes);
}
