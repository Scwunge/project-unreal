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
	// "overlays": { "entries": [ { "formats": ..., "directory": "name" } ] }. A folder inside the pack whose files replace the
	// pack's own for the formats it names (0 means no limit on that side). Later entries win.
	struct FOverlay
	{
		FString Directory;
		int32 MinFormat = 0;
		int32 MaxFormat = 0;
		bool bHasRange = false;
	};
	TArray<FOverlay> Overlays;
	// "language": { "xx_yy": { "name": "...", "region": "...", "bidirectional": false } }: languages the pack adds to the picker
	// (its lang/xx_yy.json holds the text).
	struct FLanguage
	{
		FString Code;
		FString Name;
		FString Region;
		bool bBidirectional = false;
	};
	TArray<FLanguage> Languages;
	// Files the pack has under assets/<namespace>/shaders/ and assets/<namespace>/post_effect/ (core shaders, post effects):
	// GLSL and render-graph JSON for the game's own renderer. Unreal draws the world, so they have no effect here; they are
	// listed so the pack's load can say so.
	TArray<FString> ShaderFiles;
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
	// The name the picker and -McPacks call the pack, when that is not its file name (a built-in pack).
	void SetName(const FString& InName) { Info.Name = InName; }

	// Whether the pack holds this file, by its path inside the pack ("assets/minecraft/textures/block/stone.png").
	bool Has(const FString& Relative) const;
	// The file's bytes. Safe from any thread.
	bool Read(const FString& Relative, TArray<uint8>& Out) const;
	// The folder pack's file on disk, or empty for a zip.
	FString GetFolderFile(const FString& Relative) const;

	// The first file of a zip whose name ends with this (a font's "unifont.hex" in its zip), no pack.mcmeta needed.
	static bool ReadZipEntry(const FString& ZipPath, const FString& NameEnding, TArray<uint8>& Out);

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
	// Fills Info.ShaderFiles from what the pack holds.
	void FindShaderFiles();
	// The path a file has inside the pack: in the last overlay for this game's format that has it, else as given.
	FString Resolve(const FString& Relative) const;
	bool HasExact(const FString& Path) const;

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
	// The packs that ship with the game, under the names the game gives them: the zips the launcher's asset index holds
	// ("high_contrast", "programmer_art"), which tools/fetch_cache.py puts in cache/assets/minecraft/resourcepacks. Only
	// the ones that are in the cache. They are offered in the picker and used by name like any other pack. The Default
	// pack, the vanilla assets themselves, is not one of them: it is always on, below everything.
	MCCORE_API TArray<FString> GetBuiltInNames();
	MCCORE_API bool IsBuiltIn(const FString& Name);

	// The packs now in use. The first call reads -McPacks from the command line.
	MCCORE_API TSharedRef<const FMcPackStack> GetStack();
	// Replaces the packs with these names (lowest first). Bumps GetGeneration. Names that can't be opened are logged and skipped.
	MCCORE_API void SetPacks(const TArray<FString>& Names);
	// Replaces the stack itself (tests).
	MCCORE_API void SetStack(TSharedRef<const FMcPackStack> Stack);
	MCCORE_API uint32 GetGeneration();
	// F3+T: asks for the packs to be read again from disk and the game's pictures, models and sounds rebuilt. The HUD takes
	// the request (true once per ask) and does the reloading, which needs the screen and the world.
	MCCORE_API void RequestReload();
	MCCORE_API bool TakeReloadRequest();
	MCCORE_API bool HasPacks();

	// A name below may carry a namespace ("zz:textures/a.png" is assets/zz/textures/a.png); without one it is the game's
	// ("minecraft"). A namespace other than the game's has nothing but the packs to give it.
	// Looks for the file "assets/minecraft/<Relative>" in the packs. True with a path on disk when a pack has it (a folder
	// pack's own file, or for a zip the file taken out into Saved/McPackCache, along with its .mcmeta); true with a
	// path that doesn't exist when a pack's filter hides it. False when the vanilla copy applies.
	MCCORE_API bool TryResolve(const FString& Relative, FString& OutPath);
	// Every pack's copy of "assets/minecraft/<Relative>", lowest pack first, for the files the game merges rather than
	// replaces (lang/*.json, sounds.json). Vanilla's own copy is not included.
	MCCORE_API void ReadAll(const FString& Relative, TArray<TArray<uint8>>& Out);
	// True if a pack provides the file.
	MCCORE_API bool IsFromPack(const FString& Relative);
	// A sprite the packs' atlases make (unstitch, paletted_permutations): the PNG written for "textures/block/x.png", if any.
	MCCORE_API bool FindAtlasSprite(const FString& Relative, FString& OutPath);
	// Whether a pack's atlas filter hides this texture ("textures/block/x.png") of the game's.
	MCCORE_API bool IsAtlasFiltered(const FString& Relative);

	// ---- The picker's side ----
	// A zip or folder in the resourcepacks folder. Pack is null (and Error says why) if it can't be used.
	struct FFolderEntry
	{
		FString Name;  // the file or folder name, which is also what -McPacks and the saved selection call it
		FString Path;
		TSharedPtr<FMcResourcePack> Pack;
		FString Error;
		bool bBuiltIn = false;
	};
	// The built-in packs, then everything in the folder that is a zip or a directory, by name.
	MCCORE_API TArray<FFolderEntry> ScanFolder();
	// Dropping files onto the window: the paths that are packs (a .zip, or a folder holding pack.mcmeta), by the name they
	// would have in the resourcepacks folder, and the others, which the game does not offer to add.
	struct FDrop
	{
		TArray<FString> Packs;
		TArray<FString> Names;
		TArray<FString> Others;
	};
	MCCORE_API FDrop SortDropped(const TArray<FString>& Paths);
	// Copies those packs (as SortDropped found them) into the resourcepacks folder, a folder and everything in it. A name the
	// folder already has is not overwritten; it, and any copy that failed, goes into Failed. Returns the names copied.
	MCCORE_API TArray<FString> CopyDropped(const FDrop& Drop, TArray<FString>& Failed, const FString& IntoFolder = FString());
	// The names of the packs in use, lowest first.
	MCCORE_API TArray<FString> GetSelectedNames();
	// Uses exactly these packs (lowest first) and remembers them in Saved/McResourcePacks.json for the next start. A
	// -McPacks argument on the command line wins over the remembered list.
	MCCORE_API void ApplySelection(const TArray<FString>& Names);

	// The resource pack format of the game (manifest.json: resource_major 97). A pack made for another format still loads
	// and its files are used where they match, as in the game, which only marks it.
	constexpr int32 GameFormat = 97;
	// Whether the pack says it supports GameFormat: 0 yes, -1 made for an older version, 1 for a newer one, 2 when it
	// names no format at all. The supported range (supported_formats, or min_format and max_format) wins over pack_format.
	MCCORE_API int32 GetCompatibility(const FMcPackInfo& Info);

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
