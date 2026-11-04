// Chat and the commands typed into it, as the game has them (minecraft.wiki: Commands). The replies are the jar's own
// text (McLang). Commands run on the server; what they do to the world goes through IMcCommandTarget, which the game
// mode implements, so they can be tested without a world.
//
// Who may run what follows the game's permission levels: 0 for everyone (/help, /list, /me, /msg), 2 for cheats
// (/clear, /difficulty, /effect, /enchant, /fill, /gamemode, /gamerule, /give, /kill, /seed, /say, /setblock, /summon, /time, /tp,
// /weather, /xp), 3 for running a server (/kick, /op, /deop, /whitelist). The player hosting a world of their own has every level; on a dedicated server it is the level in
// ops.json, and the console has every level too. A command the sender can't use is unknown to them, as in the game.
//
// Not here yet: /spawnpoint, /execute, /clone, data tags on /summon and /setblock, and the other
// selector arguments (@a, @p, @r, @s and names work; [type=...] and the like don't). /gamerule keeps every rule, but
// only doDaylightCycle changes the game so far.

#pragma once

#include "CoreMinimal.h"
#include "McSimulation.h"
#include "McLaunch.h"
#include "McBlockRegistry.h"

struct FMcCommandSender
{
	// The player's name; empty for the server console.
	FString Name;
	int32 PermissionLevel = 0;
	bool IsConsole() const { return Name.IsEmpty(); }
};

struct FMcCommandReply
{
	FString Text;
	bool bError = false;
};

// /worldborder's state, kept for the session. Values the game never showed before a command set them start at -1, so the first set always counts as a change.
struct FMcWorldBorderState
{
	double Size = 59999968.0;
	double CentreX = 0.0;
	double CentreZ = 0.0;
	double DamageAmount = -1.0;
	double DamageBuffer = -1.0;
	double WarningDistance = -1.0;
	double WarningTicks = -1.0;
};

// What a command can do. Players are named as they joined; names match ignoring case.
class MCSERVER_API IMcCommandTarget
{
public:
	virtual ~IMcCommandTarget() = default;

	virtual TArray<FString> GetPlayerNames() const = 0;
	virtual int32 GetMaxPlayers() const = 0;
	// True on a dedicated server (where /kick, /op and /whitelist mean something).
	virtual bool IsDedicated() const = 0;
	virtual void SendTo(const FString& Player, const FMcCommandReply& Line) = 0;
	virtual void Broadcast(const FString& Line) = 0;
	// /title: what to show a player. Kind is 0 title, 1 subtitle, 2 action bar, 3 clear, 4 reset, 5 times (the three tick counts);
	// Text is legacy text (McText::ToLegacy of the component).
	virtual void SendTitle(const FString& Player, int32 Kind, const FString& Text, int32 FadeIn, int32 Stay, int32 FadeOut) {}

	virtual bool IsCreative(const FString& Player) const = 0;
	virtual void SetCreative(const FString& Player, bool bCreative) = 0;
	// Feet position in blocks (Minecraft axes: x east, y up, z south).
	virtual bool GetPosition(const FString& Player, FVector& OutFeet) const = 0;
	virtual void Teleport(const FString& Player, const FVector& Feet) = 0;
	virtual void Kill(const FString& Player) = 0;
	virtual void Kick(const FString& Player, const FString& Reason) = 0;
	// How many of Item make a stack, or 0 for no such item.
	virtual int32 GetMaxStackSize(FName Item) const = 0;
	virtual FString GetItemName(FName Item) const = 0;
	virtual void Give(const FString& Player, FName Item, int32 Count) = 0;
	// A give with item components, each as its JSON text (/give @s minecraft:white_banner[minecraft:banner_patterns=[...]]).
	virtual void GiveWith(const FString& Player, FName Item, int32 Count, const TMap<FName, FString>& Components) { Give(Player, Item, Count); }

	virtual double GetDayTime() const = 0;
	virtual void SetDayTime(double Ticks) = 0;
	virtual double GetGameTime() const = 0;
	// Duration in ticks; 0 lets the weather choose (as the game does).
	virtual void SetWeather(EMcWeather Weather, int32 DurationTicks) = 0;
	virtual int64 GetSeed() const = 0;
	// 0 peaceful, 1 easy, 2 normal, 3 hard.
	virtual int32 GetDifficulty() const = 0;
	virtual void SetDifficulty(int32 Difficulty) = 0;
	// A rule's text as set, or empty if it never was (it is then the default).
	virtual FString GetGameRule(const FString& Rule) const = 0;
	virtual void SetGameRule(const FString& Rule, const FString& Value) = 0;

	// Blocks at whole positions. GetBlock is false where the world isn't loaded or is out of its height.
	virtual bool GetBlock(const FIntVector& Pos, FMcStateId& OutState) const = 0;
	virtual void SetBlock(const FIntVector& Pos, FMcStateId State) = 0;
	// A new mob of the kind at the feet position; false if there is nowhere to put it.
	virtual bool Summon(FName Kind, const FVector& Feet) = 0;

