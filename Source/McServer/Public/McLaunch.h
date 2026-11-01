// How this run was started, and what a server lets in (M9).
//
// A world is opened with options: from the command line (-McWorld=name and the development switches), from the URL the
// title screen opens the map with (?McWorld=name?McSeed=...), or, on a dedicated server, from server.properties. The
// command line wins, so the development switches keep working everywhere.
//
// A dedicated server keeps its settings next to the worlds, in Saved/Server, written with the defaults on first start
// so there is something to edit:
//
//   server.properties   key=value lines, as the game's: server-port, max-players, gamemode, level-name, level-seed,
//                       level-type, white-list, motd
//   allowlist.json      [{"name": "Alex"}, ...]   who may join while white-list is on
//   ops.json            [{"name": "Alex", "level": 4, "bypassesPlayerLimit": true}, ...]
//
// There are no accounts yet, so players are matched by the name they join with, ignoring case; the game's "uuid"
// fields are kept if present but not checked.

#pragma once

#include "CoreMinimal.h"

class UWorld;

// server.properties: the lines in file order, so a rewrite keeps the owner's layout and comments.
class MCSERVER_API FMcServerProperties
{
public:
	// The keys this port reads, with the game's defaults (bar the port: see GetDefaults).
	static const TArray<TPair<FString, FString>>& GetDefaults();

	// Reads Text; lines starting with '#' or '!' and blank ones are kept as they are.
	void Parse(const FString& Text);
	FString ToString() const;

	bool Has(const FString& Key) const;
	FString GetString(const FString& Key) const;
	int32 GetInt(const FString& Key, int32 Fallback) const;
	bool GetBool(const FString& Key) const;
	void Set(const FString& Key, const FString& Value);
	// Adds any default the file doesn't have; true if one was added.
	bool AddMissingDefaults();

	// Saved/Server/server.properties, read once and written back with any missing defaults.
	static const FMcServerProperties& Get();
	static FString GetDir();
	// Changes a setting of the one Get() returns and writes the file (as /whitelist on does).
	static void SetAndSave(const FString& Key, const FString& Value);

private:
	struct FLine
	{
		FString Key;  // empty for a comment or blank line
		FString Value;
		FString Raw;
	};
	TArray<FLine> Lines;
	const FLine* Find(const FString& Key) const;
};

// The allowlist and the operators, read from their files whenever a player joins so edits apply without a restart.
struct MCSERVER_API FMcServerAccess
{
	struct FOp
	{
		FString Name;
		int32 Level = 4;
		bool bBypassesPlayerLimit = false;
	};
	TArray<FString> Allowlist;
	TArray<FOp> Ops;

	bool IsAllowlisted(const FString& Name) const;
	const FOp* FindOp(const FString& Name) const;

	// Parses the two files' text; false (with nothing added) for text that isn't a JSON array.
	bool ParseAllowlist(const FString& Json);
	bool ParseOps(const FString& Json);
	// Both files from Saved/Server, made empty ("[]") if missing.
	static FMcServerAccess Load();
	// The two files' text, as the game writes them, and both written back to Saved/Server.
	FString AllowlistToJson() const;
	FString OpsToJson() const;
	bool Save() const;
	// Why a player may not join, or empty if they may. Players is how many are on already.
	static FString CheckJoin(const FMcServerProperties& Properties, const FMcServerAccess& Access, const FString& Name, int32 Players);
};

namespace McLaunch
{
	// -Key=Value on the command line, else ?Key=Value in the world's URL, else (on a dedicated server) the
	// server.properties setting it maps to: McWorld level-name, McSeed level-seed, McGameMode gamemode.
	MCSERVER_API bool GetValue(const UWorld* World, const TCHAR* Key, FString& OutValue);
	// -Key on the command line or ?Key in the URL; McFlat is also level-type=minecraft:flat on a dedicated server.
	MCSERVER_API bool HasFlag(const UWorld* World, const TCHAR* Key);
	// True when this run shows the title screen instead of a world: a game on its own (not a server or a client) whose
	// URL asks for the title (?McTitle), or that was started with no world to open and no development switches (or
	// with -McTitle).
	MCSERVER_API bool IsMenu(const UWorld* World);
	// A seed as the New World screen and server.properties take it: a whole number as it is, other text hashed as the
	// game does (Java's String.hashCode), empty text as 0 (a random seed).
	MCSERVER_API int64 ParseSeed(const FString& Text);
	// Text made safe for a URL option (no spaces, '?', '=' or '#'): anything but a letter, digit, '-', '_' or '.' becomes
	// %XXXX, its UTF-16 code in hex. GetValue undoes it for URL options.
	MCSERVER_API FString EncodeOption(const FString& Text);
	MCSERVER_API FString DecodeOption(const FString& Text);
	// "creative" for creative (or "1"), anything else survival.
	MCSERVER_API bool IsCreativeMode(const FString& GameMode);
	// -McServer: the packaged game run as a server (Server.bat), since the engine from the launcher can't build the
	// McClientServer target. It runs without a window (-nullrhi) as a listen server whose own player is never spawned,
	// and takes server.properties, the allowlist and ops like a dedicated server.
	MCSERVER_API bool IsServerSwitch();
	// A dedicated server, or the game run with -McServer: the world serves others and has no player of its own.
	MCSERVER_API bool IsServer(const UWorld* World);
}
