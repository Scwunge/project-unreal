// The small files of a saved world (M9), next to the region files of McWorldStorage:
//
//   Saved/Worlds/<name>/level.json            seed, spawn, time, game rules, dimension
//   Saved/Worlds/<name>/playerdata/<id>.json  where a player was
//   Saved/Worlds/<name>/<dimension>/r.X.Z.mcr the chunks
//
// JSON, so a save can be read and fixed by hand. Written to a temporary file and moved over the old one, so a crash
// mid-write leaves the previous file whole.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

// What the world as a whole remembers (the game's level.dat).
struct MCSERVER_API FMcLevelData
{
	// Bumped when the file's layout changes.
	int32 Version = 1;
	int64 Seed = 0;
	// The block players first appear above.
	FIntVector Spawn = FIntVector::ZeroValue;
	// Game ticks, not wrapped (see AMcWorldActor::GetDayTime).
	double DayTime = 1000.0;
	// The dimension the world was last in, e.g. "minecraft:overworld".
	FString Dimension = TEXT("minecraft:overworld");
	// How its terrain is made, so it carries on the same way: the noise settings, or a flat preset.
	FString NoiseSettings = TEXT("minecraft:overworld");
	bool bFlat = false;
	FString FlatPreset;
	// Game rules by name, as text ("true", "3"). Only doDaylightCycle is read so far; the rest are kept for later.
	TMap<FString, FString> GameRules;
	// 0 peaceful, 1 easy, 2 normal, 3 hard.
	int32 Difficulty = 2;
	// What the world list shows (the folder name is a safe version of it) and the mode new players start in,
	// "survival" or "creative".
	FString Name;
	FString GameMode = TEXT("survival");

	bool operator==(const FMcLevelData& Other) const;
};

// One occupied slot of a saved inventory. The numbers are the game's: 0 to 35 the inventory (0 to 8 the hotbar), 100 to
// 103 the armour (feet first), 150 the off hand.
struct MCSERVER_API FMcSavedSlot
{
	int32 Slot = 0;
	FMcItemStack Stack;

	bool operator==(const FMcSavedSlot& Other) const { return Slot == Other.Slot && Stack == Other.Stack; }
};

// A player's place in the world, in Minecraft's terms (block coordinates, degrees).
struct MCSERVER_API FMcPlayerData
{
	FVector Position = FVector::ZeroVector;
	// Minecraft yaw (0 faces south, 90 west) and pitch (positive looks down).
	float Yaw = 0.0f;
	float Pitch = 0.0f;
	FString Dimension = TEXT("minecraft:overworld");
	bool bFlying = false;
	// The player's id as a UUID text ("8f1c..."), when known (a player imported from a Java save keeps theirs, so an export
	// writes the same file name); empty otherwise.
	FString Uuid;

	// ---- Survival state (only if bHasSurvival: a file from before has none, and the player starts as new) ----
	bool bHasSurvival = false;
	bool bCreative = false;
	int32 SelectedSlot = 0;
	// By name: health, food, saturation, exhaustion, air, absorption, xp_level, xp_progress, fire_ticks, food_timer.
	TMap<FString, double> Stats;
	TArray<FMcSavedSlot> Slots;

	bool operator==(const FMcPlayerData& Other) const;
};

namespace McWorldSave
{
	// The folder every world is under, Saved/Worlds.
	MCSERVER_API FString GetWorldsRoot();
	// A world name that is safe as a folder name: letters, digits, '_', '-' and '.' kept, anything else (path
	// separators, ':' and the like) replaced with '_'. Empty if nothing usable is left (empty, or only dots).
	MCSERVER_API FString SanitizeName(const FString& Name);
	MCSERVER_API FString GetWorldDir(const FString& Name);
	// "minecraft:the_nether" -> "the_nether"; another namespace is kept ("mod:thing" -> "mod_thing").
	MCSERVER_API FString GetDimensionFolder(const FString& Dimension);

	MCSERVER_API bool SaveLevel(const FString& WorldDir, const FMcLevelData& Level);
	// What a quit save writes, in the order vanilla 26.3 writes it (observed 2026-11-13 from file times after a quit: the
	// player's files first, then region and entity files, the dimensions' data files, level.dat last of all). Autosaves
	// spread the chunks over frames, so only a quit follows this strictly.
	enum class ESaveStep : uint8 { Players, Chunks, Maps, Level };
	MCSERVER_API TConstArrayView<ESaveStep> GetQuitSaveOrder();
	// session.lock, as the game writes it when it opens a world: the three bytes of a snowman (U+2603 in UTF-8), observed in
	// every vanilla save. Returns false if the file can't be written.
	MCSERVER_API bool WriteSessionLock(const FString& WorldDir);
	// False if there is no level file or it can't be read.
	MCSERVER_API bool LoadLevel(const FString& WorldDir, FMcLevelData& OutLevel);

	// The world's maps (McMapStore) in WorldDir/maps.json. Loading replaces the store; a world with no file gets an empty one.
	MCSERVER_API bool SaveMaps(const FString& WorldDir);
	MCSERVER_API bool LoadMaps(const FString& WorldDir);

	// A saved world as the world list shows it, newest played first.
	struct FWorldSummary
	{
		FString Folder;
		FMcLevelData Level;
		FDateTime LastPlayed;
	};
	MCSERVER_API TArray<FWorldSummary> ListWorlds();
	// A folder name for a new world called Name that no world uses yet: the safe name, with " (2)" and up added (as
	// "_2_") when it is taken. "World" if Name has nothing usable.
	MCSERVER_API FString MakeNewFolder(const FString& Name);
	// Deletes a world's folder, chunks and all. False if it isn't a world (no level.json) or can't be deleted.
	MCSERVER_API bool DeleteWorld(const FString& Folder);

	// Players are keyed by an id the caller picks (a name, or the platform user id); like world names it is
	// sanitised for the file name.
	MCSERVER_API bool SavePlayer(const FString& WorldDir, const FString& PlayerId, const FMcPlayerData& Player);
	MCSERVER_API bool LoadPlayer(const FString& WorldDir, const FString& PlayerId, FMcPlayerData& OutPlayer);
}