	// Experience: the level and the points towards the next.
	virtual void GetXp(const FString& Player, int32& OutLevel, int32& OutPoints) const = 0;
	// Adds (or with a negative amount takes) points or levels.
	virtual void AddXp(const FString& Player, int32 Amount, bool bLevels) = 0;
	// Sets the level, or the points into it; false (changing nothing) for more points than the level holds.
	virtual bool SetXp(const FString& Player, int32 Amount, bool bLevels) = 0;
	// Takes up to MaxCount (every one, if negative) of Item (any item, if None) from the player's inventory, armour, offhand
	// and cursor, or with bTest only counts them. How many were found.
	virtual int32 Clear(const FString& Player, FName Item, int32 MaxCount, bool bTest) = 0;

	// Effects, named without the namespace ("speed"). Duration is in ticks; an instant effect ignores it.
	virtual bool IsEffect(FName Effect) const = 0;
	// False if the player has a stronger one already.
	virtual bool GiveEffect(const FString& Player, FName Effect, int32 Amplifier, int32 DurationTicks) = 0;
	// Grants or revokes an advancement for a player (showing its toast on a grant): -1 if there is no such advancement,
	// 0 if it changed nothing (already had it, or hadn't), 1 if it did. OutTitle is its name as the game's replies show it.
	virtual int32 SetAdvancement(const FString& Player, const FString& Id, bool bGrant, FString& OutTitle) { return -1; }
	// Takes one (every one, if None) from the player; how many there were.
	virtual int32 ClearEffects(const FString& Player, FName Effect) = 0;

	// How high a level the enchantment goes ("minecraft:sharpness"), or 0 for no such enchantment.
	virtual int32 GetEnchantmentMaxLevel(FName Enchantment) const = 0;
	enum class EEnchantResult : uint8 { Done, NoItem, Unsupported };
	// Puts it on the item in the player's main hand, replacing a level it already has there.
	virtual EEnchantResult Enchant(const FString& Player, FName Enchantment, int32 Level) = 0;

	// Plays a sounds.json event for everyone, at a position in blocks (volume over 1 carries further).
	virtual void PlaySoundAt(FName Event, const FVector& Position, float Volume, float Pitch) {}

	// /setworldspawn and /spawnpoint, at a block (the feet position of where players come back).
	virtual void SetWorldSpawn(const FIntVector& Pos) {}
	virtual void SetPlayerSpawn(const FString& Player, const FIntVector& Pos) {}
	// /tick: the world's target ticks per second, whether it is frozen, a number of ticks to run while frozen, and the time a frame takes.
	virtual double GetTickRate() const { return 20.0; }
	virtual void SetTickRate(double Rate) {}
	virtual bool IsTickFrozen() const { return false; }
	virtual void SetTickFrozen(bool bFrozen) {}
	virtual void StepTicks(int32 Count) {}
	virtual double GetAverageTickMs() const { return 0.0; }
	// /defaultgamemode: what new players start in.
	virtual void SetDefaultGameMode(const FString& Mode) {}
	// /stopsound: stops a player's sounds (Source "" for any, Sound "" for any).
	virtual void StopSounds(const FString& Player, const FString& Source, const FString& Sound) {}

	virtual FMcServerAccess LoadAccess() const = 0;
	virtual void SaveAccess(const FMcServerAccess& Access) = 0;
	virtual bool IsAllowlistOn() const = 0;
	virtual void SetAllowlistOn(bool bOn) = 0;
};

namespace McCommands
{
	// The world border /worldborder sets (the game reads it to keep players inside).
	MCSERVER_API FMcWorldBorderState& GetWorldBorder();
	// A line typed into chat: a command if it starts with '/', else said to everyone as "<Name> text". Replies for the
	// sender come back; what everyone sees goes through Target.Broadcast.
	MCSERVER_API TArray<FMcCommandReply> HandleChat(const FString& Line, const FMcCommandSender& Sender, IMcCommandTarget& Target);
	// A command without its '/'.
	MCSERVER_API TArray<FMcCommandReply> Execute(const FString& Command, const FMcCommandSender& Sender, IMcCommandTarget& Target);
	// The functions of a function tag ("minecraft:tick", with or without the '#'), in order, tags inside it followed.
	MCSERVER_API TArray<FString> GetFunctionTag(const FString& Tag);
	// Runs these functions (ids) as the sender; the replies they gave.
	MCSERVER_API TArray<FMcCommandReply> RunFunctions(const TArray<FString>& Ids, const FMcCommandSender& Sender, IMcCommandTarget& Target);
	// The level a player has: the host of a world of their own has 4, otherwise their ops.json level (0 if not an op).
	MCSERVER_API int32 GetPermissionLevel(const FString& Player, bool bHost, const FMcServerAccess& Access);
	// A time as /time and /weather take it: ticks, or a number with d (days), s (seconds) or t. -1 if it isn't one.
	MCSERVER_API int32 ParseTime(const FString& Text);
	// The game's rules (minecraft.wiki: Game rule), with their defaults.
	struct FGameRule
	{
		const TCHAR* Name;
		bool bInteger;
		const TCHAR* Default;
	};
	MCSERVER_API TConstArrayView<FGameRule> GetGameRules();
	MCSERVER_API const FGameRule* FindGameRule(const FString& Name);
	// The longest chat line the game accepts.
	constexpr int32 MaxChatLength = 256;
	// A command line (it starts with a slash) may be far longer: JSON text in /tellraw and /title runs past a chat line.
	constexpr int32 MaxCommandLength = 32500;
}
