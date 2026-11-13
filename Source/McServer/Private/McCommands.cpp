#include "McCommands.h"
#include "McLang.h"
#include "McDataPacks.h"
#include "McCache.h"
#include "Misc/FileHelper.h"
#include "McTextComponent.h"
#include "McCache.h"
#include "McMob.h"
#include "McBiomeRegistry.h"
#include "McVehicle.h"
#include "McLoot.h"
#include "McRandom.h"

namespace
{
	using FReplies = TArray<FMcCommandReply>;

	FMcCommandReply Ok(const FString& Text) { return FMcCommandReply{ Text, false }; }
	FMcCommandReply Fail(const FString& Text) { return FMcCommandReply{ Text, true }; }

	struct FCommandInfo
	{
		const TCHAR* Name;
		int32 Level;
		const TCHAR* Usage;
	};

	// Aliases point at the command they stand for.
	const FCommandInfo Commands[] = {
		{ TEXT("advancement"), 2, TEXT("/advancement (grant|revoke) <targets> only <advancement>") },
		{ TEXT("clear"), 2, TEXT("/clear [<targets>] [<item>] [<maxCount>]") },
		{ TEXT("clone"), 2, TEXT("/clone <begin> <end> <destination> [replace|masked|filtered <filter>] [normal|force|move]") },
		{ TEXT("damage"), 2, TEXT("/damage <target> <amount> [<damageType>]") },
		{ TEXT("datapack"), 2, TEXT("/datapack (disable|enable|list) ...") },
		{ TEXT("defaultgamemode"), 2, TEXT("/defaultgamemode <gamemode>") },
		{ TEXT("deop"), 3, TEXT("/deop <targets>") },
		{ TEXT("difficulty"), 2, TEXT("/difficulty [peaceful|easy|normal|hard]") },
		{ TEXT("effect"), 2, TEXT("/effect (clear|give) ...") },
		{ TEXT("enchant"), 2, TEXT("/enchant <targets> <enchantment> [<level>]") },
		{ TEXT("experience"), 2, TEXT("/experience (add|query|set) ...") },
		{ TEXT("fill"), 2, TEXT("/fill <from> <to> <block> [destroy|hollow|keep|outline|replace]") },
		{ TEXT("function"), 2, TEXT("/function <name>") },
		{ TEXT("gamemode"), 2, TEXT("/gamemode <gamemode> [<target>]") },
		{ TEXT("gamerule"), 2, TEXT("/gamerule <rule> [<value>]") },
		{ TEXT("give"), 2, TEXT("/give <targets> <item> [<count>]") },
		{ TEXT("help"), 0, TEXT("/help [<command>]") },
		{ TEXT("particle"), 2, TEXT("/particle <name> [<pos> [<delta> <speed> <count> [force|normal]]]") },
		{ TEXT("kick"), 3, TEXT("/kick <targets> [<reason>]") },
		{ TEXT("kill"), 2, TEXT("/kill [<targets>]") },
		{ TEXT("list"), 0, TEXT("/list") },
		{ TEXT("me"), 0, TEXT("/me <action>") },
		{ TEXT("msg"), 0, TEXT("/msg <targets> <message>") },
		{ TEXT("op"), 3, TEXT("/op <targets>") },
		{ TEXT("playsound"), 2, TEXT("/playsound <sound> <source> [<targets>] [<pos>] [<volume>] [<pitch>]") },
		{ TEXT("random"), 2, TEXT("/random (reset|roll|value) ...") },
		{ TEXT("recipe"), 2, TEXT("/recipe (give|take) <targets> (*|<recipe>)") },
		{ TEXT("reload"), 2, TEXT("/reload") },
		{ TEXT("say"), 2, TEXT("/say <message>") },
		{ TEXT("attribute"), 2, TEXT("/attribute <target> <attribute> (get | base (get | set <value> | reset) | modifier (add <id> <value> <operation> | remove <id> | value get <id>))") },
		{ TEXT("loot"), 2, TEXT("/loot give <targets> loot <loot_table>") },
		{ TEXT("seed"), 2, TEXT("/seed") },
		{ TEXT("setblock"), 2, TEXT("/setblock <pos> <block> [destroy|keep|replace]") },
		{ TEXT("setworldspawn"), 2, TEXT("/setworldspawn [<pos>] [<yaw> [<pitch>]]") },
		{ TEXT("spawnpoint"), 2, TEXT("/spawnpoint [<targets>] [<pos>] [<yaw> [<pitch>]]") },
		{ TEXT("stopsound"), 2, TEXT("/stopsound <targets> [<source>] [<sound>]") },
		{ TEXT("rotate"), 2, TEXT("/rotate <target> (<rotation>|facing ...)") },
		{ TEXT("scoreboard"), 2, TEXT("/scoreboard (objectives|players) ...") },
		{ TEXT("execute"), 2, TEXT("/execute (as|at|positioned|if|unless|store|run) ...") },
		{ TEXT("locate"), 2, TEXT("/locate biome <biome>") },
		{ TEXT("swing"), 2, TEXT("/swing <entities> [mainhand|offhand]") },
		{ TEXT("forceload"), 2, TEXT("/forceload (add|query|remove) ...") },
		{ TEXT("spreadplayers"), 2, TEXT("/spreadplayers <x> <z> <spreadDistance> <maxRange> [under <maxHeight>] <respectTeams> <targets>") },
		{ TEXT("summon"), 2, TEXT("/summon <entity> [<pos>]") },
		{ TEXT("teammsg"), 0, TEXT("/teammsg <message>") },
		{ TEXT("tm"), 0, TEXT("/tm <message>") },
		{ TEXT("stopwatch"), 2, TEXT("/stopwatch (create|query|restart|remove) <id>") },
		{ TEXT("item"), 2, TEXT("/item replace entity <targets> <slot> (with <item> [count] | from entity <source> <slot>)") },
		{ TEXT("team"), 2, TEXT("/team (add|empty|join|leave|list|modify|remove) ...") },
		{ TEXT("bossbar"), 2, TEXT("/bossbar (add|get|list|remove|set) ...") },
		{ TEXT("data"), 2, TEXT("/data (get|merge|modify|remove) ...") },
		{ TEXT("trigger"), 0, TEXT("/trigger <objective> [add|set <value>]") },
		{ TEXT("tag"), 2, TEXT("/tag <targets> (add|list|remove) ...") },
		{ TEXT("teleport"), 2, TEXT("/teleport <location> | <destination> | <targets> (<location>|<destination>)") },
		{ TEXT("tell"), 0, TEXT("/tell -> msg") },
		{ TEXT("tellraw"), 2, TEXT("/tellraw <targets> <message>") },
		{ TEXT("time"), 2, TEXT("/time (add|query|set) ...") },
		{ TEXT("title"), 2, TEXT("/title <targets> (actionbar|clear|reset|subtitle|times|title) ...") },
		{ TEXT("tp"), 2, TEXT("/tp -> teleport") },
		{ TEXT("w"), 0, TEXT("/w -> msg") },
		{ TEXT("version"), 0, TEXT("/version") },
		{ TEXT("worldborder"), 2, TEXT("/worldborder (add|center|damage|get|set|warning) ...") },
		{ TEXT("tick"), 3, TEXT("/tick (query|rate|freeze|unfreeze|step|sprint) ...") },
		{ TEXT("weather"), 2, TEXT("/weather (clear|rain|thunder) [<duration>]") },
		{ TEXT("whitelist"), 3, TEXT("/whitelist (add|list|off|on|reload|remove) ...") },
		{ TEXT("xp"), 2, TEXT("/xp -> experience") },
	};

	const FCommandInfo* FindCommand(const FString& Name)
	{
		for (const FCommandInfo& Info : Commands)
		{
			if (Name == Info.Name)
			{
				return &Info;
			}
		}
		return nullptr;
	}

	// What the game says for a command it doesn't know (or the sender may not use): the error, then the line up to where
	// it went wrong with the marker.
	FReplies Unknown(const FString& Command, int32 At)
	{
		const FString Shown = Command.Left(At);
		return { Fail(McLang::Get(TEXT("command.unknown.command"))), Fail(Shown + McLang::Get(TEXT("command.context.here"))) };
	}

	FReplies BadArgument(const FString& Command)
	{
		return { Fail(McLang::Get(TEXT("command.unknown.argument"))), Fail(Command + McLang::Get(TEXT("command.context.here"))) };
	}

	// A player argument: a name, @s (the sender), @p (the nearest player: the sender, if a player), @a (everyone) or @r (one
	// at random). False if it matches nobody.
	bool ResolvePlayers(const FString& Token, const FMcCommandSender& Sender, const IMcCommandTarget& Target, TArray<FString>& Out, FString& Error)
	{
		const TArray<FString> Players = Target.GetPlayerNames();
		Out.Reset();
		if (Token == TEXT("@s") || Token == TEXT("@p"))
		{
			if (Sender.IsConsole())
			{
				if (Token == TEXT("@p") && Players.Num() > 0)
				{
					Out.Add(Players[0]);
					return true;
				}
				Error = McLang::Get(TEXT("permissions.requires.player"));
				return false;
			}
			Out.Add(Sender.Name);
			return true;
		}
		if (Token == TEXT("@a") || Token == TEXT("@e"))
		{
			Out = Players;
		}
		else if (Token == TEXT("@r"))
		{
			if (Players.Num() > 0)
			{
				Out.Add(Players[FMath::RandRange(0, Players.Num() - 1)]);
			}
		}
		else if (Token.StartsWith(TEXT("@")))
		{
			Error = McLang::Get(TEXT("command.unknown.argument"));
			return false;
		}
		else
		{
			for (const FString& Player : Players)
			{
				if (Player.Equals(Token, ESearchCase::IgnoreCase))
				{
					Out.Add(Player);
					break;
				}
			}
			if (Out.Num() == 0)
			{
				Error = McLang::Get(TEXT("argument.player.unknown"));
				return false;
			}
		}
		if (Out.Num() == 0)
		{
			Error = McLang::Get(TEXT("argument.entity.notfound.player"));
			return false;
		}
		return true;
	}

	// A coordinate: a number, or ~ / ~n relative to Base. Whole numbers are block centres along x and z, as in the game.
	bool ParseCoordinate(const FString& Token, double Base, bool bCentre, double& Out)
	{
		if (Token.StartsWith(TEXT("~")))
		{
			const FString Rest = Token.Mid(1);
			if (!Rest.IsEmpty() && !Rest.IsNumeric())
			{
				return false;
			}
			Out = Base + (Rest.IsEmpty() ? 0.0 : FCString::Atod(*Rest));
			return true;
		}
		if (!Token.IsNumeric())
		{
			return false;
		}
		Out = FCString::Atod(*Token);
		if (bCentre && !Token.Contains(TEXT(".")))
		{
			Out += 0.5;
		}
		return true;
	}

	FString FormatCoordinate(double Value)
	{
		return FString::Printf(TEXT("%f"), Value);
	}

	FString JoinNames(const TArray<FString>& Names)
	{
		return FString::Join(Names, TEXT(", "));
	}

	FString RestOf(const TArray<FString>& Words, int32 From)
	{
		TArray<FString> Rest;
		for (int32 I = From; I < Words.Num(); ++I)
		{
			Rest.Add(Words[I]);
		}
		return FString::Join(Rest, TEXT(" "));
	}

	FString SenderName(const FMcCommandSender& Sender)
	{
		return Sender.IsConsole() ? FString(TEXT("Server")) : Sender.Name;
	}

	FString GameModeName(bool bCreative)
	{
		return McLang::Get(bCreative ? TEXT("gameMode.creative") : TEXT("gameMode.survival"));
	}

	FName ItemId(const FString& Token)
	{
		return FName(*(Token.Contains(TEXT(":")) ? Token.ToLower() : TEXT("minecraft:") + Token.ToLower()));
	}

	// ---- The commands ----

	FReplies RunGameMode(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2 || Words.Num() > 3)
		{
			return Unknown(Command, Command.Len());
		}
		bool bCreative;
		if (Words[1] == TEXT("creative"))
		{
			bCreative = true;
		}
		else if (Words[1] == TEXT("survival"))
		{
			bCreative = false;
		}
		else
		{
			// Adventure and spectator are the game's too, but this port doesn't have them yet.
			return BadArgument(Command);
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words.Num() == 3 ? Words[2] : FString(TEXT("@s")), Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		FReplies Replies;
		for (const FString& Player : Players)
		{
			if (Target.IsCreative(Player) == bCreative)
			{
				continue;
			}
			Target.SetCreative(Player, bCreative);
			if (Player == Sender.Name)
			{
				Replies.Add(Ok(McLang::Format(TEXT("commands.gamemode.success.self"), { GameModeName(bCreative) })));
			}
			else
			{
				Target.SendTo(Player, Ok(McLang::Format(TEXT("gameMode.changed"), { GameModeName(bCreative) })));
				Replies.Add(Ok(McLang::Format(TEXT("commands.gamemode.success.other"), { Player, GameModeName(bCreative) })));
			}
		}
		return Replies;
	}

	FReplies RunTime(const FString& Command, const TArray<FString>& Words, IMcCommandTarget& Target)
	{
		if (Words.Num() == 3 && Words[1] == TEXT("set"))
		{
			static const TMap<FString, int32> Named = { { TEXT("day"), 1000 }, { TEXT("noon"), 6000 }, { TEXT("night"), 13000 }, { TEXT("midnight"), 18000 } };
			int32 Ticks = Named.Contains(Words[2]) ? Named[Words[2]] : McCommands::ParseTime(Words[2]);
			if (Ticks < 0)
			{
				return BadArgument(Command);
			}
			// The day count stays: set moves the clock within today, as the game's does.
			const double Now = Target.GetDayTime();
			const double Day = FMath::FloorToDouble(Now / 24000.0) * 24000.0;
			Target.SetDayTime(Day + Ticks % 24000);
			return { Ok(McLang::Format(TEXT("commands.time.set"), { FString::FromInt(Ticks) })) };
		}
		if (Words.Num() == 3 && Words[1] == TEXT("add"))
		{
			const int32 Ticks = McCommands::ParseTime(Words[2]);
			if (Ticks < 0)
			{
				return BadArgument(Command);
			}
			Target.SetDayTime(Target.GetDayTime() + Ticks);
			return { Ok(McLang::Format(TEXT("commands.time.set"), { FString::FromInt(static_cast<int64>(Target.GetDayTime()) % 24000) })) };
		}
		if (Words.Num() == 3 && Words[1] == TEXT("query"))
		{
			const int64 Now = static_cast<int64>(Target.GetDayTime());
			if (Words[2] == TEXT("daytime"))
			{
				return { Ok(McLang::Format(TEXT("commands.time.query"), { LexToString(Now % 24000) })) };
			}
			if (Words[2] == TEXT("day"))
			{
				return { Ok(McLang::Format(TEXT("commands.time.query"), { LexToString(Now / 24000) })) };
			}
			if (Words[2] == TEXT("gametime"))
			{
				return { Ok(McLang::Format(TEXT("commands.time.query.gametime"), { LexToString(static_cast<int64>(Target.GetGameTime())) })) };
			}
			return BadArgument(Command);
		}
		return Unknown(Command, Command.Len());
	}

	FReplies RunWeather(const FString& Command, const TArray<FString>& Words, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2 || Words.Num() > 3)
		{
			return Unknown(Command, Command.Len());
		}
		int32 Duration = 0;
		if (Words.Num() == 3)
		{
			Duration = McCommands::ParseTime(Words[2]);
			if (Duration <= 0)
			{
				return BadArgument(Command);
			}
		}
		if (Words[1] == TEXT("clear"))
		{
			Target.SetWeather(EMcWeather::Clear, Duration);
			return { Ok(McLang::Get(TEXT("commands.weather.set.clear"))) };
		}
		if (Words[1] == TEXT("rain"))
		{
			Target.SetWeather(EMcWeather::Rain, Duration);
			return { Ok(McLang::Get(TEXT("commands.weather.set.rain"))) };
		}
		if (Words[1] == TEXT("thunder"))
		{
			Target.SetWeather(EMcWeather::Thunder, Duration);
			return { Ok(McLang::Get(TEXT("commands.weather.set.thunder"))) };
		}
		return BadArgument(Command);
	}

	FReplies RunTeleport(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		TArray<FString> Movers;
		FString Error;
		int32 Next = 1;
		// /tp x y z and /tp <destination> move the sender; with four words or two names, the first names who moves.
		if (Words.Num() == 5 || Words.Num() == 3)
		{
			if (!ResolvePlayers(Words[1], Sender, Target, Movers, Error))
			{
				return { Fail(Error) };
			}
			Next = 2;
		}
		else if (Words.Num() == 4 || Words.Num() == 2)
		{
			if (!ResolvePlayers(TEXT("@s"), Sender, Target, Movers, Error))
			{
				return { Fail(Error) };
			}
		}
		else
		{
			return Unknown(Command, Command.Len());
		}

		const bool bLocation = Words.Num() - Next == 3;
		TArray<FString> Destination;
		if (!bLocation && !ResolvePlayers(Words[Next], Sender, Target, Destination, Error))
		{
			return { Fail(Error) };
		}
		if (!bLocation && Destination.Num() != 1)
		{
			return BadArgument(Command);
		}
		FVector To = FVector::ZeroVector;
		if (!bLocation && !Target.GetPosition(Destination[0], To))
		{
			return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
		}
		for (const FString& Mover : Movers)
		{
			FVector Here = FVector::ZeroVector;
			Target.GetPosition(Mover, Here);
			FVector Feet = To;
			if (bLocation && !(ParseCoordinate(Words[Next], Here.X, true, Feet.X)
				&& ParseCoordinate(Words[Next + 1], Here.Y, false, Feet.Y)
				&& ParseCoordinate(Words[Next + 2], Here.Z, true, Feet.Z)))
			{
				return BadArgument(Command);
			}
			Target.Teleport(Mover, Feet);
			To = bLocation ? Feet : To;
		}
		const FString Who = Movers.Num() == 1 ? Movers[0] : FString::FromInt(Movers.Num());
		const TCHAR* Many = Movers.Num() == 1 ? TEXT("single") : TEXT("multiple");
		if (bLocation)
		{
			return { Ok(McLang::Format(FString::Printf(TEXT("commands.teleport.success.location.%s"), Many), { Who, FormatCoordinate(To.X), FormatCoordinate(To.Y), FormatCoordinate(To.Z) })) };
		}
		return { Ok(McLang::Format(FString::Printf(TEXT("commands.teleport.success.entity.%s"), Many), { Who, Destination[0] })) };
	}

	FReplies RunGive(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3 || Words.Num() > 4)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		// Item components ride in brackets after the id: name[component=json,...] (the JSON may not contain spaces).
		FString ItemToken = Words[2];
		TMap<FName, FString> Components;
		int32 Open;
		if (ItemToken.FindChar(TEXT('['), Open) && ItemToken.EndsWith(TEXT("]")))
		{
			const FString Inside = ItemToken.Mid(Open + 1, ItemToken.Len() - Open - 2);
			ItemToken.LeftInline(Open);
			int32 Equals;
			if (Inside.FindChar(TEXT('='), Equals))
			{
				Components.Add(ItemId(Inside.Left(Equals)), Inside.Mid(Equals + 1));
			}
		}
		const FName Item = ItemId(ItemToken);
		const int32 MaxStack = Target.GetMaxStackSize(Item);
		if (MaxStack <= 0)
		{
			return { Fail(McLang::Format(TEXT("argument.item.id.invalid"), { Item.ToString() })) };
		}
		int32 Count = 1;
		if (Words.Num() == 4)
		{
			if (!Words[3].IsNumeric() || Words[3].Contains(TEXT(".")) || FCString::Atoi(*Words[3]) < 1)
			{
				return BadArgument(Command);
			}
			Count = FCString::Atoi(*Words[3]);
		}
		// The game caps a give at 100 stacks.
		const FString Shown = TEXT("[") + Target.GetItemName(Item) + TEXT("]");
		if (Count > MaxStack * 100)
		{
			return { Fail(McLang::Format(TEXT("commands.give.failed.toomanyitems"), { FString::FromInt(MaxStack * 100), Shown })) };
		}
		for (const FString& Player : Players)
		{
			Components.IsEmpty() ? Target.Give(Player, Item, Count) : Target.GiveWith(Player, Item, Count, Components);
		}
		if (Players.Num() == 1)
		{
			return { Ok(McLang::Format(TEXT("commands.give.success.single"), { FString::FromInt(Count), Shown, Players[0] })) };
		}
		return { Ok(McLang::Format(TEXT("commands.give.success.multiple"), { FString::FromInt(Count), Shown, FString::FromInt(Players.Num()) })) };
	}

	FReplies RunKill(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() > 2)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words.Num() == 2 ? Words[1] : FString(TEXT("@s")), Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		for (const FString& Player : Players)
		{
			Target.Kill(Player);
		}
		if (Players.Num() == 1)
		{
			return { Ok(McLang::Format(TEXT("commands.kill.success.single"), { Players[0] })) };
		}
		return { Ok(McLang::Format(TEXT("commands.kill.success.multiple"), { FString::FromInt(Players.Num()) })) };
	}

	// ---- World and player commands ----

	// A block position: three coordinates, relative ones from Base (the sender's feet), rounded down to the block.
	bool ParseBlockPos(const TArray<FString>& Words, int32 First, const FVector& Base, FIntVector& Out)
	{
		double X, Y, Z;
		if (Words.Num() < First + 3 || !ParseCoordinate(Words[First], Base.X, false, X) || !ParseCoordinate(Words[First + 1], Base.Y, false, Y)
			|| !ParseCoordinate(Words[First + 2], Base.Z, false, Z))
		{
			return false;
		}
		Out = FIntVector(FMath::FloorToInt(X), FMath::FloorToInt(Y), FMath::FloorToInt(Z));
		return true;
	}

	FVector SenderFeet(const FMcCommandSender& Sender, const IMcCommandTarget& Target)
	{
		FVector Feet = FVector::ZeroVector;
		if (Sender.Position.IsSet())
		{
			return *Sender.Position;
		}
		if (!Sender.IsConsole())
		{
			Target.GetPosition(Sender.Name, Feet);
		}
		return Feet;
	}

	// A block state as the game writes it: stone, minecraft:oak_stairs[facing=east], ...
	TOptional<FMcStateId> ParseBlock(const FString& Token)
	{
		FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (!Registry.EnsureLoaded())
		{
			return {};
		}
		const int32 Bracket = Token.Find(TEXT("["));
		const FString Id = Bracket == INDEX_NONE ? Token : Token.Left(Bracket);
		return Registry.ParseState((Id.Contains(TEXT(":")) ? Id : TEXT("minecraft:") + Id).ToLower() + (Bracket == INDEX_NONE ? FString() : Token.Mid(Bracket)));
	}

	// The name a data pack has in /datapack: "file/<name>" for one of the datapacks folder.
	FString PackTitle(const FString& Name)
	{
		return TEXT("file/") + Name + TEXT(" (world)");
	}

	// /datapack list [available|enabled], enable <name> [first|last|before <pack>|after <pack>], disable <name>.
	FReplies RunDataPack(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Enabled = McData::GetSelectedNames();
		if (!Enabled.Contains(TEXT("vanilla")))
		{
			Enabled.Insert(TEXT("vanilla"), 0);
		}
		const auto Find = [](const TArray<FString>& Names, const FString& Wanted) -> int32
		{
			return Names.IndexOfByPredicate([&Wanted](const FString& Name) { return Name == Wanted || TEXT("file/") + Name == Wanted || PackTitle(Name) == Wanted; });
		};
		if (Words[1] == TEXT("list") && Words.Num() <= 3)
		{
			const FString Which = Words.Num() == 3 ? Words[2] : FString();
			if (!Which.IsEmpty() && Which != TEXT("available") && Which != TEXT("enabled"))
			{
				return BadArgument(Command);
			}
			FReplies Replies;
			if (Which != TEXT("available"))
			{
				// Lowest first, as the game lists them, the game's own pack among them.
				TArray<FString> Shown;
				for (const FString& Name : Enabled)
				{
					Shown.Add(Name == TEXT("vanilla") ? FString(TEXT("[vanilla (built-in)]")) : TEXT("[") + PackTitle(Name) + TEXT("]"));
				}
				Replies.Add(Ok(McLang::Format(TEXT("commands.datapack.list.enabled.success"), { FString::FromInt(Shown.Num()), FString::Join(Shown, TEXT(", ")) })));
			}
			if (Which != TEXT("enabled"))
			{
				TArray<FString> Shown;
				for (const McData::FFolderEntry& Entry : McData::ScanFolder())
				{
					if (Entry.Pack.IsValid() && Find(Enabled, Entry.Name) == INDEX_NONE)
					{
						Shown.Add(TEXT("[") + PackTitle(Entry.Name) + TEXT("]"));
					}
				}
				Replies.Add(Shown.IsEmpty() ? Ok(McLang::Get(TEXT("commands.datapack.list.available.none")))
					: Ok(McLang::Format(TEXT("commands.datapack.list.available.success"), { FString::FromInt(Shown.Num()), FString::Join(Shown, TEXT(", ")) })));
			}
			return Replies;
		}
		if ((Words[1] == TEXT("enable") || Words[1] == TEXT("disable")) && Words.Num() >= 3)
		{
			FString Name = Words[2].TrimQuotes();
			const bool bEnable = Words[1] == TEXT("enable");
			Name.RemoveFromStart(TEXT("file/"));
			Name = Name.Replace(TEXT(" (world)"), TEXT(""));
			const int32 At = Find(Enabled, Name);
			if (bEnable)
			{
				bool bKnown = false;
				for (const McData::FFolderEntry& Entry : McData::ScanFolder())
				{
					bKnown |= Entry.Pack.IsValid() && Entry.Name == Name;
				}
				if (!bKnown)
				{
					return { Fail(McLang::Format(TEXT("commands.datapack.unknown"), { PackTitle(Name) })) };
				}
				if (At != INDEX_NONE)
				{
					return { Fail(McLang::Format(TEXT("commands.datapack.enable.failed"), { PackTitle(Name) })) };
				}
				// Last is the top (the game's default); first is the bottom, under the game's own pack; before and after are
				// relative to another pack.
				int32 Insert = Enabled.Num();
				if (Words.Num() >= 4)
				{
					const FString Place = Words[3];
					if (Place == TEXT("first"))
					{
						Insert = 0;
					}
					else if (Place == TEXT("last"))
					{
						Insert = Enabled.Num();
					}
					else if ((Place == TEXT("before") || Place == TEXT("after")) && Words.Num() == 5)
					{
						FString Other = Words[4].TrimQuotes();
						Other.RemoveFromStart(TEXT("file/"));
						const int32 OtherAt = Find(Enabled, Other);
						if (OtherAt == INDEX_NONE)
						{
							return { Fail(McLang::Format(TEXT("commands.datapack.unknown"), { PackTitle(Other) })) };
						}
						// "before" in the game's list is above (higher priority), so after it in ours.
						Insert = Place == TEXT("before") ? OtherAt + 1 : OtherAt;
					}
					else
					{
						return BadArgument(Command);
					}
				}
				Enabled.Insert(Name, Insert);
			}
			else
			{
				if (At == INDEX_NONE)
				{
					return { Fail(McLang::Format(TEXT("commands.datapack.disable.failed"), { PackTitle(Name) })) };
				}
				Enabled.RemoveAt(At);
			}
			McData::ApplySelection(Enabled);
			// The change is read in at once, as the game reloads its data after the command.
			McData::Reload();
			return { Ok(McLang::Format(bEnable ? TEXT("commands.datapack.modify.enable") : TEXT("commands.datapack.modify.disable"), { TEXT("[") + PackTitle(Name) + TEXT("]") })) };
		}
		return BadArgument(Command);
	}

	// The functions of a function tag ("minecraft:tick"), in order, tags inside it followed.
	void CollectTagFunctions(const FString& Tag, TArray<FString>& Out, int32 Depth)
	{
		if (Depth > 8)
		{
			return;
		}
		const FString Full = McCache::Namespaced(Tag);
		for (const McData::FFile& File : McData::List(TEXT("tags/function")))
		{
			if (File.Id != Full)
			{
				continue;
			}
			for (const FString& Value : McData::ReadTagValues(File.Paths))
			{
				if (Value.StartsWith(TEXT("#")))
				{
					CollectTagFunctions(Value.RightChop(1), Out, Depth + 1);
				}
				else
				{
					Out.AddUnique(McCache::Namespaced(Value));
				}
			}
		}
	}

	// Runs the lines of a function as the sender; the count of commands run, or -1 for a function that isn't there.
	int32 RunFunctionLines(const FString& Id, const FMcCommandSender& Sender, IMcCommandTarget& Target, FReplies& Replies, int32 Depth)
	{
		const FString Path = McData::PathOf(Id, TEXT("function"), TEXT(".mcfunction"));
		FString Text;
		if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Text, *Path))
		{
			return -1;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		int32 Ran = 0;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			FString Line = Lines[Index].TrimStartAndEnd();
			// A line that ends in a backslash goes on in the next.
			while (Line.EndsWith(TEXT("\\")) && Index + 1 < Lines.Num())
			{
				Line.LeftChopInline(1);
				Line += Lines[++Index].TrimStartAndEnd();
			}
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("$")))
			{
				continue;
			}
			Line.RemoveFromStart(TEXT("/"));
			TArray<FString> Words;
			Line.ParseIntoArrayWS(Words);
			if (Words.Num() == 2 && Words[0] == TEXT("function") && Depth < 16)
			{
				if (RunFunctionLines(McCache::Namespaced(Words[1]), Sender, Target, Replies, Depth + 1) >= 0)
				{
					++Ran;
					continue;
				}
			}
			// What a function's commands answer is not shown (what they broadcast, like /say, is).
			McCommands::Execute(Line, Sender, Target);
			++Ran;
		}
		return Ran;
	}

	// /function <name>: a function, or every function of a #tag.
	FReplies RunFunction(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() != 2)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Ids;
		const bool bTag = Words[1].StartsWith(TEXT("#"));
		if (bTag)
		{
			CollectTagFunctions(Words[1].RightChop(1), Ids, 0);
		}
		else
		{
			Ids.Add(McCache::Namespaced(Words[1]));
		}
		TArray<FString> Existing;
		for (const FString& Id : Ids)
		{
			if (!McData::PathOf(Id, TEXT("function"), TEXT(".mcfunction")).IsEmpty())
			{
				Existing.Add(Id);
			}
		}
		if (Existing.IsEmpty())
		{
			if (bTag)
			{
				return { Fail(McLang::Format(Ids.IsEmpty() ? TEXT("arguments.function.tag.unknown") : TEXT("commands.function.scheduled.no_functions"), { Words[1].RightChop(1) })) };
			}
			return { Fail(McLang::Format(TEXT("arguments.function.unknown"), { Words[1] })) };
		}
		// The game says what it will run, then they run.
		FReplies Replies;
		Replies.Add(Ok(Existing.Num() == 1 ? McLang::Format(TEXT("commands.function.scheduled.single"), { Existing[0] })
			: McLang::Format(TEXT("commands.function.scheduled.multiple"), { FString::Join(Existing, TEXT(", ")) })));
		FReplies Ignored;
		for (const FString& Id : Existing)
		{
			RunFunctionLines(Id, Sender, Target, Ignored, 0);
		}
		return Replies;
	}

	// /playsound <sound> <source> [<targets>] [<x> <y> <z>] [<volume>] [<pitch>]: the event plays for everyone (the targets are
	// not told apart), at the position given or where the sender stands. The sounds.json of the packs decides what is heard.
	FReplies RunPlaySound(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		static const TCHAR* Sources[] = { TEXT("master"), TEXT("music"), TEXT("record"), TEXT("weather"), TEXT("block"), TEXT("hostile"),
			TEXT("neutral"), TEXT("player"), TEXT("ambient"), TEXT("voice") };
		if (Words.Num() < 3 || Words.Num() > 10)
		{
			return Unknown(Command, Command.Len());
		}
		bool bSource = false;
		for (const TCHAR* Source : Sources)
		{
			bSource |= Words[2] == Source;
		}
		FString Event = Words[1];
		Event.RemoveFromStart(TEXT("minecraft:"));
		if (!bSource || Event.IsEmpty())
		{
			return BadArgument(Command);
		}
		int32 Next = 3;
		FString Who = SenderName(Sender);
		if (Words.IsValidIndex(Next) && (Words[Next].StartsWith(TEXT("@")) || !Words[Next].IsNumeric() && !Words[Next].StartsWith(TEXT("~"))))
		{
			Who = Words[Next].StartsWith(TEXT("@")) ? SenderName(Sender) : Words[Next];
			++Next;
		}
		const FVector Here = SenderFeet(Sender, Target);
		FVector Position = Here;
		if (Words.Num() - Next >= 3)
		{
			if (!(ParseCoordinate(Words[Next], Here.X, true, Position.X) && ParseCoordinate(Words[Next + 1], Here.Y, false, Position.Y)
				&& ParseCoordinate(Words[Next + 2], Here.Z, true, Position.Z)))
			{
				return BadArgument(Command);
			}
			Next += 3;
		}
		float Volume = 1.0f;
		float Pitch = 1.0f;
		if (Words.IsValidIndex(Next))
		{
			if (!Words[Next].IsNumeric() || FCString::Atof(*Words[Next]) < 0.0f)
			{
				return BadArgument(Command);
			}
			Volume = FCString::Atof(*Words[Next++]);
		}
		if (Words.IsValidIndex(Next))
		{
			if (!Words[Next].IsNumeric() || FCString::Atof(*Words[Next]) < 0.0f || FCString::Atof(*Words[Next]) > 2.0f)
			{
				return BadArgument(Command);
			}
			Pitch = FCString::Atof(*Words[Next++]);
		}
		Target.PlaySoundAt(FName(*Event), Position, Volume, Pitch);
		return { Ok(McLang::Format(TEXT("commands.playsound.success.single"), { Words[1], Who })) };
	}

	FReplies RunSetBlock(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 5 || Words.Num() > 6)
		{
			return Unknown(Command, Command.Len());
		}
		FIntVector Pos;
		const TOptional<FMcStateId> State = ParseBlock(Words[4]);
		const FString Mode = Words.Num() == 6 ? Words[5] : FString(TEXT("replace"));
		if (!ParseBlockPos(Words, 1, SenderFeet(Sender, Target), Pos) || !State.IsSet() || !(Mode == TEXT("replace") || Mode == TEXT("keep") || Mode == TEXT("destroy")))
		{
			return BadArgument(Command);
		}
		FMcStateId Old;
		if (!Target.GetBlock(Pos, Old))
		{
			return { Fail(McLang::Get(TEXT("argument.pos.unloaded"))) };
		}
		// keep only fills air; nothing is changed into what it already is.
		if (Old == *State || (Mode == TEXT("keep") && !FMcBlockRegistry::Get().IsAir(Old)))
		{
			return { Fail(McLang::Get(TEXT("commands.setblock.failed"))) };
		}
		Target.SetBlock(Pos, *State);
		return { Ok(McLang::Format(TEXT("commands.setblock.success"), { FString::FromInt(Pos.X), FString::FromInt(Pos.Y), FString::FromInt(Pos.Z) })) };
	}

	FReplies RunFill(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 8 || Words.Num() > 10)
		{
			return Unknown(Command, Command.Len());
		}
		const FVector Base = SenderFeet(Sender, Target);
		FIntVector A, B;
		const TOptional<FMcStateId> State = ParseBlock(Words[7]);
		const FString Mode = Words.Num() >= 9 ? Words[8] : FString(TEXT("replace"));
		TOptional<FMcStateId> Filter;
		if (Words.Num() == 10)
		{
			Filter = ParseBlock(Words[9]);
		}
		const bool bModeOk = Mode == TEXT("replace") || (Words.Num() < 10 && (Mode == TEXT("keep") || Mode == TEXT("hollow") || Mode == TEXT("outline") || Mode == TEXT("destroy")));
		if (!ParseBlockPos(Words, 1, Base, A) || !ParseBlockPos(Words, 4, Base, B) || !State.IsSet() || !bModeOk || (Words.Num() == 10 && !Filter.IsSet()))
		{
			return BadArgument(Command);
		}
		const FIntVector Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z));
		const FIntVector Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
		const int64 Volume = int64(Max.X - Min.X + 1) * (Max.Y - Min.Y + 1) * (Max.Z - Min.Z + 1);
		const FString LimitRule = Target.GetGameRule(TEXT("commandModificationBlockLimit"));
		const int64 Limit = LimitRule.IsNumeric() ? FCString::Atoi64(*LimitRule) : 32768;
		if (Volume > Limit)
		{
			return { Fail(McLang::Format(TEXT("commands.fill.toobig"), { LexToString(Limit), LexToString(Volume) })) };
		}
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const TOptional<FMcStateId> Air = ParseBlock(TEXT("air"));
		int32 Changed = 0;
		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
				{
					const FIntVector Pos(X, Y, Z);
					FMcStateId Old;
					if (!Target.GetBlock(Pos, Old))
					{
						continue;
					}
					// hollow and outline put the block on the shell; hollow also empties the inside, outline leaves it.
					const bool bShell = X == Min.X || X == Max.X || Y == Min.Y || Y == Max.Y || Z == Min.Z || Z == Max.Z;
					FMcStateId New = *State;
					if (!bShell && Mode == TEXT("outline"))
					{
						continue;
					}
					if (!bShell && Mode == TEXT("hollow"))
					{
						New = Air.Get(Old);
					}
					if ((Mode == TEXT("keep") && !Registry.IsAir(Old)) || (Filter.IsSet() && Old != *Filter) || Old == New)
					{
						continue;
					}
					Target.SetBlock(Pos, New);
					++Changed;
				}
			}
		}
		if (Changed == 0)
		{
			return { Fail(McLang::Get(TEXT("commands.fill.failed"))) };
		}
		return { Ok(McLang::Format(TEXT("commands.fill.success"), { FString::FromInt(Changed) })) };
	}

	// /clone <begin> <end> <destination> [replace|masked|filtered <block>] [normal|force|move]
	FReplies RunClone(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 10)
		{
			return Unknown(Command, Command.Len());
		}
		const FVector Base = SenderFeet(Sender, Target);
		FIntVector A, B, Dest;
		if (!ParseBlockPos(Words, 1, Base, A) || !ParseBlockPos(Words, 4, Base, B) || !ParseBlockPos(Words, 7, Base, Dest))
		{
			return BadArgument(Command);
		}
		int32 At = 10;
		FString Filter = TEXT("replace");
		TOptional<FMcStateId> Match;
		if (At < Words.Num() && (Words[At] == TEXT("replace") || Words[At] == TEXT("masked") || Words[At] == TEXT("filtered")))
		{
			Filter = Words[At++];
			if (Filter == TEXT("filtered"))
			{
				if (At >= Words.Num())
				{
					return Unknown(Command, Command.Len());
				}
				Match = ParseBlock(Words[At]);
				if (!Match.IsSet())
				{
					return BadArgument(Command);
				}
				++At;
			}
		}
		FString Mode = TEXT("normal");
		// A mode follows a filter word; straight after the destination there is none.
		if (At < Words.Num() && At == 10 && Filter == TEXT("replace") && !(At > 10))
		{
			return BadArgument(Command);
		}
		if (At < Words.Num())
		{
			Mode = Words[At++];
			if ((Mode != TEXT("normal") && Mode != TEXT("force") && Mode != TEXT("move")) || At < Words.Num())
			{
				return BadArgument(Command);
			}
		}
		const FIntVector Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z));
		const FIntVector Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
		const FIntVector Size = Max - Min + FIntVector(1);
		const FIntVector DestMax = Dest + Size - FIntVector(1);
		const int64 Volume = int64(Size.X) * Size.Y * Size.Z;
		const FString LimitRule = Target.GetGameRule(TEXT("commandModificationBlockLimit"));
		const int64 Limit = LimitRule.IsNumeric() ? FCString::Atoi64(*LimitRule) : 32768;
		if (Volume > Limit)
		{
			return { Fail(McLang::Format(TEXT("commands.clone.toobig"), { LexToString(Limit), LexToString(Volume) })) };
		}
		const bool bOverlap = Dest.X <= Max.X && DestMax.X >= Min.X && Dest.Y <= Max.Y && DestMax.Y >= Min.Y && Dest.Z <= Max.Z && DestMax.Z >= Min.Z;
		if (bOverlap && Mode == TEXT("normal"))
		{
			return { Fail(McLang::Get(TEXT("commands.clone.overlap"))) };
		}
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		struct FCopy { FIntVector From; FIntVector To; FMcStateId State; };
		TArray<FCopy> Copies;
		for (int32 X = 0; X < Size.X; ++X)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 Z = 0; Z < Size.Z; ++Z)
				{
					const FIntVector From = Min + FIntVector(X, Y, Z);
					FMcStateId State, Existing;
					if (!Target.GetBlock(From, State) || !Target.GetBlock(Dest + FIntVector(X, Y, Z), Existing))
					{
						return { Fail(McLang::Get(TEXT("commands.clone.failed"))) };
					}
					if ((Filter == TEXT("masked") && Registry.IsAir(State)) || (Filter == TEXT("filtered") && State != *Match))
					{
						continue;
					}
					Copies.Add({ From, Dest + FIntVector(X, Y, Z), State });
				}
			}
		}
		if (Mode == TEXT("move"))
		{
			const TOptional<FMcStateId> Air = ParseBlock(TEXT("air"));
			for (const FCopy& Copy : Copies)
			{
				Target.SetBlock(Copy.From, Air.Get(Copy.State));
			}
		}
		for (const FCopy& Copy : Copies)
		{
			Target.SetBlock(Copy.To, Copy.State);
		}
		if (Copies.IsEmpty())
		{
			return { Fail(McLang::Get(TEXT("commands.clone.failed"))) };
		}
		return { Ok(McLang::Format(TEXT("commands.clone.success"), { FString::FromInt(Copies.Num()) })) };
	}

	// /setworldspawn [<pos>] [<yaw> [<pitch>]] and /spawnpoint [<targets>] [<pos>] [<yaw> [<pitch>]]: where new and dead players
	// come back. The angle is told back as given (0.0 without one); nothing turns the player at the spawn yet.
	FString ShownAngle(const TArray<FString>& Words, int32 At)
	{
		return At < Words.Num() && Words[At].IsNumeric() ? FString::SanitizeFloat(FCString::Atod(*Words[At]), 1) : FString(TEXT("0.0"));
	}

	FReplies RunSetWorldSpawn(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() == 3 || Words.Num() > 6)
		{
			return Unknown(Command, Command.Len());
		}
		const FVector Base = SenderFeet(Sender, Target);
		FIntVector Pos(FMath::FloorToInt32(Base.X), FMath::FloorToInt32(Base.Y), FMath::FloorToInt32(Base.Z));
		if (Words.Num() >= 4 && !ParseBlockPos(Words, 1, Base, Pos))
		{
			return BadArgument(Command);
		}
		if (Words.Num() == 1 && Sender.IsConsole())
		{
			return { Fail(McLang::Get(TEXT("permissions.requires.player"))) };
		}
		Target.SetWorldSpawn(Pos);
		return { Ok(McLang::Format(TEXT("commands.setworldspawn.success.new"), { FString::FromInt(Pos.X), FString::FromInt(Pos.Y), FString::FromInt(Pos.Z),
			ShownAngle(Words, 4), ShownAngle(Words, 5), TEXT("minecraft:overworld") })) };
	}

	FReplies RunSpawnPoint(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() > 8 || Words.Num() == 3 || Words.Num() == 4)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words.Num() >= 2 ? Words[1] : FString(TEXT("@s")), Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		const FVector Base = SenderFeet(Sender, Target);
		FIntVector Pos(FMath::FloorToInt32(Base.X), FMath::FloorToInt32(Base.Y), FMath::FloorToInt32(Base.Z));
		if (Words.Num() >= 5 && !ParseBlockPos(Words, 2, Base, Pos))
		{
			return BadArgument(Command);
		}
		if (Words.Num() < 5 && Players.Num() == 1)
		{
			// Without a position the spawn is where that player stands.
			FVector Feet;
			if (Target.GetPosition(Players[0], Feet))
			{
				Pos = FIntVector(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z));
			}
		}
		for (const FString& Player : Players)
		{
			Target.SetPlayerSpawn(Player, Pos);
		}
		const FString Who = Players.Num() == 1 ? Players[0] : FString::FromInt(Players.Num());
		return { Ok(McLang::Format(Players.Num() == 1 ? TEXT("commands.spawnpoint.success.single.new") : TEXT("commands.spawnpoint.success.multiple.new"),
			{ FString::FromInt(Pos.X), FString::FromInt(Pos.Y), FString::FromInt(Pos.Z), ShownAngle(Words, 5), ShownAngle(Words, 6), TEXT("minecraft:overworld"), Who })) };
	}

	// /worldborder: the state is kept for the session; nothing enforces it in the world yet, and a change over a time lands at once.
	FMcWorldBorderState& BorderState()
	{
		return McCommands::GetWorldBorder();
	}

	// A number the way the game prints a double: 5.9999968E7 from ten million up, else with at least one decimal.
	FString JavaDouble(double Value)
	{
		if (FMath::Abs(Value) >= 1e7)
		{
			int32 Exponent = 0;
			double Mantissa = Value;
			while (FMath::Abs(Mantissa) >= 10.0)
			{
				Mantissa /= 10.0;
				++Exponent;
			}
			FString Digits = FString::Printf(TEXT("%.9f"), Mantissa);
			while (Digits.EndsWith(TEXT("0")) && !Digits.EndsWith(TEXT(".0")))
			{
				Digits.LeftChopInline(1);
			}
			return Digits + TEXT("E") + FString::FromInt(Exponent);
		}
		FString Text = FString::SanitizeFloat(Value, 1);
		return Text;
	}

	// The game's "...tail<--[HERE]" context line, from where the failing argument begins.
	FString BorderContext(const FString& Command, int32 From)
	{
		const FString Before = Command.Left(From);
		return (Before.Len() > 10 ? TEXT("...") + Before.Right(10) : Before) + Command.Mid(From) + McLang::Get(TEXT("command.context.here"));
	}

	bool ParseNumber(const FString& Word, double& Out)
	{
		if (Word.IsEmpty() || !Word.IsNumeric())
		{
			return false;
		}
		Out = FCString::Atod(*Word);
		return true;
	}

	// A time: ticks, or with t, s or d after the number.
	bool ParseTicks(const FString& Word, double& Ticks)
	{
		FString Number = Word;
		double Unit = 1.0;
		if (Number.EndsWith(TEXT("t"))) { Number.LeftChopInline(1); }
		else if (Number.EndsWith(TEXT("s"))) { Number.LeftChopInline(1); Unit = 20.0; }
		else if (Number.EndsWith(TEXT("d"))) { Number.LeftChopInline(1); Unit = 24000.0; }
		double Value;
		if (!ParseNumber(Number, Value))
		{
			return false;
		}
		Ticks = Value * Unit;
		return true;
	}

	FReplies RunWorldBorder(const FString& Command, const TArray<FString>& Words, IMcCommandTarget& Target)
	{
		constexpr double MaxSize = 59999968.0;
		constexpr double MaxCentre = 29999984.0;
		FMcWorldBorderState& State = BorderState();
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		// Where the n-th word begins in the command.
		const auto WordStart = [&](int32 Index)
		{
			int32 At = 0;
			for (int32 I = 0; I < Index; ++I)
			{
				At += Words[I].Len() + 1;
			}
			return At;
		};
		const FString& Sub = Words[1];
		if (Sub == TEXT("get") && Words.Num() == 2)
		{
			return { Ok(McLang::Format(TEXT("commands.worldborder.get"), { LexToString(int64(FMath::FloorToDouble(State.Size + 0.5))) })) };
		}
		if ((Sub == TEXT("set") || Sub == TEXT("add")) && (Words.Num() == 3 || Words.Num() == 4))
		{
			double Value;
			if (!ParseNumber(Words[2], Value))
			{
				return BadArgument(Command);
			}
			if (Value > MaxSize)
			{
				return { Fail(FString::Printf(TEXT("Double must not be more than %s: found %s"), *JavaDouble(MaxSize), *JavaDouble(Value))), Fail(BorderContext(Command, WordStart(2))) };
			}
			double Ticks = 0.0;
			if (Words.Num() == 4)
			{
				if (!ParseTicks(Words[3], Ticks))
				{
					return BadArgument(Command);
				}
				if (Ticks < 0.0)
				{
					return { Fail(FString::Printf(TEXT("The tick count must not be less than 0: found %d"), FMath::RoundToInt32(Ticks))), Fail(BorderContext(Command, WordStart(2))) };
				}
			}
			const double NewSize = Sub == TEXT("add") ? State.Size + Value : Value;
			if (NewSize > MaxSize)
			{
				return { Fail(McLang::Format(TEXT("commands.worldborder.set.failed.big"), { JavaDouble(MaxSize) })) };
			}
			if (NewSize < 1.0)
			{
				return { Fail(McLang::Get(TEXT("commands.worldborder.set.failed.small"))) };
			}
			if (NewSize == State.Size)
			{
				return { Fail(McLang::Get(TEXT("commands.worldborder.set.failed.nochange"))) };
			}
			const FString Shown = JavaDouble(NewSize);
			const bool bGrow = NewSize > State.Size;
			State.Size = NewSize;
			if (Ticks <= 0.0)
			{
				return { Ok(McLang::Format(TEXT("commands.worldborder.set.immediate"), { Shown })) };
			}
			return { Ok(McLang::Format(bGrow ? TEXT("commands.worldborder.set.grow") : TEXT("commands.worldborder.set.shrink"),
				{ Shown, FString::Printf(TEXT("%.2f"), Ticks / 20.0) })) };
		}
		if (Sub == TEXT("center") && Words.Num() == 4)
		{
			double X, Z;
			if (!ParseNumber(Words[2], X) || !ParseNumber(Words[3], Z))
			{
				return BadArgument(Command);
			}
			if (FMath::Abs(X) > MaxCentre || FMath::Abs(Z) > MaxCentre)
			{
				return { Fail(McLang::Format(TEXT("commands.worldborder.set.failed.far"), { JavaDouble(MaxCentre) })) };
			}
			// A whole number names the block's middle.
			if (!Words[2].Contains(TEXT("."))) { X += 0.5; }
			if (!Words[3].Contains(TEXT("."))) { Z += 0.5; }
			X = FMath::Clamp(X, -MaxCentre, MaxCentre);
			Z = FMath::Clamp(Z, -MaxCentre, MaxCentre);
			if (X == State.CentreX && Z == State.CentreZ)
			{
				return { Fail(McLang::Get(TEXT("commands.worldborder.center.failed"))) };
			}
			State.CentreX = X;
			State.CentreZ = Z;
			return { Ok(McLang::Format(TEXT("commands.worldborder.center.success"), { FString::Printf(TEXT("%.2f"), X), FString::Printf(TEXT("%.2f"), Z) })) };
		}
		if (Sub == TEXT("damage") && Words.Num() == 4 && (Words[2] == TEXT("amount") || Words[2] == TEXT("buffer")))
		{
			double Value;
			if (!ParseNumber(Words[3], Value))
			{
				return BadArgument(Command);
			}
			if (Value < 0.0)
			{
				return { Fail(FString::Printf(TEXT("Float must not be less than 0.0: found %s"), *JavaDouble(Value))), Fail(BorderContext(Command, WordStart(3))) };
			}
			const bool bAmount = Words[2] == TEXT("amount");
			double& Slot = bAmount ? State.DamageAmount : State.DamageBuffer;
			const FString Key = bAmount ? TEXT("commands.worldborder.damage.amount") : TEXT("commands.worldborder.damage.buffer");
			if (Slot == Value)
			{
				return { Fail(McLang::Get(Key + TEXT(".failed"))) };
			}
			Slot = Value;
			return { Ok(McLang::Format(Key + TEXT(".success"), { FString::Printf(TEXT("%.2f"), Value) })) };
		}
		if (Sub == TEXT("warning") && Words.Num() == 4 && (Words[2] == TEXT("distance") || Words[2] == TEXT("time")))
		{
			const bool bDistance = Words[2] == TEXT("distance");
			double Value;
			if (bDistance ? !ParseNumber(Words[3], Value) : !ParseTicks(Words[3], Value))
			{
				return BadArgument(Command);
			}
			if (Value < 0.0)
			{
				return { Fail(FString::Printf(TEXT("%s must not be less than 0: found %d"), bDistance ? TEXT("Integer") : TEXT("The tick count"), FMath::RoundToInt32(Value))),
					Fail(BorderContext(Command, WordStart(3))) };
			}
			double& Slot = bDistance ? State.WarningDistance : State.WarningTicks;
			const FString Key = bDistance ? TEXT("commands.worldborder.warning.distance") : TEXT("commands.worldborder.warning.time");
			if (Slot == Value)
			{
				return { Fail(McLang::Get(Key + TEXT(".failed"))) };
			}
			Slot = Value;
			return { Ok(McLang::Format(Key + TEXT(".success"), { bDistance ? FString::FromInt(FMath::RoundToInt32(Value)) : FString::Printf(TEXT("%.2f"), Value / 20.0) })) };
		}
		return Unknown(Command, Command.Len());
	}

	// /tick query|rate <n>|freeze|unfreeze|step [<n>]: the speed the world runs at (the target rate scales its clock).
	FReplies RunTick(const FString& Command, const TArray<FString>& Words, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Sub = Words[1];
		if (Sub == TEXT("query") && Words.Num() == 2)
		{
			const double Rate = Target.GetTickRate();
			FReplies Replies = { Ok(McLang::Get(Target.IsTickFrozen() ? TEXT("commands.tick.status.frozen") : TEXT("commands.tick.status.running"))) };
			Replies.Add(Ok(McLang::Format(TEXT("commands.tick.query.rate.running"), { FString::SanitizeFloat(Rate, 1), FString::Printf(TEXT("%.1f"), Target.GetAverageTickMs()),
				FString::Printf(TEXT("%.1f"), 1000.0 / Rate) })));
			return Replies;
		}
		if (Sub == TEXT("rate") && Words.Num() == 3)
		{
			if (!Words[2].IsNumeric())
			{
				return BadArgument(Command);
			}
			const double Rate = FCString::Atod(*Words[2]);
			if (Rate < 1.0 || Rate > 10000.0)
			{
				return BadArgument(Command);
			}
			Target.SetTickRate(Rate);
			return { Ok(McLang::Format(TEXT("commands.tick.rate.success"), { FString::SanitizeFloat(Rate, 1) })) };
		}
		if (Sub == TEXT("freeze") && Words.Num() == 2)
		{
			Target.SetTickFrozen(true);
			return { Ok(McLang::Get(TEXT("commands.tick.status.frozen"))) };
		}
		if (Sub == TEXT("unfreeze") && Words.Num() == 2)
		{
			Target.SetTickFrozen(false);
			return { Ok(McLang::Get(TEXT("commands.tick.status.running"))) };
		}
		if (Sub == TEXT("step") && Words.Num() <= 3)
		{
			int32 Count = 1;
			if (Words.Num() == 3)
			{
				if (!Words[2].IsNumeric())
				{
					return BadArgument(Command);
				}
				Count = FCString::Atoi(*Words[2]);
				if (Count < 1)
				{
					return BadArgument(Command);
				}
			}
			if (!Target.IsTickFrozen())
			{
				return { Fail(McLang::Get(TEXT("commands.tick.step.fail"))) };
			}
			Target.StepTicks(Count);
			return { Ok(McLang::Format(TEXT("commands.tick.step.success"), { FString::FromInt(Count) })) };
		}
		return Unknown(Command, Command.Len());
	}

	// /damage <targets> <amount> [<damage type>]: players only. A type the game has but this port has no kind for hurts as generic damage.
	FReplies RunDamage(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3)
		{
			return Unknown(Command, Command.Len());
		}
		if (Words.Num() > 4)
		{
			return BadArgument(Command);
		}
		if (!Words[2].IsNumeric())
		{
			return BadArgument(Command);
		}
		const double Amount = FCString::Atod(*Words[2]);
		if (Amount < 0.0)
		{
			return { Fail(FString::Printf(TEXT("Float must not be less than 0.0: found %s"), *FString::SanitizeFloat(Amount, 1))), Fail(Command + McLang::Get(TEXT("command.context.here"))) };
		}
		FString Type = TEXT("generic");
		if (Words.Num() == 4)
		{
			Type = Words[3];
			Type.RemoveFromStart(TEXT("minecraft:"));
			if (!IFileManager::Get().FileExists(*(McCache::Root() / TEXT("client/data/minecraft/damage_type") / Type + TEXT(".json"))))
			{
				return { Fail(FString::Printf(TEXT("Can't find element '%s' of type 'minecraft:damage_type'"), *(Words[3].Contains(TEXT(":")) ? Words[3] : TEXT("minecraft:") + Words[3]))),
					Fail(Command + McLang::Get(TEXT("command.context.here"))) };
			}
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
		}
		TArray<FString> Hurt;
		for (const FString& Player : Players)
		{
			if (Target.Hurt(Player, float(Amount), Type))
			{
				Hurt.Add(Player);
			}
		}
		if (Hurt.IsEmpty())
		{
			return { Fail(McLang::Get(TEXT("commands.damage.invulnerable"))) };
		}
		return { Ok(McLang::Format(TEXT("commands.damage.success"), { FString::SanitizeFloat(Amount, 1), Hurt.Num() == 1 ? Hurt[0] : FString::FromInt(Hurt.Num()) })) };
	}

	// /rotate <targets> <yaw> <pitch>: turns players. Only the yaw is applied (the pitch is told back but not set).
	FReplies RunRotate(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 4)
		{
			return Unknown(Command, Command.Len());
		}
		if (Words.Num() > 4)
		{
			return BadArgument(Command);
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
		}
		for (const FString& Player : Players)
		{
			const bool bRelative = Words[2].StartsWith(TEXT("~"));
			const FString Number = bRelative ? Words[2].Mid(1) : Words[2];
			if (!Number.IsEmpty() && !Number.IsNumeric())
			{
				return BadArgument(Command);
			}
			double Yaw = Number.IsEmpty() ? 0.0 : FCString::Atod(*Number);
			FVector Feet;
			if (!Target.GetPosition(Player, Feet))
			{
				continue;
			}
			double Current = 0.0;
			if (bRelative)
			{
				Target.GetYaw(Player, Current);
				Yaw += Current;
			}
			Target.SetYaw(Player, Yaw);
		}
		return { Ok(McLang::Format(TEXT("commands.rotate.success"), { Players.Num() == 1 ? Players[0] : FString::FromInt(Players.Num()) })) };
	}

	// /scoreboard: objectives and scores, kept for the session (in the order made; the game lists them in an order of its own). Display slots are
	// remembered but nothing draws a sidebar, list or below-name yet, and no criterion other than dummy and trigger changes by itself.
	struct FObjective
	{
		FString Name;
		FString Criteria;
		FString Display;
		FString RenderType = TEXT("integer");
		TMap<FString, int32> Scores;
		TSet<FString> Enabled;
	};

	TArray<FObjective>& Objectives()
	{
		static TArray<FObjective> All;
		return All;
	}

	TMap<FString, FString>& DisplaySlots()
	{
		static TMap<FString, FString> Slots;
		return Slots;
	}

	FObjective* FindObjective(const FString& Name)
	{
		return Objectives().FindByPredicate([&](const FObjective& O) { return O.Name == Name; });
	}

	// The text of a JSON text component ({"text":"x"} or "x"), else the words as typed.
	FString PlainText(const FString& Json)
	{
		FString Text = Json.TrimStartAndEnd();
		int32 At = Text.Find(TEXT("\"text\""));
		if (Text.StartsWith(TEXT("{")) && At != INDEX_NONE)
		{
			int32 Open = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 6);
			const int32 Close = Open == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open + 1);
			if (Close != INDEX_NONE)
			{
				return Text.Mid(Open + 1, Close - Open - 1);
			}
		}
		if (Text.Len() >= 2 && Text.StartsWith(TEXT("\"")) && Text.EndsWith(TEXT("\"")))
		{
			return Text.Mid(1, Text.Len() - 2);
		}
		return Text;
	}

	FString Shown(const FObjective& Objective)
	{
		return TEXT("[") + (Objective.Display.IsEmpty() ? Objective.Name : Objective.Display) + TEXT("]");
	}

	FReplies RunScoreboard(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3)
		{
			return Unknown(Command, Command.Len());
		}
		const auto UnknownObjective = [](const FString& Name) { return FReplies{ Fail(FString::Printf(TEXT("Unknown scoreboard objective '%s'"), *Name)) }; };
		// Where the n-th word begins in the command.
		const auto WordStart = [&](int32 Index)
		{
			int32 At = 0;
			for (int32 I = 0; I < Index; ++I)
			{
				At += Words[I].Len() + 1;
			}
			return At;
		};
		if (Words[1] == TEXT("objectives"))
		{
			const FString& Sub = Words[2];
			if (Sub == TEXT("list") && Words.Num() == 3)
			{
				if (Objectives().IsEmpty())
				{
					return { Ok(McLang::Get(TEXT("commands.scoreboard.objectives.list.empty"))) };
				}
				TArray<FString> Names;
				for (const FObjective& O : Objectives())
				{
					Names.Add(Shown(O));
				}
				return { Ok(McLang::Format(TEXT("commands.scoreboard.objectives.list.success"), { FString::FromInt(Names.Num()), FString::Join(Names, TEXT(", ")) })) };
			}
			if (Sub == TEXT("add") && Words.Num() >= 5)
			{
				for (int32 I = 0; I < Words[3].Len(); ++I)
				{
					const TCHAR C = Words[3][I];
					if (!(FChar::IsAlnum(C) || C == '_' || C == '.' || C == '+' || C == '-'))
					{
						return { Fail(McLang::Get(TEXT("command.expected.separator"))), Fail(BorderContext(Command, WordStart(3) + I)) };
					}
				}
				if (FindObjective(Words[3]))
				{
					return { Fail(McLang::Get(TEXT("commands.scoreboard.objectives.add.duplicate"))) };
				}
				FObjective Made;
				Made.Name = Words[3];
				Made.Criteria = Words[4];
				if (Words.Num() > 5)
				{
					TArray<FString> Rest(Words.GetData() + 5, Words.Num() - 5);
					Made.Display = PlainText(FString::Join(Rest, TEXT(" ")));
				}
				Objectives().Add(Made);
				return { Ok(McLang::Format(TEXT("commands.scoreboard.objectives.add.success"), { Shown(Made) })) };
			}
			if (Sub == TEXT("remove") && Words.Num() == 4)
			{
				FObjective* O = FindObjective(Words[3]);
				if (!O)
				{
					return UnknownObjective(Words[3]);
				}
				const FString Name = Shown(*O);
				const FString Key = O->Name;
				Objectives().RemoveAll([&](const FObjective& Each) { return Each.Name == Key; });
				for (auto It = DisplaySlots().CreateIterator(); It; ++It)
				{
					if (It.Value() == Key)
					{
						It.RemoveCurrent();
					}
				}
				return { Ok(McLang::Format(TEXT("commands.scoreboard.objectives.remove.success"), { Name })) };
			}
			if (Sub == TEXT("setdisplay") && (Words.Num() == 4 || Words.Num() == 5))
			{
				if (Words.Num() == 5 && !FindObjective(Words[4]))
				{
					return UnknownObjective(Words[4]);
				}
				if (Words.Num() == 4)
				{
					DisplaySlots().Remove(Words[3]);
					return { Ok(McLang::Format(TEXT("commands.scoreboard.objectives.display.cleared"), { Words[3] })) };
				}
				DisplaySlots().Add(Words[3], Words[4]);
				return { Ok(McLang::Format(TEXT("commands.scoreboard.objectives.display.set"), { Words[3], Words[4] })) };
			}
			if (Sub == TEXT("modify") && Words.Num() >= 5)
			{
				FObjective* O = FindObjective(Words[3]);
				if (!O)
				{
					return UnknownObjective(Words[3]);
				}
				if (Words[4] == TEXT("displayname") && Words.Num() >= 6)
				{
					TArray<FString> Rest(Words.GetData() + 5, Words.Num() - 5);
					const FString Old = O->Name;
					O->Display = PlainText(FString::Join(Rest, TEXT(" ")));
					return { Ok(McLang::Format(TEXT("commands.scoreboard.objectives.modify.displayname"), { Old, Shown(*O) })) };
				}
				if (Words[4] == TEXT("rendertype") && Words.Num() == 6 && (Words[5] == TEXT("integer") || Words[5] == TEXT("hearts")))
				{
					O->RenderType = Words[5];
					return { Ok(McLang::Format(TEXT("commands.scoreboard.objectives.modify.rendertype"), { Shown(*O) })) };
				}
				return BadArgument(Command);
			}
			return Unknown(Command, Command.Len());
		}
		if (Words[1] != TEXT("players"))
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Sub = Words[2];
		// Who a target word names: players by selector or name, else a name the scoreboard tracks (a fake player).
		const auto Resolve = [&](const FString& Token, TArray<FString>& Out, FString& Error)
		{
			if (Token == TEXT("*"))
			{
				Out.Reset();
				for (const FObjective& O : Objectives())
				{
					for (const TPair<FString, int32>& Pair : O.Scores)
					{
						Out.AddUnique(Pair.Key);
					}
				}
				return true;
			}
			if (Token.StartsWith(TEXT("@")))
			{
				return ResolvePlayers(Token, Sender, Target, Out, Error);
			}
			Out = { Token };
			return true;
		};
		if (Sub == TEXT("list") && Words.Num() <= 4)
		{
			TArray<FString> Who;
			FString Error;
			if (Words.Num() == 3)
			{
				for (const FObjective& O : Objectives())
				{
					for (const TPair<FString, int32>& Pair : O.Scores)
					{
						Who.AddUnique(Pair.Key);
					}
				}
				if (Who.IsEmpty())
				{
					return { Ok(McLang::Get(TEXT("commands.scoreboard.players.list.empty"))) };
				}
				return { Ok(McLang::Format(TEXT("commands.scoreboard.players.list.success"), { FString::FromInt(Who.Num()), FString::Join(Who, TEXT(", ")) })) };
			}
			if (!Resolve(Words[3], Who, Error) || Who.IsEmpty())
			{
				return { Fail(Error.IsEmpty() ? McLang::Get(TEXT("argument.entity.notfound.entity")) : Error) };
			}
			TArray<FString> Lines;
			for (const FObjective& O : Objectives())
			{
				if (const int32* Value = O.Scores.Find(Who[0]))
				{
					Lines.Add(McLang::Format(TEXT("commands.scoreboard.players.list.entity.entry"), { Shown(O), FString::FromInt(*Value) }));
				}
			}
			if (Lines.IsEmpty())
			{
				return { Ok(McLang::Format(TEXT("commands.scoreboard.players.list.entity.empty"), { Who[0] })) };
			}
			FReplies Replies = { Ok(McLang::Format(TEXT("commands.scoreboard.players.list.entity.success"), { Who[0], FString::FromInt(Lines.Num()) })) };
			for (const FString& Line : Lines)
			{
				Replies.Add(Ok(Line));
			}
			return Replies;
		}
		if (Words.Num() < 4)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Who;
		FString Error;
		if (!Resolve(Words[3], Who, Error))
		{
			return { Fail(Error) };
		}
		const auto Count = [&]() { return Who.Num() == 1 ? Who[0] : FString::FromInt(Who.Num()); };
		if (Sub == TEXT("reset") && (Words.Num() == 4 || Words.Num() == 5))
		{
			FObjective* O = Words.Num() == 5 ? FindObjective(Words[4]) : nullptr;
			if (Words.Num() == 5 && !O)
			{
				return UnknownObjective(Words[4]);
			}
			for (const FString& Name : Who)
			{
				for (FObjective& Each : Objectives())
				{
					if (!O || &Each == O)
					{
						Each.Scores.Remove(Name);
					}
				}
			}
			if (O)
			{
				return { Ok(McLang::Format(Who.Num() == 1 ? TEXT("commands.scoreboard.players.reset.specific.single") : TEXT("commands.scoreboard.players.reset.specific.multiple"), { Shown(*O), Count() })) };
			}
			return { Ok(McLang::Format(Who.Num() == 1 ? TEXT("commands.scoreboard.players.reset.all.single") : TEXT("commands.scoreboard.players.reset.all.multiple"), { Count() })) };
		}
		if (Words.Num() < 5)
		{
			return Unknown(Command, Command.Len());
		}
		FObjective* O = FindObjective(Words[4]);
		if (!O)
		{
			return UnknownObjective(Words[4]);
		}
		if (Sub == TEXT("get") && Words.Num() == 5)
		{
			const int32* Value = O->Scores.Find(Who[0]);
			if (!Value)
			{
				return { Fail(McLang::Format(TEXT("commands.scoreboard.players.get.null"), { O->Name, Who[0] })) };
			}
			return { Ok(McLang::Format(TEXT("commands.scoreboard.players.get.success"), { Who[0], FString::FromInt(*Value), Shown(*O) })) };
		}
		if (Sub == TEXT("enable") && Words.Num() == 5)
		{
			if (O->Criteria != TEXT("trigger"))
			{
				return { Fail(McLang::Get(TEXT("commands.scoreboard.players.enable.invalid"))) };
			}
			int32 Changed = 0;
			for (const FString& Name : Who)
			{
				bool bAlready = false;
				O->Enabled.Add(Name, &bAlready);
				Changed += bAlready ? 0 : 1;
			}
			if (!Changed)
			{
				return { Fail(McLang::Get(TEXT("commands.scoreboard.players.enable.failed"))) };
			}
			return { Ok(McLang::Format(Who.Num() == 1 ? TEXT("commands.scoreboard.players.enable.success.single") : TEXT("commands.scoreboard.players.enable.success.multiple"), { Shown(*O), Count() })) };
		}
		if ((Sub == TEXT("set") || Sub == TEXT("add") || Sub == TEXT("remove")) && Words.Num() == 6)
		{
			if (!Words[5].IsNumeric() || Words[5].Contains(TEXT(".")))
			{
				return BadArgument(Command);
			}
			const int32 Amount = FCString::Atoi(*Words[5]);
			if (Sub != TEXT("set") && Amount < 0)
			{
				return { Fail(FString::Printf(TEXT("Integer must not be less than 0: found %d"), Amount)), Fail(BorderContext(Command, WordStart(5))) };
			}
			int32 Last = 0;
			for (const FString& Name : Who)
			{
				int32& Slot = O->Scores.FindOrAdd(Name);
				Slot = Sub == TEXT("set") ? Amount : Sub == TEXT("add") ? Slot + Amount : Slot - Amount;
				Last = Slot;
			}
			if (Sub == TEXT("set"))
			{
				return { Ok(McLang::Format(Who.Num() == 1 ? TEXT("commands.scoreboard.players.set.success.single") : TEXT("commands.scoreboard.players.set.success.multiple"),
					Who.Num() == 1 ? TArray<FString>{ Shown(*O), Who[0], FString::FromInt(Amount) } : TArray<FString>{ Shown(*O), Count(), FString::FromInt(Amount) })) };
			}
			const bool bAdd = Sub == TEXT("add");
			if (Who.Num() == 1)
			{
				return { Ok(McLang::Format(bAdd ? TEXT("commands.scoreboard.players.add.success.single") : TEXT("commands.scoreboard.players.remove.success.single"),
					{ FString::FromInt(Amount), Shown(*O), Who[0], FString::FromInt(Last) })) };
			}
			return { Ok(McLang::Format(bAdd ? TEXT("commands.scoreboard.players.add.success.multiple") : TEXT("commands.scoreboard.players.remove.success.multiple"),
				{ FString::FromInt(Amount), Shown(*O), Count() })) };
		}
		if (Sub == TEXT("operation") && Words.Num() == 8)
		{
			TArray<FString> Sources;
			if (!Resolve(Words[6], Sources, Error) || Sources.IsEmpty())
			{
				return { Fail(Error.IsEmpty() ? McLang::Get(TEXT("argument.entity.notfound.entity")) : Error) };
			}
			FObjective* From = FindObjective(Words[7]);
			if (!From)
			{
				return UnknownObjective(Words[7]);
			}
			const FString& Op = Words[5];
			if (Op != TEXT("=") && Op != TEXT("+=") && Op != TEXT("-=") && Op != TEXT("*=") && Op != TEXT("/=") && Op != TEXT("%=") && Op != TEXT("<") && Op != TEXT(">") && Op != TEXT("><"))
			{
				return BadArgument(Command);
			}
			int32 Last = 0;
			for (const FString& Name : Who)
			{
				const int32* Other = From->Scores.Find(Sources[0]);
				if (!Other)
				{
					continue;
				}
				int32& Slot = O->Scores.FindOrAdd(Name);
				if (Op == TEXT("=")) { Slot = *Other; }
				else if (Op == TEXT("+=")) { Slot += *Other; }
				else if (Op == TEXT("-=")) { Slot -= *Other; }
				else if (Op == TEXT("*=")) { Slot *= *Other; }
				else if (Op == TEXT("/=") || Op == TEXT("%="))
				{
					if (*Other == 0)
					{
						return { Fail(McLang::Get(TEXT("arguments.operation.div0"))) };
					}
					Slot = Op == TEXT("/=") ? int32(FMath::FloorToInt32(double(Slot) / *Other)) : Slot - *Other * FMath::FloorToInt32(double(Slot) / *Other);
				}
				else if (Op == TEXT("<")) { Slot = FMath::Min(Slot, *Other); }
				else if (Op == TEXT(">")) { Slot = FMath::Max(Slot, *Other); }
				else
				{
					// Swap.
					int32& Theirs = From->Scores.FindOrAdd(Sources[0]);
					Swap(Slot, Theirs);
				}
				Last = Slot;
			}
			if (Who.Num() == 1)
			{
				return { Ok(McLang::Format(TEXT("commands.scoreboard.players.operation.success.single"), { Shown(*O), Who[0], FString::FromInt(Last) })) };
			}
			return { Ok(McLang::Format(TEXT("commands.scoreboard.players.operation.success.multiple"), { Shown(*O), Count() })) };
		}
		return Unknown(Command, Command.Len());
	}

	// /execute: as, at, positioned (a position, or as <players>), if/unless (score, entity, block, loaded, dimension), store (result|success) score, run, and
	// rotated/facing/in/align/anchored (taken and ignored). Entities are the players (nothing else is selected); no on, summon, data, bossbar, blocks, predicate forms.
	bool ParseRange(const FString& Text, int32& Min, int32& Max)
	{
		Min = MIN_int32;
		Max = MAX_int32;
		int32 At = INDEX_NONE;
		if (Text.FindChar(TEXT('.'), At) && Text.Mid(At).StartsWith(TEXT("..")))
		{
			const FString Low = Text.Left(At);
			const FString High = Text.Mid(At + 2);
			if ((!Low.IsEmpty() && !Low.IsNumeric()) || (!High.IsEmpty() && !High.IsNumeric()) || (Low.IsEmpty() && High.IsEmpty()))
			{
				return false;
			}
			if (!Low.IsEmpty()) { Min = FCString::Atoi(*Low); }
			if (!High.IsEmpty()) { Max = FCString::Atoi(*High); }
			return true;
		}
		if (!Text.IsNumeric() || Text.Contains(TEXT(".")))
		{
			return false;
		}
		Min = Max = FCString::Atoi(*Text);
		return true;
	}

	struct FExecCondition
	{
		bool bHas = false;
		bool bPass = false;
		bool bCount = false;
		int32 Count = 0;
	};

	FReplies ExecuteFrom(const FString& Original, const TArray<FString>& W, int32 I, const FMcCommandSender& Sender, IMcCommandTarget& Target, const FExecCondition& Last)
	{
		const auto Incomplete = [&]() { return Unknown(Original, Original.Len()); };
		if (I >= W.Num())
		{
			if (!Last.bHas)
			{
				return Incomplete();
			}
			const bool bCount = Last.bCount && (Last.bPass || true);
			if (Last.bPass)
			{
				return { Ok(bCount ? McLang::Format(TEXT("commands.execute.conditional.pass_count"), { FString::FromInt(Last.Count) }) : McLang::Get(TEXT("commands.execute.conditional.pass"))) };
			}
			return { Ok(bCount ? McLang::Format(TEXT("commands.execute.conditional.fail_count"), { FString::FromInt(Last.Count) }) : McLang::Get(TEXT("commands.execute.conditional.fail"))) };
		}
		const FString& Key = W[I];
		const auto Merge = [](FReplies& Into, const FReplies& More) { Into.Append(More); };
		if (Key == TEXT("run"))
		{
			if (I + 1 >= W.Num())
			{
				return Incomplete();
			}
			TArray<FString> Rest(W.GetData() + I + 1, W.Num() - I - 1);
			return McCommands::Execute(FString::Join(Rest, TEXT(" ")), Sender, Target);
		}
		if (Key == TEXT("as") || Key == TEXT("at"))
		{
			if (I + 1 >= W.Num())
			{
				return Incomplete();
			}
			TArray<FString> Players;
			FString Error;
			if (!ResolvePlayers(W[I + 1], Sender, Target, Players, Error))
			{
				return {};
			}
			FReplies All;
			for (const FString& Player : Players)
			{
				FMcCommandSender Next = Sender;
				FVector Feet;
				const bool bHave = Target.GetPosition(Player, Feet);
				if (Key == TEXT("as"))
				{
					Next.Name = Player;
				}
				if (bHave)
				{
					Next.Position = Feet;
				}
				Merge(All, ExecuteFrom(Original, W, I + 2, Next, Target, FExecCondition()));
			}
			return All;
		}
		if (Key == TEXT("positioned"))
		{
			if (I + 1 >= W.Num())
			{
				return Incomplete();
			}
			FMcCommandSender Next = Sender;
			if (W[I + 1] == TEXT("as"))
			{
				if (I + 2 >= W.Num())
				{
					return Incomplete();
				}
				TArray<FString> Players;
				FString Error;
				if (!ResolvePlayers(W[I + 2], Sender, Target, Players, Error))
				{
					return {};
				}
				FReplies All;
				for (const FString& Player : Players)
				{
					FVector Feet;
					FMcCommandSender Each = Sender;
					if (Target.GetPosition(Player, Feet))
					{
						Each.Position = Feet;
					}
					Merge(All, ExecuteFrom(Original, W, I + 3, Each, Target, FExecCondition()));
				}
				return All;
			}
			const FVector Base = SenderFeet(Sender, Target);
			FVector Pos = Base;
			if (I + 3 >= W.Num() || !ParseCoordinate(W[I + 1], Base.X, true, Pos.X) || !ParseCoordinate(W[I + 2], Base.Y, false, Pos.Y) || !ParseCoordinate(W[I + 3], Base.Z, true, Pos.Z))
			{
				return I + 3 >= W.Num() ? Incomplete() : BadArgument(Original);
			}
			Next.Position = Pos;
			return ExecuteFrom(Original, W, I + 4, Next, Target, FExecCondition());
		}
		if (Key == TEXT("rotated") || Key == TEXT("facing") || Key == TEXT("in") || Key == TEXT("align") || Key == TEXT("anchored"))
		{
			// Taken and ignored: the arguments each form has.
			int32 Skip = Key == TEXT("rotated") ? (I + 1 < W.Num() && W[I + 1] == TEXT("as") ? 2 : 2) : Key == TEXT("facing") ? (I + 1 < W.Num() && W[I + 1] == TEXT("entity") ? 3 : 3) : 1;
			if (Key == TEXT("rotated") && I + 1 < W.Num() && W[I + 1] == TEXT("as")) { Skip = 2; }
			if (I + Skip >= W.Num())
			{
				return Incomplete();
			}
			return ExecuteFrom(Original, W, I + 1 + Skip, Sender, Target, FExecCondition());
		}
		if (Key == TEXT("store"))
		{
			if (I + 4 >= W.Num() || (W[I + 1] != TEXT("result") && W[I + 1] != TEXT("success")) || W[I + 2] != TEXT("score"))
			{
				return I + 4 >= W.Num() ? Incomplete() : BadArgument(Original);
			}
			FObjective* O = FindObjective(W[I + 4]);
			if (!O)
			{
				return { Fail(FString::Printf(TEXT("Unknown scoreboard objective '%s'"), *W[I + 4])) };
			}
			TArray<FString> Holders;
			FString Error;
			if (W[I + 3].StartsWith(TEXT("@")))
			{
				if (!ResolvePlayers(W[I + 3], Sender, Target, Holders, Error))
				{
					return {};
				}
			}
			else
			{
				Holders = { W[I + 3] };
			}
			FReplies Got = ExecuteFrom(Original, W, I + 5, Sender, Target, FExecCondition());
			bool bSuccess = false;
			for (const FMcCommandReply& Reply : Got)
			{
				bSuccess |= !Reply.bError;
			}
			int32 Value = bSuccess ? 1 : 0;
			if (W[I + 1] == TEXT("result") && !Got.IsEmpty())
			{
				// The number the command reported: "Count: n", "has n", "now n".
				const FString& Text = Got[0].Text;
				for (const TCHAR* Marker : { TEXT("Count: "), TEXT(" has "), TEXT("(now ") })
				{
					int32 At = Text.Find(Marker);
					if (At != INDEX_NONE)
					{
						Value = FCString::Atoi(*Text.Mid(At + FCString::Strlen(Marker)));
						break;
					}
				}
			}
			for (const FString& Holder : Holders)
			{
				O->Scores.FindOrAdd(Holder) = Value;
			}
			return Got;
		}
		if (Key == TEXT("if") || Key == TEXT("unless"))
		{
			const bool bUnless = Key == TEXT("unless");
			if (I + 1 >= W.Num())
			{
				return Incomplete();
			}
			const FString& Kind = W[I + 1];
			FExecCondition Next;
			Next.bHas = true;
			int32 End = I + 2;
			bool bPass = false;
			if (Kind == TEXT("score"))
			{
				if (I + 5 >= W.Num())
				{
					return Incomplete();
				}
				FObjective* O = FindObjective(W[I + 3]);
				if (!O)
				{
					return { Fail(FString::Printf(TEXT("Unknown scoreboard objective '%s'"), *W[I + 3])) };
				}
				TArray<FString> Who;
				FString Error;
				if (W[I + 2].StartsWith(TEXT("@")) ? !ResolvePlayers(W[I + 2], Sender, Target, Who, Error) : (Who = { W[I + 2] }, false))
				{
					return {};
				}
				const FString& Op = W[I + 4];
				if (Op == TEXT("matches"))
				{
					int32 Min, Max;
					if (!ParseRange(W[I + 5], Min, Max))
					{
						return BadArgument(Original);
					}
					const int32* Value = Who.IsEmpty() ? nullptr : O->Scores.Find(Who[0]);
					bPass = Value && *Value >= Min && *Value <= Max;
					End = I + 6;
				}
				else
				{
					if (I + 6 >= W.Num())
					{
						return Incomplete();
					}
					FObjective* P = FindObjective(W[I + 6]);
					if (!P)
					{
						return { Fail(FString::Printf(TEXT("Unknown scoreboard objective '%s'"), *W[I + 6])) };
					}
					TArray<FString> Others;
					if (W[I + 5].StartsWith(TEXT("@")) ? !ResolvePlayers(W[I + 5], Sender, Target, Others, Error) : (Others = { W[I + 5] }, false))
					{
						return {};
					}
					const int32* A = Who.IsEmpty() ? nullptr : O->Scores.Find(Who[0]);
					const int32* B = Others.IsEmpty() ? nullptr : P->Scores.Find(Others[0]);
					if (Op != TEXT("<") && Op != TEXT("<=") && Op != TEXT("=") && Op != TEXT(">=") && Op != TEXT(">"))
					{
						return BadArgument(Original);
					}
					bPass = A && B && (Op == TEXT("<") ? *A < *B : Op == TEXT("<=") ? *A <= *B : Op == TEXT("=") ? *A == *B : Op == TEXT(">=") ? *A >= *B : *A > *B);
					End = I + 7;
				}
			}
			else if (Kind == TEXT("entity"))
			{
				if (I + 2 >= W.Num())
				{
					return Incomplete();
				}
				// The players that match; a selector with filters other than a player type matches none (mobs are not selected).
				TArray<FString> Who;
				FString Error;
				const FString& Token = W[I + 2];
				const int32 Bracket = Token.Find(TEXT("["));
				if (Bracket != INDEX_NONE)
				{
					const FString Filters = Token.Mid(Bracket);
					if (Token.StartsWith(TEXT("@e")) && (Filters.Contains(TEXT("type=minecraft:player")) || Filters.Contains(TEXT("type=player"))))
					{
						Who = Target.GetPlayerNames();
					}
				}
				else if (Token == TEXT("@e"))
				{
					Who = Target.GetPlayerNames();
				}
				else if (!ResolvePlayers(Token, Sender, Target, Who, Error))
				{
					Who.Reset();
				}
				Next.bCount = true;
				Next.Count = Who.Num();
				bPass = Who.Num() > 0;
				End = I + 3;
			}
			else if (Kind == TEXT("block"))
			{
				if (I + 5 >= W.Num())
				{
					return Incomplete();
				}
				const FVector Base = SenderFeet(Sender, Target);
				FIntVector Pos;
				if (!ParseBlockPos(W, I + 2, Base, Pos))
				{
					return BadArgument(Original);
				}
				const TOptional<FMcStateId> Wanted = ParseBlock(W[I + 5]);
				if (!Wanted.IsSet())
				{
					return BadArgument(Original);
				}
				FMcStateId Have;
				bPass = Target.GetBlock(Pos, Have) && Have == *Wanted;
				End = I + 6;
			}
			else if (Kind == TEXT("loaded"))
			{
				if (I + 4 >= W.Num())
				{
					return Incomplete();
				}
				const FVector Base = SenderFeet(Sender, Target);
				FIntVector Pos;
				if (!ParseBlockPos(W, I + 2, Base, Pos))
				{
					return BadArgument(Original);
				}
				FMcStateId Have;
				bPass = Target.GetBlock(Pos, Have);
				End = I + 5;
			}
			else if (Kind == TEXT("dimension"))
			{
				if (I + 2 >= W.Num())
				{
					return Incomplete();
				}
				bPass = W[I + 2] == TEXT("minecraft:overworld") || W[I + 2] == TEXT("overworld");
				End = I + 3;
			}
			else
			{
				return BadArgument(Original);
			}
			Next.bPass = bUnless ? !bPass : bPass;
			if (!Next.bPass && End < W.Num())
			{
				// A failed test stops this branch; nothing more is said.
				return {};
			}
			return ExecuteFrom(Original, W, End, Sender, Target, Next);
		}
		return BadArgument(Original);
	}

	FReplies RunExecute(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		return ExecuteFrom(Command, Words, 1, Sender, Target, FExecCondition());
	}

	// /locate biome <biome>: the nearest loaded part of the world with that biome (the game searches further, generating as it goes; structures and
	// points of interest are not built).
	FReplies RunLocate(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3 || Words[1] != TEXT("biome"))
		{
			return Unknown(Command, Words.Num() < 3 ? Command.Len() : Command.Len());
		}
		if (Words.Num() > 3)
		{
			return BadArgument(Command);
		}
		FString Id = Words[2];
		const FString Bare = Id.StartsWith(TEXT("minecraft:")) ? Id.Mid(10) : Id;
		Id = TEXT("minecraft:") + Bare;
		FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
		Biomes.EnsureLoaded();
		const TOptional<FMcBiomeId> Wanted = Biomes.Find(FName(*Id));
		if (!Wanted.IsSet())
		{
			return { Fail(FString::Printf(TEXT("Can't find element '%s' of type 'minecraft:worldgen/biome'"), *Id)), Fail(Command + McLang::Get(TEXT("command.context.here"))) };
		}
		const FVector From = SenderFeet(Sender, Target);
		FIntVector Found;
		if (!Target.FindBiome(*Wanted, From, Found))
		{
			return { Fail(FString::Printf(TEXT("Could not find a biome of type \"%s\" within a reasonable distance"), *Id)) };
		}
		const int32 Away = FMath::RoundToInt32(FMath::Sqrt(FMath::Square(double(Found.X) - From.X) + FMath::Square(double(Found.Z) - From.Z)));
		return { Ok(McLang::Format(TEXT("commands.locate.biome.success"), { Id, FString::Printf(TEXT("[%d, %d, %d]"), Found.X, Found.Y, Found.Z), FString::FromInt(Away) })) };
	}

	// /swing <targets> [mainhand|offhand]: players swing their arm (what is drawn is the first-person arm on the machine playing them).
	FReplies RunSwing(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		if (Words.Num() > 3 || (Words.Num() == 3 && Words[2] != TEXT("mainhand") && Words[2] != TEXT("offhand")))
		{
			return BadArgument(Command);
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
		}
		if (Players.IsEmpty())
		{
			return { Fail(McLang::Get(TEXT("commands.swing.failed.notliving"))) };
		}
		for (const FString& Player : Players)
		{
			Target.SwingArm(Player);
		}
		return { Ok(Players.Num() == 1 ? McLang::Format(TEXT("commands.swing.success.single"), { Players[0] }) : McLang::Format(TEXT("commands.swing.success.multiple"), { FString::FromInt(Players.Num()) })) };
	}

	// /forceload add|remove|query: the chunks marked to stay loaded, kept for the session (nothing keeps them loaded yet).
	TSet<FIntPoint>& ForceLoaded()
	{
		static TSet<FIntPoint> Chunks;
		return Chunks;
	}

	FReplies RunSpreadPlayers(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		// /spreadplayers <x> <z> <spreadDistance> <maxRange> [under <maxHeight>] <respectTeams> <targets>
		const bool bUnder = Words.Num() == 9 && Words[5] == TEXT("under");
		if (Words.Num() != 7 && !bUnder)
		{
			return Unknown(Command, Command.Len());
		}
		const FVector Here = SenderFeet(Sender, Target);
		double CentreX = 0.0, CentreZ = 0.0, Spread = 0.0, Range = 0.0, MaxHeight = 319.0;
		if (!ParseCoordinate(Words[1], Here.X, true, CentreX) || !ParseCoordinate(Words[2], Here.Z, true, CentreZ)
			|| !ParseNumber(Words[3], Spread) || !ParseNumber(Words[4], Range) || (bUnder && !ParseNumber(Words[6], MaxHeight)))
		{
			return BadArgument(Command);
		}
		if (Spread < 0.0)
		{
			return { Fail(FString::Printf(TEXT("Float must not be less than 0.0: found %s"), *JavaDouble(Spread))) };
		}
		if (Range < Spread + 1.0)
		{
			return { Fail(FString::Printf(TEXT("Float must not be less than %s: found %s"), *JavaDouble(Spread + 1.0), *JavaDouble(Range))) };
		}
		const int32 TeamsWord = bUnder ? 7 : 5;
		const FString& Teams = Words[TeamsWord];
		if (Teams != TEXT("true") && Teams != TEXT("false"))
		{
			return BadArgument(Command);
		}
		TArray<FString> Who;
		FString Error;
		if (!ResolvePlayers(Words.Last(), Sender, Target, Who, Error))
		{
			return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
		}
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		TArray<FVector> Spots;
		for (int32 Index = 0; Index < Who.Num(); ++Index)
		{
			FVector Spot = FVector::ZeroVector;
			bool bFound = false;
			for (int32 Try = 0; Try < 10000 && !bFound; ++Try)
			{
				const double X = FMath::FloorToDouble(CentreX + FMath::FRandRange(-Range, Range)) + 0.5;
				const double Z = FMath::FloorToDouble(CentreZ + FMath::FRandRange(-Range, Range)) + 0.5;
				bool bFar = true;
				for (const FVector& Other : Spots)
				{
					bFar = bFar && FVector2D::Distance(FVector2D(X, Z), FVector2D(Other.X, Other.Z)) >= Spread;
				}
				if (!bFar)
				{
					continue;
				}
				for (int32 Y = FMath::FloorToInt(MaxHeight); Y >= -64; --Y)
				{
					FMcStateId State;
					if (Target.GetBlock(FIntVector(FMath::FloorToInt(X), Y, FMath::FloorToInt(Z)), State) && !Registry.IsAir(State))
					{
						Spot = FVector(X, Y + 1.0, Z);
						bFound = true;
						break;
					}
				}
			}
			if (!bFound)
			{
				return { Fail(McLang::Format(TEXT("commands.spreadplayers.failed.entities"), { FString::FromInt(Who.Num()), JavaDouble(CentreX), JavaDouble(CentreZ), FString::Printf(TEXT("%.0f"), Spread) })) };
			}
			Spots.Add(Spot);
		}
		double Total = 0.0;
		for (int32 Index = 0; Index < Spots.Num(); ++Index)
		{
			Target.Teleport(Who[Index], Spots[Index]);
			double Nearest = 0.0;
			bool bAny = false;
			for (int32 Other = 0; Other < Spots.Num(); ++Other)
			{
				const double Distance = FVector2D::Distance(FVector2D(Spots[Index].X, Spots[Index].Z), FVector2D(Spots[Other].X, Spots[Other].Z));
				if (Other != Index && (!bAny || Distance < Nearest))
				{
					Nearest = Distance;
					bAny = true;
				}
			}
			Total += Nearest;
		}
		const FString Key = Teams == TEXT("true") ? TEXT("commands.spreadplayers.success.teams") : TEXT("commands.spreadplayers.success.entities");
		return { Ok(McLang::Format(Key, { FString::FromInt(Who.Num()), JavaDouble(CentreX), JavaDouble(CentreZ), FString::Printf(TEXT("%.2f"), Total / FMath::Max(1, Spots.Num())) })) };
	}

	FReplies RunForceLoad(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		const FString Dimension = TEXT("minecraft:overworld");
		const auto Chunk = [](const FIntPoint& P) { return FString::Printf(TEXT("[%d, %d]"), P.X, P.Y); };
		const FString& Sub = Words[1];
		TSet<FIntPoint>& Chunks = ForceLoaded();
		if (Sub == TEXT("remove") && Words.Num() == 3 && Words[2] == TEXT("all"))
		{
			Chunks.Reset();
			return { Ok(McLang::Format(TEXT("commands.forceload.removed.all"), { Dimension })) };
		}
		if (Sub == TEXT("query") && Words.Num() == 2)
		{
			TArray<FIntPoint> All = Chunks.Array();
			All.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.Y != B.Y ? A.Y < B.Y : A.X < B.X; });
			if (All.IsEmpty())
			{
				return { Ok(McLang::Format(TEXT("commands.forceload.added.none"), { Dimension })) };
			}
			TArray<FString> Shown;
			for (const FIntPoint& P : All)
			{
				Shown.Add(Chunk(P));
			}
			return { Ok(All.Num() == 1 ? McLang::Format(TEXT("commands.forceload.list.single"), { Dimension, Shown[0] })
				: McLang::Format(TEXT("commands.forceload.list.multiple"), { FString::FromInt(All.Num()), Dimension, FString::Join(Shown, TEXT(", ")) })) };
		}
		const bool bAdd = Sub == TEXT("add");
		const bool bRemove = Sub == TEXT("remove");
		const bool bQuery = Sub == TEXT("query");
		if (!(bAdd || bRemove || bQuery) || (Words.Num() != 4 && Words.Num() != 6) || (bQuery && Words.Num() != 4))
		{
			return (bAdd || bRemove || bQuery) ? Unknown(Command, Command.Len()) : BadArgument(Command);
		}
		const FVector Base = SenderFeet(Sender, Target);
		double X1, Z1, X2, Z2;
		if (!ParseCoordinate(Words[2], Base.X, false, X1) || !ParseCoordinate(Words[3], Base.Z, false, Z1))
		{
			return BadArgument(Command);
		}
		X2 = X1;
		Z2 = Z1;
		if (Words.Num() == 6 && (!ParseCoordinate(Words[4], Base.X, false, X2) || !ParseCoordinate(Words[5], Base.Z, false, Z2)))
		{
			return BadArgument(Command);
		}
		if (FMath::Abs(X1) >= 30000000.0 || FMath::Abs(Z1) >= 30000000.0 || FMath::Abs(X2) >= 30000000.0 || FMath::Abs(Z2) >= 30000000.0)
		{
			return { Fail(McLang::Get(TEXT("argument.pos.outofworld"))) };
		}
		const FIntPoint Low(FMath::FloorToInt32(FMath::Min(X1, X2) / 16.0), FMath::FloorToInt32(FMath::Min(Z1, Z2) / 16.0));
		const FIntPoint High(FMath::FloorToInt32(FMath::Max(X1, X2) / 16.0), FMath::FloorToInt32(FMath::Max(Z1, Z2) / 16.0));
		if (bQuery)
		{
			const FIntPoint At(FMath::FloorToInt32(X1 / 16.0), FMath::FloorToInt32(Z1 / 16.0));
			return { Ok(McLang::Format(Chunks.Contains(At) ? TEXT("commands.forceload.query.success") : TEXT("commands.forceload.query.failure"), { Chunk(At), Dimension })) };
		}
		const int64 Count = int64(High.X - Low.X + 1) * (High.Y - Low.Y + 1);
		if (Count > 256)
		{
			return { Fail(McLang::Format(TEXT("commands.forceload.toobig"), { TEXT("256"), LexToString(Count) })) };
		}
		int32 Changed = 0;
		for (int32 X = Low.X; X <= High.X; ++X)
		{
			for (int32 Z = Low.Y; Z <= High.Y; ++Z)
			{
				if (bAdd ? !Chunks.Contains(FIntPoint(X, Z)) : Chunks.Contains(FIntPoint(X, Z)))
				{
					++Changed;
					if (bAdd) { Chunks.Add(FIntPoint(X, Z)); } else { Chunks.Remove(FIntPoint(X, Z)); }
				}
			}
		}
		if (Changed == 0)
		{
			return { Fail(McLang::Get(bAdd ? TEXT("commands.forceload.added.failure") : TEXT("commands.forceload.removed.failure"))) };
		}
		if (Low == High)
		{
			return { Ok(McLang::Format(bAdd ? TEXT("commands.forceload.added.single") : TEXT("commands.forceload.removed.single"), { Chunk(Low), Dimension })) };
		}
		return { Ok(McLang::Format(bAdd ? TEXT("commands.forceload.added.multiple") : TEXT("commands.forceload.removed.multiple"), { FString::FromInt(Changed), Dimension, Chunk(Low), Chunk(High) })) };
	}

	FReplies RunSummon(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() != 2 && Words.Num() != 5)
		{
			return Unknown(Command, Command.Len());
		}
		const FName Kind = ItemId(Words[1]);
		FVector Feet = SenderFeet(Sender, Target);
		if (Words.Num() == 5 && !(ParseCoordinate(Words[2], Feet.X, true, Feet.X) && ParseCoordinate(Words[3], Feet.Y, false, Feet.Y) && ParseCoordinate(Words[4], Feet.Z, true, Feet.Z)))
		{
			return BadArgument(Command);
		}
		const FMcMobType* Type = FMcMobType::Find(Kind);
		// Minecarts and boats are entities without a mob type.
		if (!Type && !FMcVehicle::IsVehicleType(Kind) && Kind != TEXT("minecraft:tnt"))
		{
			return { Fail(McLang::Get(TEXT("commands.summon.failed"))) };
		}
		if (Type && Type->IsHostile() && Target.GetDifficulty() == 0)
		{
			return { Fail(McLang::Get(TEXT("commands.summon.failed.peaceful"))) };
		}
		if (!Target.Summon(Kind, Feet))
		{
			return { Fail(McLang::Get(TEXT("commands.summon.invalidPosition"))) };
		}
		return { Ok(McLang::Format(TEXT("commands.summon.success"), { McLang::Get(TEXT("entity.minecraft.") + McCache::PathOf(Kind.ToString())) })) };
	}

	// /particle <name> [<pos> [<delta> <speed> <count> [force|normal]]]: the game's own form. The name must be one of the game's particle kinds.
	FReplies RunParticle(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() != 2 && Words.Num() != 5 && Words.Num() != 10 && Words.Num() != 11)
		{
			return Unknown(Command, Command.Len());
		}
		FString Name = Words[1];
		if (!Name.Contains(TEXT(":")))
		{
			Name = TEXT("minecraft:") + Name;
		}
		if (!IFileManager::Get().FileExists(*McCache::Asset(TEXT("particles/") + McCache::PathOf(Name) + TEXT(".json"))))
		{
			return BadArgument(Command);
		}
		FVector Here = SenderFeet(Sender, Target);
		FVector Pos = Here;
		if (Words.Num() >= 5 && !(ParseCoordinate(Words[2], Here.X, true, Pos.X) && ParseCoordinate(Words[3], Here.Y, false, Pos.Y) && ParseCoordinate(Words[4], Here.Z, true, Pos.Z)))
		{
			return BadArgument(Command);
		}
		FVector Delta = FVector::ZeroVector;
		double Speed = 0.0;
		int32 Count = 0;
		if (Words.Num() >= 10)
		{
			if (!(ParseCoordinate(Words[5], 0.0, false, Delta.X) && ParseCoordinate(Words[6], 0.0, false, Delta.Y) && ParseCoordinate(Words[7], 0.0, false, Delta.Z))
				|| !Words[8].IsNumeric() || !Words[9].IsNumeric() || (Words.Num() == 11 && Words[10] != TEXT("force") && Words[10] != TEXT("normal")))
			{
				return BadArgument(Command);
			}
			Speed = FCString::Atod(*Words[8]);
			Count = FCString::Atoi(*Words[9]);
		}
		if (!Target.SpawnParticle(FName(*Name), Pos, Delta, Speed, Count))
		{
			return { Fail(McLang::Get(TEXT("commands.particle.failed"))) };
		}
		return { Ok(McLang::Format(TEXT("commands.particle.success"), { Name })) };
	}

	FReplies RunExperience(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		// /xp add|set <targets> <amount> [levels|points], /xp query <targets> levels|points
		if (Words.Num() < 4 || Words.Num() > 5)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Action = Words[1];
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[2], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		if (Action == TEXT("query"))
		{
			if (Words.Num() != 4 || Players.Num() != 1 || (Words[3] != TEXT("levels") && Words[3] != TEXT("points")))
			{
				return BadArgument(Command);
			}
			int32 Level = 0, Points = 0;
			Target.GetXp(Players[0], Level, Points);
			const bool bLevels = Words[3] == TEXT("levels");
			return { Ok(McLang::Format(bLevels ? TEXT("commands.experience.query.levels") : TEXT("commands.experience.query.points"), { Players[0], FString::FromInt(bLevels ? Level : Points) })) };
		}
		const FString Unit = Words.Num() == 5 ? Words[4] : FString(TEXT("points"));
		const bool bSigned = Words[3].StartsWith(TEXT("-"));
		if ((Action != TEXT("add") && Action != TEXT("set")) || (Unit != TEXT("levels") && Unit != TEXT("points"))
			|| !Words[3].IsNumeric() || Words[3].Contains(TEXT(".")) || (Action == TEXT("set") && bSigned))
		{
			return BadArgument(Command);
		}
		const int32 Amount = FCString::Atoi(*Words[3]);
		const bool bLevels = Unit == TEXT("levels");
		for (const FString& Player : Players)
		{
			if (Action == TEXT("add"))
			{
				Target.AddXp(Player, Amount, bLevels);
			}
			else if (!Target.SetXp(Player, Amount, bLevels))
			{
				return { Fail(McLang::Get(TEXT("commands.experience.set.points.invalid"))) };
			}
		}
		const FString Key = FString::Printf(TEXT("commands.experience.%s.%s.success.%s"), *Action, *Unit, Players.Num() == 1 ? TEXT("single") : TEXT("multiple"));
		return { Ok(McLang::Format(Key, { FString::FromInt(Amount), Players.Num() == 1 ? Players[0] : FString::FromInt(Players.Num()) })) };
	}

	FReplies RunClear(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		// /clear [<targets>] [<item>] [<maxCount>]; a maxCount of 0 only counts.
		if (Words.Num() > 4)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words.Num() >= 2 ? Words[1] : FString(TEXT("@s")), Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		const FName Item = Words.Num() >= 3 ? ItemId(Words[2]) : NAME_None;
		if (!Item.IsNone() && Target.GetMaxStackSize(Item) <= 0)
		{
			return { Fail(McLang::Format(TEXT("argument.item.id.invalid"), { Item.ToString() })) };
		}
		int32 MaxCount = -1;
		if (Words.Num() == 4)
		{
			if (!Words[3].IsNumeric() || Words[3].Contains(TEXT(".")) || Words[3].StartsWith(TEXT("-")))
			{
				return BadArgument(Command);
			}
			MaxCount = FCString::Atoi(*Words[3]);
		}
		const bool bTest = MaxCount == 0;
		int32 Total = 0;
		for (const FString& Player : Players)
		{
			Total += Target.Clear(Player, Item, MaxCount, bTest);
		}
		const bool bSingle = Players.Num() == 1;
		const FString Who = bSingle ? Players[0] : FString::FromInt(Players.Num());
		if (Total == 0)
		{
			return { Fail(McLang::Format(bSingle ? TEXT("clear.failed.single") : TEXT("clear.failed.multiple"), { Who })) };
		}
		const FString Key = FString::Printf(TEXT("commands.clear.%s.%s"), bTest ? TEXT("test") : TEXT("success"), bSingle ? TEXT("single") : TEXT("multiple"));
		return { Ok(McLang::Format(Key, { FString::FromInt(Total), Who })) };
	}

	FReplies RunAdvancement(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		// /advancement (grant|revoke) <targets> only <advancement>
		if (Words.Num() != 5 || (Words[1] != TEXT("grant") && Words[1] != TEXT("revoke")) || Words[3] != TEXT("only"))
		{
			return Unknown(Command, Command.Len());
		}
		const bool bGrant = Words[1] == TEXT("grant");
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[2], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		int32 Changed = 0;
		FString Title;
		for (const FString& Player : Players)
		{
			FString Name;
			const int32 Result = Target.SetAdvancement(Player, Words[4], bGrant, Name);
			if (Result < 0)
			{
				return { Fail(McLang::Format(TEXT("advancement.advancementNotFound"), { Words[4] })) };
			}
			Changed += Result;
			Title = Name;
		}
		const bool bSingle = Players.Num() == 1;
		const FString Who = bSingle ? Players[0] : FString::FromInt(Players.Num());
		const FString Key = FString::Printf(TEXT("commands.advancement.%s.one.to.%s.%s"), bGrant ? TEXT("grant") : TEXT("revoke"), bSingle ? TEXT("one") : TEXT("many"),
			Changed > 0 ? TEXT("success") : TEXT("failure"));
		const FString Text = McLang::Format(Key, { Title, Who });
		return { Changed > 0 ? Ok(Text) : Fail(Text) };
	}

	FReplies RunRecipe(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		// /recipe (give|take) <targets> (*|<recipe>)
		if (Words.Num() != 4 || (Words[1] != TEXT("give") && Words[1] != TEXT("take")))
		{
			return Unknown(Command, Command.Len());
		}
		const bool bGrant = Words[1] == TEXT("give");
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[2], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		int32 Changed = 0;
		for (const FString& Player : Players)
		{
			FString Unknown;
			const int32 Result = Target.SetRecipes(Player, Words[3], bGrant, Unknown);
			if (Result < 0)
			{
				return { Fail(McLang::Format(TEXT("recipe.notFound"), { Unknown })) };
			}
			Changed += Result;
		}
		const bool bSingle = Players.Num() == 1;
		if (Changed == 0)
		{
			return { Fail(McLang::Get(bGrant ? TEXT("commands.recipe.give.failed") : TEXT("commands.recipe.take.failed"))) };
		}
		const FString Key = FString::Printf(TEXT("commands.recipe.%s.success.%s"), bGrant ? TEXT("give") : TEXT("take"), bSingle ? TEXT("single") : TEXT("multiple"));
		return { Ok(McLang::Format(Key, { FString::FromInt(Changed), bSingle ? Players[0] : FString::FromInt(Players.Num()) })) };
	}

	FReplies RunEffect(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		// /effect give <targets> <effect> [<seconds>|infinite] [<amplifier>] [<hideParticles>], /effect clear [<targets>] [<effect>]
		if (Words.Num() < 2 || (Words[1] != TEXT("give") && Words[1] != TEXT("clear")))
		{
			return Unknown(Command, Command.Len());
		}
		const bool bGive = Words[1] == TEXT("give");
		if (Words.Num() > (bGive ? 7 : 4) || (bGive && Words.Num() < 4))
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words.Num() >= 3 ? Words[2] : FString(TEXT("@s")), Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		FName Effect;
		if (Words.Num() >= 4)
		{
			FString Text = Words[3].ToLower();
			Text.RemoveFromStart(TEXT("minecraft:"));
			Effect = FName(*Text);
			if (!Target.IsEffect(Effect))
			{
				return BadArgument(Command);
			}
		}
		const bool bSingle = Players.Num() == 1;
		const FString Who = bSingle ? Players[0] : FString::FromInt(Players.Num());
		if (!bGive)
		{
			int32 Removed = 0;
			for (const FString& Player : Players)
			{
				Removed += Target.ClearEffects(Player, Effect);
			}
			const bool bAll = Effect.IsNone();
			if (Removed == 0)
			{
				return { Fail(McLang::Get(bAll ? TEXT("commands.effect.clear.everything.failed") : TEXT("commands.effect.clear.specific.failed"))) };
			}
			const FString Key = FString::Printf(TEXT("commands.effect.clear.%s.success.%s"), bAll ? TEXT("everything") : TEXT("specific"), bSingle ? TEXT("single") : TEXT("multiple"));
			return { Ok(bAll ? McLang::Format(Key, { Who }) : McLang::Format(Key, { McLang::Get(TEXT("effect.minecraft.") + Effect.ToString()), Who })) };
		}
		const auto IsWhole = [](const FString& Text, int32 Min, int32 Max)
		{
			return Text.IsNumeric() && !Text.Contains(TEXT(".")) && FCString::Atoi(*Text) >= Min && FCString::Atoi(*Text) <= Max;
		};
		int32 Seconds = 30;
		if (Words.Num() >= 5)
		{
			if (Words[4] == TEXT("infinite"))
			{
				Seconds = -1;
			}
			else if (!IsWhole(Words[4], 1, 1000000))
			{
				return BadArgument(Command);
			}
			else
			{
				Seconds = FCString::Atoi(*Words[4]);
			}
		}
		int32 Amplifier = 0;
		if (Words.Num() >= 6)
		{
			if (!IsWhole(Words[5], 0, 255))
			{
				return BadArgument(Command);
			}
			Amplifier = FCString::Atoi(*Words[5]);
		}
		if (Words.Num() == 7 && Words[6] != TEXT("true") && Words[6] != TEXT("false"))
		{
			return BadArgument(Command);
		}
		const int32 Ticks = Seconds < 0 ? 1000000000 : Seconds * 20;
		int32 Applied = 0;
		for (const FString& Player : Players)
		{
			Applied += Target.GiveEffect(Player, Effect, Amplifier, Ticks) ? 1 : 0;
		}
		if (Applied == 0)
		{
			return { Fail(McLang::Get(TEXT("commands.effect.give.failed"))) };
		}
		return { Ok(McLang::Format(bSingle ? TEXT("commands.effect.give.success.single") : TEXT("commands.effect.give.success.multiple"),
			{ McLang::Get(TEXT("effect.minecraft.") + Effect.ToString()), Who })) };
	}

	FReplies RunEnchant(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		// /enchant <targets> <enchantment> [<level>]
		if (Words.Num() < 3 || Words.Num() > 4)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		const FName Enchantment = ItemId(Words[2]);
		const int32 MaxLevel = Target.GetEnchantmentMaxLevel(Enchantment);
		if (MaxLevel <= 0)
		{
			return BadArgument(Command);
		}
		int32 Level = 1;
		if (Words.Num() == 4)
		{
			if (!Words[3].IsNumeric() || Words[3].Contains(TEXT(".")) || FCString::Atoi(*Words[3]) < 1)
			{
				return BadArgument(Command);
			}
			Level = FCString::Atoi(*Words[3]);
		}
		if (Level > MaxLevel)
		{
			return { Fail(McLang::Format(TEXT("commands.enchant.failed.level"), { FString::FromInt(Level), FString::FromInt(MaxLevel) })) };
		}
		int32 Done = 0;
		for (const FString& Player : Players)
		{
			switch (Target.Enchant(Player, Enchantment, Level))
			{
			case IMcCommandTarget::EEnchantResult::Done:
				++Done;
				break;
			case IMcCommandTarget::EEnchantResult::NoItem:
				if (Players.Num() == 1)
				{
					return { Fail(McLang::Format(TEXT("commands.enchant.failed.itemless"), { Player })) };
				}
				break;
			default:
				if (Players.Num() == 1)
				{
					return { Fail(McLang::Format(TEXT("commands.enchant.failed.incompatible"), { Player })) };
				}
				break;
			}
		}
		if (Done == 0)
		{
			return { Fail(McLang::Get(TEXT("commands.enchant.failed"))) };
		}
		FString Shown = McLang::Get(TEXT("enchantment.") + Enchantment.ToString().Replace(TEXT(":"), TEXT(".")));
		if (Level != 1 || MaxLevel != 1)
		{
			Shown += TEXT(" ") + McLang::Get(FString::Printf(TEXT("enchantment.level.%d"), Level));
		}
		return { Ok(Players.Num() == 1 ? McLang::Format(TEXT("commands.enchant.success.single"), { Shown, Players[0] })
			: McLang::Format(TEXT("commands.enchant.success.multiple"), { Shown, FString::FromInt(Done) })) };
	}

	// ---- Commands the game answers without needing more of the engine ----

	// "min..max" as /random takes it.
	bool ParseIntRange(const FString& Text, int64& OutMin, int64& OutMax)
	{
		FString Left, Right;
		if (!Text.Split(TEXT(".."), &Left, &Right) || Left.IsEmpty() || Right.IsEmpty()
			|| !Left.IsNumeric() || !Right.IsNumeric() || Left.Contains(TEXT(".")) || Right.Contains(TEXT(".")))
		{
			return false;
		}
		OutMin = FCString::Atoi64(*Left);
		OutMax = FCString::Atoi64(*Right);
		return true;
	}

	FReplies RunRandom(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender)
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		if (Words[1] == TEXT("reset"))
		{
			if (Words.Num() < 3 || Words.Num() > 5)
			{
				return Unknown(Command, Command.Len());
			}
			// The game keeps one sequence per loot table and the like; 197 of them in a fresh world.
			if (Words[2] == TEXT("*"))
			{
				return { Ok(McLang::Format(TEXT("commands.random.reset.all.success"), { TEXT("197") })) };
			}
			return { Ok(McLang::Format(TEXT("commands.random.reset.success"), { Words[2] })) };
		}
		const bool bRoll = Words[1] == TEXT("roll");
		if (!bRoll && Words[1] != TEXT("value"))
		{
			return BadArgument(Command);
		}
		if (Words.Num() < 3)
		{
			return Unknown(Command, Command.Len());
		}
		int64 Min, Max;
		if (!ParseIntRange(Words[2], Min, Max))
		{
			return BadArgument(Command);
		}
		if (Min >= Max)
		{
			return { Fail(McLang::Get(TEXT("commands.random.error.range_too_small"))) };
		}
		if (Max - Min + 1 > 2147483647)
		{
			return { Fail(McLang::Get(TEXT("commands.random.error.range_too_large"))) };
		}
		const int64 Value = Min + FMath::Min<int64>(Max - Min, static_cast<int64>(FMath::FRand() * static_cast<double>(Max - Min + 1)));
		if (bRoll)
		{
			return { Ok(McLang::Format(TEXT("commands.random.roll"), { SenderName(Sender), LexToString(Value), LexToString(Min), LexToString(Max) })) };
		}
		return { Ok(McLang::Format(TEXT("commands.random.sample.success"), { LexToString(Value) })) };
	}

	FReplies RunDefaultGameMode(const FString& Command, const TArray<FString>& Words, IMcCommandTarget& Target)
	{
		if (Words.Num() != 2)
		{
			return Unknown(Command, Command.Len());
		}
		if (Words[1] != TEXT("creative") && Words[1] != TEXT("survival") && Words[1] != TEXT("adventure") && Words[1] != TEXT("spectator"))
		{
			return BadArgument(Command);
		}
		Target.SetDefaultGameMode(Words[1]);
		return { Ok(McLang::Format(TEXT("commands.defaultgamemode.success"), { McLang::Get(TEXT("gameMode.") + Words[1]) })) };
	}

	// Tags on players, kept for the session.
	TMap<FString, TArray<FString>>& PlayerTags()
	{
		static TMap<FString, TArray<FString>> Tags;
		return Tags;
	}

	// /team: teams and who is on them, kept for the session (wording from a ref run, cmd_team.txt). The game's team effects on play (colours of
	// names, friendly fire, nametags, collision) are not drawn or applied.
	struct FTeam
	{
		FString Name;
		FString Display;
		FString Color;
		FString Prefix;
		FString Suffix;
		bool bFriendlyFire = true;
		bool bSeeInvisible = true;
		FString Nametag = TEXT("always");
		FString DeathMessage = TEXT("always");
		FString Collision = TEXT("always");
		TArray<FString> Members;
	};

	TMap<FString, FTeam>& Teams()
	{
		static TMap<FString, FTeam> All;
		return All;
	}

	FReplies RunTeam(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Verb = Words[1];
		const auto Shown = [](const FTeam& Team) { return TEXT("[") + (Team.Display.IsEmpty() ? Team.Name : Team.Display) + TEXT("]"); };
		const auto Joined = [&](int32 From)
		{
			TArray<FString> Rest;
			for (int32 I = From; I < Words.Num(); ++I)
			{
				Rest.Add(Words[I]);
			}
			return FString::Join(Rest, TEXT(" "));
		};
		const auto Find = [&](const FString& Name) -> FTeam* { return Teams().Find(Name); };
		const auto NotFound = [&](const FString& Name) -> FReplies { return { Fail(McLang::Format(TEXT("team.notFound"), { Name })) }; };

		if (Verb == TEXT("list"))
		{
			if (Words.Num() == 2)
			{
				if (Teams().Num() == 0)
				{
					return { Ok(McLang::Get(TEXT("commands.team.list.teams.empty"))) };
				}
				TArray<FString> Keys;
				Teams().GetKeys(Keys);
				Keys.Sort();
				TArray<FString> Names;
				for (const FString& Key : Keys)
				{
					Names.Add(Shown(Teams()[Key]));
				}
				return { Ok(McLang::Format(TEXT("commands.team.list.teams.success"), { FString::FromInt(Names.Num()), FString::Join(Names, TEXT(", ")) })) };
			}
			FTeam* Team = Find(Words[2]);
			if (!Team)
			{
				return NotFound(Words[2]);
			}
			if (Team->Members.Num() == 0)
			{
				return { Ok(McLang::Format(TEXT("commands.team.list.members.empty"), { Shown(*Team) })) };
			}
			return { Ok(McLang::Format(TEXT("commands.team.list.members.success"), { Shown(*Team), FString::FromInt(Team->Members.Num()), FString::Join(Team->Members, TEXT(", ")) })) };
		}
		if (Verb == TEXT("add"))
		{
			if (Words.Num() < 3)
			{
				return Unknown(Command, Command.Len());
			}
			if (Find(Words[2]))
			{
				return { Fail(McLang::Get(TEXT("commands.team.add.duplicate"))) };
			}
			FTeam Team;
			Team.Name = Words[2];
			Team.Display = Words.Num() > 3 ? PlainText(Joined(3)) : FString();
			Teams().Add(Team.Name, Team);
			return { Ok(McLang::Format(TEXT("commands.team.add.success"), { Shown(Team) })) };
		}
		if (Verb == TEXT("remove") || Verb == TEXT("empty"))
		{
			if (Words.Num() != 3)
			{
				return Words.Num() < 3 ? Unknown(Command, Command.Len()) : BadArgument(Command);
			}
			FTeam* Team = Find(Words[2]);
			if (!Team)
			{
				return NotFound(Words[2]);
			}
			const FString Name = Shown(*Team);
			if (Verb == TEXT("remove"))
			{
				Teams().Remove(Words[2]);
				return { Ok(McLang::Format(TEXT("commands.team.remove.success"), { Name })) };
			}
			if (Team->Members.Num() == 0)
			{
				return { Fail(McLang::Get(TEXT("commands.team.empty.unchanged"))) };
			}
			const int32 Count = Team->Members.Num();
			Team->Members.Reset();
			return { Ok(McLang::Format(TEXT("commands.team.empty.success"), { FString::FromInt(Count), Name })) };
		}

		// Members are player selectors or plain names.
		const auto Members = [&](const FString& Word, TArray<FString>& Out) -> bool
		{
			FString Error;
			if (Word.StartsWith(TEXT("@")))
			{
				return ResolvePlayers(Word, Sender, Target, Out, Error);
			}
			Out.Add(Word);
			return true;
		};
		const auto LeaveAll = [&](const FString& Member)
		{
			bool bWas = false;
			for (TPair<FString, FTeam>& Pair : Teams())
			{
				bWas = Pair.Value.Members.Remove(Member) > 0 || bWas;
			}
			return bWas;
		};

		if (Verb == TEXT("join"))
		{
			if (Words.Num() < 3 || Words.Num() > 4)
			{
				return Words.Num() < 3 ? Unknown(Command, Command.Len()) : BadArgument(Command);
			}
			FTeam* Team = Find(Words[2]);
			if (!Team)
			{
				return NotFound(Words[2]);
			}
			TArray<FString> Who;
			if (!Members(Words.Num() == 4 ? Words[3] : TEXT("@s"), Who))
			{
				return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
			}
			for (const FString& Member : Who)
			{
				LeaveAll(Member);
				Team->Members.Add(Member);
			}
			if (Who.Num() == 1)
			{
				return { Ok(McLang::Format(TEXT("commands.team.join.success.single"), { Team->Prefix + Who[0] + Team->Suffix, Shown(*Team) })) };
			}
			return { Ok(McLang::Format(TEXT("commands.team.join.success.multiple"), { FString::FromInt(Who.Num()), Shown(*Team) })) };
		}
		if (Verb == TEXT("leave"))
		{
			if (Words.Num() != 3)
			{
				return Words.Num() < 3 ? Unknown(Command, Command.Len()) : BadArgument(Command);
			}
			TArray<FString> Who;
			if (!Members(Words[2], Who))
			{
				return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
			}
			TArray<FString> Left;
			for (const FString& Member : Who)
			{
				if (LeaveAll(Member))
				{
					Left.Add(Member);
				}
			}
			if (Left.Num() == 1)
			{
				return { Ok(McLang::Format(TEXT("commands.team.leave.success.single"), { Left[0] })) };
			}
			return { Ok(McLang::Format(TEXT("commands.team.leave.success.multiple"), { FString::FromInt(Left.Num()) })) };
		}
		if (Verb == TEXT("modify"))
		{
			if (Words.Num() < 5)
			{
				return Unknown(Command, Command.Len());
			}
			FTeam* Team = Find(Words[2]);
			if (!Team)
			{
				return NotFound(Words[2]);
			}
			const FString& Option = Words[3];
			const FString& Value = Words[4];
			const auto Flag = [&](bool& bState, const TCHAR* Key) -> FReplies
			{
				if (Value != TEXT("true") && Value != TEXT("false"))
				{
					return BadArgument(Command);
				}
				const bool bNew = Value == TEXT("true");
				const FString Base = FString(TEXT("commands.team.option.")) + Key;
				if (bNew == bState)
				{
					return { Fail(McLang::Get(Base + (bNew ? TEXT(".alreadyEnabled") : TEXT(".alreadyDisabled")))) };
				}
				bState = bNew;
				return { Ok(McLang::Format(Base + (bNew ? TEXT(".enabled") : TEXT(".disabled")), { Shown(*Team) })) };
			};
			const auto Choice = [&](FString& State, const TCHAR* Key, const TCHAR* LangGroup, std::initializer_list<const TCHAR*> Allowed) -> FReplies
			{
				bool bKnown = false;
				for (const TCHAR* One : Allowed)
				{
					bKnown = bKnown || Value == One;
				}
				if (!bKnown)
				{
					return BadArgument(Command);
				}
				const FString Base = FString(TEXT("commands.team.option.")) + Key;
				if (State == Value)
				{
					return { Fail(McLang::Get(Base + TEXT(".unchanged"))) };
				}
				State = Value;
				return { Ok(McLang::Format(Base + TEXT(".success"), { Shown(*Team), McLang::Get(FString(LangGroup) + Value) })) };
			};
			if (Option == TEXT("color"))
			{
				static const TCHAR* Colors[] = { TEXT("black"), TEXT("dark_blue"), TEXT("dark_green"), TEXT("dark_aqua"), TEXT("dark_red"), TEXT("dark_purple"), TEXT("gold"), TEXT("gray"), TEXT("dark_gray"), TEXT("blue"), TEXT("green"), TEXT("aqua"), TEXT("red"), TEXT("light_purple"), TEXT("yellow"), TEXT("white"), TEXT("reset") };
				bool bKnown = false;
				for (const TCHAR* One : Colors)
				{
					bKnown = bKnown || Value == One;
				}
				if (!bKnown)
				{
					return { Fail(McLang::Format(TEXT("argument.color.invalid"), { Value })), Fail(BorderContext(Command, Command.Len() - Value.Len())) };
				}
				const FString New = Value == TEXT("reset") ? FString() : Value;
				if (New == Team->Color)
				{
					return { Fail(McLang::Get(TEXT("commands.team.option.color.unchanged"))) };
				}
				Team->Color = New;
				if (New.IsEmpty())
				{
					return { Ok(McLang::Format(TEXT("commands.team.option.color.clear.success"), { Shown(*Team) })) };
				}
				return { Ok(McLang::Format(TEXT("commands.team.option.color.success"), { Shown(*Team), New })) };
			}
			if (Option == TEXT("friendlyFire"))
			{
				return Flag(Team->bFriendlyFire, TEXT("friendlyfire"));
			}
			if (Option == TEXT("seeFriendlyInvisibles"))
			{
				return Flag(Team->bSeeInvisible, TEXT("seeFriendlyInvisibles"));
			}
			if (Option == TEXT("nametagVisibility"))
			{
				return Choice(Team->Nametag, TEXT("nametagVisibility"), TEXT("team.visibility."), { TEXT("always"), TEXT("never"), TEXT("hideForOtherTeams"), TEXT("hideForOwnTeam") });
			}
			if (Option == TEXT("deathMessageVisibility"))
			{
				return Choice(Team->DeathMessage, TEXT("deathMessageVisibility"), TEXT("team.visibility."), { TEXT("always"), TEXT("never"), TEXT("hideForOtherTeams"), TEXT("hideForOwnTeam") });
			}
			if (Option == TEXT("collisionRule"))
			{
				return Choice(Team->Collision, TEXT("collisionRule"), TEXT("team.collision."), { TEXT("always"), TEXT("never"), TEXT("pushOtherTeams"), TEXT("pushOwnTeam") });
			}
			if (Option == TEXT("displayName"))
			{
				const FString New = PlainText(Joined(4));
				if (New == (Team->Display.IsEmpty() ? Team->Name : Team->Display))
				{
					return { Fail(McLang::Get(TEXT("commands.team.option.name.unchanged"))) };
				}
				Team->Display = New;
				return { Ok(McLang::Format(TEXT("commands.team.option.name.success"), { Shown(*Team) })) };
			}
			if (Option == TEXT("prefix") || Option == TEXT("suffix"))
			{
				const FString New = PlainText(Joined(4));
				(Option == TEXT("prefix") ? Team->Prefix : Team->Suffix) = New;
				return { Ok(McLang::Format(Option == TEXT("prefix") ? TEXT("commands.team.option.prefix.success") : TEXT("commands.team.option.suffix.success"), { New })) };
			}
			return BadArgument(Command);
		}
		return BadArgument(Command);
	}

	// /bossbar: custom bars kept for the session (wording from a ref run, cmd_boss.txt). Nothing draws a bar on the screen yet.
	struct FBossbar
	{
		FString Id;
		FString Name;
		int32 Value = 0;
		int32 Max = 100;
		bool bVisible = true;
		FString Color = TEXT("white");
		FString Style = TEXT("progress");
		TArray<FString> Players;
	};

	TMap<FString, FBossbar>& Bossbars()
	{
		static TMap<FString, FBossbar> All;
		return All;
	}

	FReplies RunBossbar(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Verb = Words[1];
		const auto Qualified = [](const FString& Id) { return Id.Contains(TEXT(":")) ? Id : TEXT("minecraft:") + Id; };
		const auto Shown = [](const FBossbar& Bar) { return TEXT("[") + Bar.Name + TEXT("]"); };
		const auto NotFound = [&](const FString& Id) -> FReplies { return { Fail(McLang::Format(TEXT("commands.bossbar.unknown"), { Qualified(Id) })) }; };

		if (Verb == TEXT("list"))
		{
			if (Bossbars().Num() == 0)
			{
				return { Ok(McLang::Get(TEXT("commands.bossbar.list.bars.none"))) };
			}
			TArray<FString> Keys;
			Bossbars().GetKeys(Keys);
			Keys.Sort();
			TArray<FString> Names;
			for (const FString& Key : Keys)
			{
				Names.Add(Shown(Bossbars()[Key]));
			}
			return { Ok(McLang::Format(TEXT("commands.bossbar.list.bars.some"), { FString::FromInt(Names.Num()), FString::Join(Names, TEXT(", ")) })) };
		}
		if (Verb == TEXT("add"))
		{
			if (Words.Num() < 4)
			{
				return Unknown(Command, Command.Len());
			}
			const FString Id = Qualified(Words[2]);
			if (Bossbars().Contains(Id))
			{
				return { Fail(McLang::Format(TEXT("commands.bossbar.create.failed"), { Id })) };
			}
			FBossbar Bar;
			Bar.Id = Id;
			TArray<FString> Rest;
			for (int32 I = 3; I < Words.Num(); ++I)
			{
				Rest.Add(Words[I]);
			}
			Bar.Name = PlainText(FString::Join(Rest, TEXT(" ")));
			Bossbars().Add(Id, Bar);
			return { Ok(McLang::Format(TEXT("commands.bossbar.create.success"), { Shown(Bar) })) };
		}
		if (Verb != TEXT("get") && Verb != TEXT("set") && Verb != TEXT("remove"))
		{
			return BadArgument(Command);
		}
		if (Words.Num() < 3)
		{
			return Unknown(Command, Command.Len());
		}
		FBossbar* Bar = Bossbars().Find(Qualified(Words[2]));
		if (!Bar)
		{
			return NotFound(Words[2]);
		}
		if (Verb == TEXT("remove"))
		{
			const FString Name = Shown(*Bar);
			Bossbars().Remove(Qualified(Words[2]));
			return { Ok(McLang::Format(TEXT("commands.bossbar.remove.success"), { Name })) };
		}
		if (Words.Num() < 4)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& What = Words[3];
		if (Verb == TEXT("get"))
		{
			if (What == TEXT("value"))
			{
				return { Ok(McLang::Format(TEXT("commands.bossbar.get.value"), { Shown(*Bar), FString::FromInt(Bar->Value) })) };
			}
			if (What == TEXT("max"))
			{
				return { Ok(McLang::Format(TEXT("commands.bossbar.get.max"), { Shown(*Bar), FString::FromInt(Bar->Max) })) };
			}
			if (What == TEXT("visible"))
			{
				return { Ok(McLang::Format(Bar->bVisible ? TEXT("commands.bossbar.get.visible.visible") : TEXT("commands.bossbar.get.visible.hidden"), { Shown(*Bar) })) };
			}
			if (What == TEXT("players"))
			{
				if (Bar->Players.Num() == 0)
				{
					return { Ok(McLang::Format(TEXT("commands.bossbar.get.players.none"), { Shown(*Bar) })) };
				}
				return { Ok(McLang::Format(TEXT("commands.bossbar.get.players.some"), { Shown(*Bar), FString::FromInt(Bar->Players.Num()), FString::Join(Bar->Players, TEXT(", ")) })) };
			}
			return BadArgument(Command);
		}

		// set
		const FString Last = Words.Last();
		const auto Whole = [&](const TCHAR* Key, int32& Field, int32 Minimum) -> FReplies
		{
			double Number = 0.0;
			if (Words.Num() != 5 || !ParseNumber(Last, Number) || Number != FMath::FloorToDouble(Number))
			{
				return Words.Num() < 5 ? Unknown(Command, Command.Len()) : BadArgument(Command);
			}
			if (Number < Minimum)
			{
				return { Fail(FString::Printf(TEXT("Integer must not be less than %d: found %d"), Minimum, FMath::RoundToInt32(Number))), Fail(BorderContext(Command, Command.Len() - Last.Len())) };
			}
			const FString Base = FString(TEXT("commands.bossbar.set.")) + Key;
			if (FMath::RoundToInt32(Number) == Field)
			{
				return { Fail(McLang::Get(Base + TEXT(".unchanged"))) };
			}
			Field = FMath::RoundToInt32(Number);
			return { Ok(McLang::Format(Base + TEXT(".success"), { Shown(*Bar), FString::FromInt(Field) })) };
		};
		const auto Choice = [&](FString& Field, const TCHAR* Key, std::initializer_list<const TCHAR*> Allowed) -> FReplies
		{
			bool bKnown = false;
			for (const TCHAR* One : Allowed)
			{
				bKnown = bKnown || Last == One;
			}
			if (Words.Num() != 5 || !bKnown)
			{
				return Words.Num() < 5 ? Unknown(Command, Command.Len()) : BadArgument(Command);
			}
			const FString Base = FString(TEXT("commands.bossbar.set.")) + Key;
			if (Field == Last)
			{
				return { Fail(McLang::Get(Base + TEXT(".unchanged"))) };
			}
			Field = Last;
			return { Ok(McLang::Format(Base + TEXT(".success"), { Shown(*Bar) })) };
		};
		if (What == TEXT("value"))
		{
			return Whole(TEXT("value"), Bar->Value, 0);
		}
		if (What == TEXT("max"))
		{
			return Whole(TEXT("max"), Bar->Max, 1);
		}
		if (What == TEXT("color"))
		{
			return Choice(Bar->Color, TEXT("color"), { TEXT("pink"), TEXT("blue"), TEXT("red"), TEXT("green"), TEXT("yellow"), TEXT("purple"), TEXT("white") });
		}
		if (What == TEXT("style"))
		{
			return Choice(Bar->Style, TEXT("style"), { TEXT("progress"), TEXT("notched_6"), TEXT("notched_10"), TEXT("notched_12"), TEXT("notched_20") });
		}
		if (What == TEXT("visible"))
		{
			if (Words.Num() != 5 || (Last != TEXT("true") && Last != TEXT("false")))
			{
				return Words.Num() < 5 ? Unknown(Command, Command.Len()) : BadArgument(Command);
			}
			const bool bNew = Last == TEXT("true");
			if (bNew == Bar->bVisible)
			{
				return { Fail(McLang::Get(bNew ? TEXT("commands.bossbar.set.visibility.unchanged.visible") : TEXT("commands.bossbar.set.visibility.unchanged.hidden"))) };
			}
			Bar->bVisible = bNew;
			return { Ok(McLang::Format(bNew ? TEXT("commands.bossbar.set.visible.success.visible") : TEXT("commands.bossbar.set.visible.success.hidden"), { Shown(*Bar) })) };
		}
		if (What == TEXT("name"))
		{
			if (Words.Num() < 5)
			{
				return Unknown(Command, Command.Len());
			}
			TArray<FString> Rest;
			for (int32 I = 4; I < Words.Num(); ++I)
			{
				Rest.Add(Words[I]);
			}
			const FString New = PlainText(FString::Join(Rest, TEXT(" ")));
			if (New == Bar->Name)
			{
				return { Fail(McLang::Get(TEXT("commands.bossbar.set.name.unchanged"))) };
			}
			Bar->Name = New;
			return { Ok(McLang::Format(TEXT("commands.bossbar.set.name.success"), { Shown(*Bar) })) };
		}
		if (What == TEXT("players"))
		{
			TArray<FString> Who;
			if (Words.Num() == 5)
			{
				FString Error;
				if (!ResolvePlayers(Last, Sender, Target, Who, Error))
				{
					Who.Reset();
				}
			}
			else if (Words.Num() != 4)
			{
				return BadArgument(Command);
			}
			Who.Sort();
			TArray<FString> Old = Bar->Players;
			Old.Sort();
			if (Old == Who)
			{
				return { Fail(McLang::Get(TEXT("commands.bossbar.set.players.unchanged"))) };
			}
			Bar->Players = Who;
			if (Who.Num() == 0)
			{
				return { Ok(McLang::Format(TEXT("commands.bossbar.set.players.success.none"), { Shown(*Bar) })) };
			}
			return { Ok(McLang::Format(TEXT("commands.bossbar.set.players.success.some"), { Shown(*Bar), FString::FromInt(Who.Num()), FString::Join(Who, TEXT(", ")) })) };
		}
		return BadArgument(Command);
	}

	// /data: a small NBT value (just the kinds SNBT can write), its text form, and paths into it. Storage is kept for the session.
	struct FNbt
	{
		enum class EType : uint8 { Byte, Short, Int, Long, Float, Double, String, List, Compound, ByteArray, IntArray, LongArray };
		EType Type = EType::Compound;
		double Real = 0.0;
		int64 Whole = 0;
		FString Text;
		TArray<FNbt> Items;
		TArray<FString> Keys;

		bool IsNumber() const { return Type <= EType::Double; }
		bool IsReal() const { return Type == EType::Float || Type == EType::Double; }
		double AsDouble() const { return IsReal() ? Real : double(Whole); }
		int32 Find(const FString& Key) const { return Keys.IndexOfByKey(Key); }
		bool operator==(const FNbt& Other) const
		{
			if (Type != Other.Type || Text != Other.Text || Items.Num() != Other.Items.Num() || Keys.Num() != Other.Keys.Num())
			{
				return false;
			}
			if (IsReal() ? Real != Other.Real : Whole != Other.Whole)
			{
				return false;
			}
			for (int32 I = 0; I < Keys.Num(); ++I)
			{
				const int32 At = Other.Find(Keys[I]);
				if (At == INDEX_NONE || !(Items[I] == Other.Items[At]))
				{
					return false;
				}
			}
			if (Type != EType::Compound)
			{
				for (int32 I = 0; I < Items.Num(); ++I)
				{
					if (!(Items[I] == Other.Items[I]))
					{
						return false;
					}
				}
			}
			return true;
		}
	};

	FString ShortestReal(double Value, bool bFloat)
	{
		FString Best;
		for (int32 Digits = 1; Digits <= 17; ++Digits)
		{
			Best = FString::Printf(TEXT("%.*g"), Digits, Value);
			const double Back = FCString::Atod(*Best);
			if (bFloat ? float(Back) == float(Value) : Back == Value)
			{
				break;
			}
		}
		if (Best.Contains(TEXT("e")))
		{
			Best = FString::Printf(TEXT("%.1f"), Value);
		}
		if (!Best.Contains(TEXT(".")))
		{
			Best += TEXT(".0");
		}
		return Best;
	}

	bool PlainKey(const FString& Key)
	{
		for (TCHAR C : Key)
		{
			if (!(FChar::IsAlnum(C) || C == '_' || C == '-' || C == '.' || C == '+'))
			{
				return false;
			}
		}
		return !Key.IsEmpty();
	}

	FString QuoteNbt(const FString& Text)
	{
		const TCHAR Quote = Text.Contains(TEXT("\"")) && !Text.Contains(TEXT("'")) ? '\'' : '"';
		FString Out;
		Out.AppendChar(Quote);
		for (TCHAR C : Text)
		{
			if (C == Quote || C == '\\')
			{
				Out.AppendChar('\\');
			}
			Out.AppendChar(C);
		}
		Out.AppendChar(Quote);
		return Out;
	}

	FString PrintNbt(const FNbt& Value)
	{
		using E = FNbt::EType;
		switch (Value.Type)
		{
		case E::Byte: return FString::Printf(TEXT("%lldb"), Value.Whole);
		case E::Short: return FString::Printf(TEXT("%llds"), Value.Whole);
		case E::Int: return FString::Printf(TEXT("%lld"), Value.Whole);
		case E::Long: return FString::Printf(TEXT("%lldL"), Value.Whole);
		case E::Float: return ShortestReal(Value.Real, true) + TEXT("f");
		case E::Double: return ShortestReal(Value.Real, false) + TEXT("d");
		case E::String: return QuoteNbt(Value.Text);
		default: break;
		}
		TArray<FString> Parts;
		if (Value.Type == E::Compound)
		{
			TArray<FString> Sorted = Value.Keys;
			Sorted.Sort();
			for (const FString& Key : Sorted)
			{
				Parts.Add((PlainKey(Key) ? Key : QuoteNbt(Key)) + TEXT(": ") + PrintNbt(Value.Items[Value.Find(Key)]));
			}
			return TEXT("{") + FString::Join(Parts, TEXT(", ")) + TEXT("}");
		}
		for (const FNbt& Item : Value.Items)
		{
			Parts.Add(PrintNbt(Item));
		}
		if (Value.Type == E::List)
		{
			return TEXT("[") + FString::Join(Parts, TEXT(", ")) + TEXT("]");
		}
		const TCHAR* Tag = Value.Type == E::ByteArray ? TEXT("B") : Value.Type == E::IntArray ? TEXT("I") : TEXT("L");
		return FString(TEXT("[")) + Tag + TEXT(";") + (Parts.Num() ? TEXT(" ") : TEXT("")) + FString::Join(Parts, TEXT(", ")) + TEXT("]");
	}

	struct FNbtParser
	{
		const FString& Text;
		int32 Pos = 0;
		explicit FNbtParser(const FString& InText) : Text(InText) {}

		TCHAR Peek() const { return Pos < Text.Len() ? Text[Pos] : TCHAR(0); }
		void Skip() { while (Pos < Text.Len() && FChar::IsWhitespace(Text[Pos])) { ++Pos; } }

		bool Word(FString& Out)
		{
			const int32 Start = Pos;
			while (Pos < Text.Len() && (FChar::IsAlnum(Text[Pos]) || Text[Pos] == '_' || Text[Pos] == '-' || Text[Pos] == '.' || Text[Pos] == '+'))
			{
				++Pos;
			}
			Out = Text.Mid(Start, Pos - Start);
			return Pos > Start;
		}

		bool Quoted(FString& Out)
		{
			const TCHAR Quote = Text[Pos++];
			Out.Reset();
			while (Pos < Text.Len() && Text[Pos] != Quote)
			{
				if (Text[Pos] == '\\' && Pos + 1 < Text.Len())
				{
					++Pos;
				}
				Out.AppendChar(Text[Pos++]);
			}
			if (Pos >= Text.Len())
			{
				return false;
			}
			++Pos;
			return true;
		}

		static void FromWord(const FString& Word, FNbt& Out)
		{
			using E = FNbt::EType;
			Out = FNbt();
			if (Word == TEXT("true") || Word == TEXT("false"))
			{
				Out.Type = E::Byte;
				Out.Whole = Word == TEXT("true");
				return;
			}
			FString Digits = Word;
			const TCHAR Last = Word.Len() ? FChar::ToLower(Word[Word.Len() - 1]) : TCHAR(0);
			E Kind = E::Int;
			bool bSuffix = true;
			if (Last == 'b') { Kind = E::Byte; }
			else if (Last == 's') { Kind = E::Short; }
			else if (Last == 'l') { Kind = E::Long; }
			else if (Last == 'f') { Kind = E::Float; }
			else if (Last == 'd') { Kind = E::Double; }
			else { bSuffix = false; }
			if (bSuffix)
			{
				Digits = Word.Left(Word.Len() - 1);
			}
			const bool bDecimal = Digits.Contains(TEXT("."));
			const bool bNumeric = !Digits.IsEmpty() && (Digits.IsNumeric() || (Digits[0] == '-' && Digits.Mid(1).IsNumeric()) || (Digits[0] == '+' && Digits.Mid(1).IsNumeric()));
			if (!bNumeric || (bDecimal && Kind != E::Float && Kind != E::Double && bSuffix) || (bDecimal && !bSuffix && false))
			{
				Out.Type = E::String;
				Out.Text = Word;
				return;
			}
			if (!bSuffix)
			{
				Kind = bDecimal ? E::Double : E::Int;
			}
			Out.Type = Kind;
			if (Kind == E::Float || Kind == E::Double)
			{
				Out.Real = FCString::Atod(*Digits);
				if (Kind == E::Float)
				{
					Out.Real = double(float(Out.Real));
				}
			}
			else
			{
				Out.Whole = FCString::Atoi64(*Digits);
			}
		}

		bool Value(FNbt& Out)
		{
			using E = FNbt::EType;
			Skip();
			const TCHAR C = Peek();
			Out = FNbt();
			if (C == '{')
			{
				++Pos;
				Out.Type = E::Compound;
				Skip();
				if (Peek() == '}')
				{
					++Pos;
					return true;
				}
				while (true)
				{
					Skip();
					FString Key;
					if (Peek() == '"' || Peek() == '\'')
					{
						if (!Quoted(Key))
						{
							return false;
						}
					}
					else if (!Word(Key))
					{
						return false;
					}
					Skip();
					if (Peek() != ':')
					{
						return false;
					}
					++Pos;
					FNbt Item;
					if (!Value(Item))
					{
						return false;
					}
					const int32 At = Out.Find(Key);
					if (At == INDEX_NONE)
					{
						Out.Keys.Add(Key);
						Out.Items.Add(Item);
					}
					else
					{
						Out.Items[At] = Item;
					}
					Skip();
					if (Peek() == ',')
					{
						++Pos;
						continue;
					}
					if (Peek() == '}')
					{
						++Pos;
						return true;
					}
					return false;
				}
			}
			if (C == '[')
			{
				++Pos;
				Out.Type = E::List;
				Skip();
				if (Pos + 1 < Text.Len() && Text[Pos + 1] == ';' && (Text[Pos] == 'B' || Text[Pos] == 'I' || Text[Pos] == 'L'))
				{
					Out.Type = Text[Pos] == 'B' ? E::ByteArray : Text[Pos] == 'I' ? E::IntArray : E::LongArray;
					Pos += 2;
				}
				Skip();
				if (Peek() == ']')
				{
					++Pos;
					return true;
				}
				while (true)
				{
					FNbt Item;
					if (!Value(Item))
					{
						return false;
					}
					Out.Items.Add(Item);
					Skip();
					if (Peek() == ',')
					{
						++Pos;
						continue;
					}
					if (Peek() == ']')
					{
						++Pos;
						return true;
					}
					return false;
				}
			}
			if (C == '"' || C == '\'')
			{
				Out.Type = E::String;
				return Quoted(Out.Text);
			}
			FString Word;
			if (!this->Word(Word))
			{
				return false;
			}
			FromWord(Word, Out);
			return true;
		}
	};

	struct FNbtStep
	{
		FString Key;
		int32 Index = 0;
		bool bIndex = false;
	};

	bool ParseNbtPath(const FString& Path, TArray<FNbtStep>& Out)
	{
		int32 I = 0;
		while (I < Path.Len())
		{
			FNbtStep Step;
			if (Path[I] == '[')
			{
				const int32 Close = Path.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
				const FString Number = Close == INDEX_NONE ? FString() : Path.Mid(I + 1, Close - I - 1);
				if (Number.IsEmpty() || !(Number.IsNumeric() || (Number[0] == '-' && Number.Mid(1).IsNumeric())))
				{
					return false;
				}
				Step.bIndex = true;
				Step.Index = FCString::Atoi(*Number);
				I = Close + 1;
			}
			else
			{
				if (Path[I] == '.')
				{
					++I;
				}
				const int32 Start = I;
				while (I < Path.Len() && Path[I] != '.' && Path[I] != '[')
				{
					++I;
				}
				Step.Key = Path.Mid(Start, I - Start);
				if (Step.Key.IsEmpty())
				{
					return false;
				}
			}
			Out.Add(Step);
		}
		return true;
	}

	FNbt* NbtAt(FNbt& Root, const TArray<FNbtStep>& Steps, bool bCreate)
	{
		FNbt* Here = &Root;
		for (const FNbtStep& Step : Steps)
		{
			if (Step.bIndex)
			{
				if (Here->Type != FNbt::EType::List && Here->Type < FNbt::EType::ByteArray)
				{
					return nullptr;
				}
				const int32 At = Step.Index < 0 ? Here->Items.Num() + Step.Index : Step.Index;
				if (!Here->Items.IsValidIndex(At))
				{
					return nullptr;
				}
				Here = &Here->Items[At];
			}
			else
			{
				if (Here->Type != FNbt::EType::Compound)
				{
					return nullptr;
				}
				int32 At = Here->Find(Step.Key);
				if (At == INDEX_NONE)
				{
					if (!bCreate)
					{
						return nullptr;
					}
					Here->Keys.Add(Step.Key);
					Here->Items.Add(FNbt());
					At = Here->Items.Num() - 1;
				}
				Here = &Here->Items[At];
			}
		}
		return Here;
	}

	void MergeNbt(FNbt& Into, const FNbt& From)
	{
		for (int32 I = 0; I < From.Keys.Num(); ++I)
		{
			const int32 At = Into.Find(From.Keys[I]);
			if (At != INDEX_NONE && Into.Items[At].Type == FNbt::EType::Compound && From.Items[I].Type == FNbt::EType::Compound)
			{
				MergeNbt(Into.Items[At], From.Items[I]);
			}
			else if (At != INDEX_NONE)
			{
				Into.Items[At] = From.Items[I];
			}
			else
			{
				Into.Keys.Add(From.Keys[I]);
				Into.Items.Add(From.Items[I]);
			}
		}
	}

	TMap<FString, FNbt>& DataStorage()
	{
		static TMap<FString, FNbt> All;
		return All;
	}

	// Storage lives in <world>/data/command_storage.snbt: one compound, a child per storage id. It is read when the world's folder first
	// meets a /data command, and written after each command that could change it.
	FString& StorageFolder()
	{
		static FString Folder;
		return Folder;
	}

	FString StoragePath(const FString& Folder)
	{
		return Folder / TEXT("data") / TEXT("command_storage.snbt");
	}

	void LoadStorage(const IMcCommandTarget& Target)
	{
		const FString Folder = Target.GetWorldFolder();
		if (Folder == StorageFolder())
		{
			return;
		}
		StorageFolder() = Folder;
		DataStorage().Reset();
		FString Text;
		if (Folder.IsEmpty() || !FFileHelper::LoadFileToString(Text, *StoragePath(Folder)))
		{
			return;
		}
		FNbtParser Parser(Text);
		FNbt Root;
		if (Parser.Value(Root) && Root.Type == FNbt::EType::Compound)
		{
			for (int32 I = 0; I < Root.Keys.Num(); ++I)
			{
				DataStorage().Add(Root.Keys[I], Root.Items[I]);
			}
		}
	}

	void SaveStorage(const IMcCommandTarget& Target)
	{
		const FString Folder = Target.GetWorldFolder();
		if (Folder.IsEmpty())
		{
			return;
		}
		FNbt Root;
		for (const TPair<FString, FNbt>& Pair : DataStorage())
		{
			Root.Keys.Add(Pair.Key);
			Root.Items.Add(Pair.Value);
		}
		FFileHelper::SaveStringToFile(PrintNbt(Root), *StoragePath(Folder));
	}

	FReplies RunData(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Verb = Words[1];
		if (Verb != TEXT("get") && Verb != TEXT("merge") && Verb != TEXT("modify") && Verb != TEXT("remove"))
		{
			return BadArgument(Command);
		}
		const FString& Kind = Words[2];
		if (Kind != TEXT("storage") && Kind != TEXT("entity"))
		{
			return Kind == TEXT("block") ? BadArgument(Command) : BadArgument(Command);
		}
		const auto Qualified = [](const FString& Id) { return Id.Contains(TEXT(":")) ? Id : TEXT("minecraft:") + Id; };
		const auto Tail = [&](int32 From)
		{
			TArray<FString> Rest;
			for (int32 I = From; I < Words.Num(); ++I)
			{
				Rest.Add(Words[I]);
			}
			return FString::Join(Rest, TEXT(" "));
		};
		// Where the n-th word begins in the command.
		const auto WordStart = [&](int32 Index)
		{
			int32 At = 0;
			for (int32 I = 0; I < Index; ++I)
			{
				At += Words[I].Len() + 1;
			}
			return At;
		};
		const auto ParseValue = [&](int32 FromWord, FNbt& Out, FReplies& Error) -> bool
		{
			const FString Source = Tail(FromWord);
			FNbtParser Parser(Source);
			if (!Parser.Value(Out))
			{
				const int32 At = WordStart(FromWord) + Parser.Pos;
				Error = { Fail(McLang::Get(TEXT("snbt.parser.expected_unquoted_string"))), Fail(BorderContext(Command.Left(At), At)) };
				return false;
			}
			Parser.Skip();
			if (Parser.Pos < Source.Len())
			{
				Error = { Fail(McLang::Get(TEXT("argument.nbt.trailing"))), Fail(BorderContext(Command, WordStart(FromWord) + Parser.Pos)) };
				return false;
			}
			return true;
		};
		const FString Changed = McLang::Get(TEXT("commands.data.merge.failed"));

		// The data the target holds, as a root compound; Id names it in replies.
		FNbt Entity;
		FNbt* Root = nullptr;
		FString Id;
		int32 After = 4;
		if (Kind == TEXT("storage"))
		{
			if (Words.Num() < 4)
			{
				return Unknown(Command, Command.Len());
			}
			Id = Qualified(Words[3]);
			Root = &DataStorage().FindOrAdd(Id);
		}
		else
		{
			if (Verb != TEXT("get"))
			{
				return { Fail(McLang::Get(TEXT("commands.data.entity.invalid"))) };
			}
			TArray<FString> Who;
			FString Error;
			if (!ResolvePlayers(Words[3], Sender, Target, Who, Error) || Who.Num() == 0)
			{
				return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
			}
			if (Who.Num() > 1)
			{
				return { Fail(McLang::Get(TEXT("argument.entity.toomany"))) };
			}
			Id = Who[0];
			FVector Feet = FVector::ZeroVector;
			Target.GetPosition(Id, Feet);
			FNbt Pos;
			Pos.Type = FNbt::EType::List;
			for (double Part : { Feet.X, Feet.Y, Feet.Z })
			{
				FNbt One;
				One.Type = FNbt::EType::Double;
				One.Real = Part;
				Pos.Items.Add(One);
			}
			Entity.Keys.Add(TEXT("Pos"));
			Entity.Items.Add(Pos);
			Root = &Entity;
		}

		if (Verb == TEXT("get"))
		{
			TArray<FNbtStep> Steps;
			if (Words.Num() <= After)
			{
				if (Kind == TEXT("entity"))
				{
					return { Ok(McLang::Format(TEXT("commands.data.entity.query"), { Id, PrintNbt(*Root) })) };
				}
				return { Ok(McLang::Format(TEXT("commands.data.storage.query"), { Id, PrintNbt(*Root) })) };
			}
			if (!ParseNbtPath(Words[After], Steps))
			{
				return BadArgument(Command);
			}
			FNbt* Found = NbtAt(*Root, Steps, false);
			if (!Found)
			{
				return { Fail(McLang::Format(TEXT("arguments.nbtpath.nothing_found"), { Words[After] })) };
			}
			if (Words.Num() <= After + 1)
			{
				return { Ok(McLang::Format(Kind == TEXT("entity") ? TEXT("commands.data.entity.query") : TEXT("commands.data.storage.query"), { Id, PrintNbt(*Found) })) };
			}
			double Scale = 0.0;
			if (!ParseNumber(Words[After + 1], Scale))
			{
				return BadArgument(Command);
			}
			if (!Found->IsNumber())
			{
				return { Fail(McLang::Format(TEXT("commands.data.get.invalid"), { Words[After] })) };
			}
			const FString Amount = FString::FromInt(FMath::FloorToInt32(Found->AsDouble() * Scale));
			return { Ok(McLang::Format(Kind == TEXT("entity") ? TEXT("commands.data.entity.get") : TEXT("commands.data.storage.get"), { Words[After], Id, FString::Printf(TEXT("%.2f"), Scale), Amount })) };
		}
		const FString Modified = McLang::Format(TEXT("commands.data.storage.modified"), { Id });
		if (Verb == TEXT("merge"))
		{
			FNbt Value;
			FReplies Error;
			if (Words.Num() <= After)
			{
				return Unknown(Command, Command.Len());
			}
			if (!ParseValue(After, Value, Error))
			{
				return Error;
			}
			if (Value.Type != FNbt::EType::Compound)
			{
				return { Fail(McLang::Get(TEXT("argument.nbt.expected.compound"))) };
			}
			FNbt Merged = *Root;
			MergeNbt(Merged, Value);
			if (Merged == *Root)
			{
				return { Fail(Changed) };
			}
			*Root = Merged;
			return { Ok(Modified) };
		}
		if (Words.Num() <= After)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FNbtStep> Steps;
		if (!ParseNbtPath(Words[After], Steps))
		{
			return BadArgument(Command);
		}
		if (Verb == TEXT("remove"))
		{
			if (Steps.Num() == 0 || Words.Num() != After + 1)
			{
				return BadArgument(Command);
			}
			const FNbtStep Last = Steps.Last();
			Steps.Pop();
			FNbt* Parent = NbtAt(*Root, Steps, false);
			if (!Parent)
			{
				return { Fail(Changed) };
			}
			if (Last.bIndex)
			{
				const int32 At = Last.Index < 0 ? Parent->Items.Num() + Last.Index : Last.Index;
				if (Parent->Type == FNbt::EType::Compound || !Parent->Items.IsValidIndex(At))
				{
					return { Fail(Changed) };
				}
				Parent->Items.RemoveAt(At);
				return { Ok(Modified) };
			}
			const int32 At = Parent->Type == FNbt::EType::Compound ? Parent->Find(Last.Key) : INDEX_NONE;
			if (At == INDEX_NONE)
			{
				return { Fail(Changed) };
			}
			Parent->Keys.RemoveAt(At);
			Parent->Items.RemoveAt(At);
			return { Ok(Modified) };
		}

		// modify <path> (set|merge|append|prepend|insert <index>) (value <nbt>|from storage <id> [<path>])
		if (Words.Num() <= After + 1)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Op = Words[After + 1];
		int32 Next = After + 2;
		int32 InsertAt = 0;
		if (Op == TEXT("insert"))
		{
			if (Words.Num() <= Next || !Words[Next].IsNumeric())
			{
				return Words.Num() <= Next ? Unknown(Command, Command.Len()) : BadArgument(Command);
			}
			InsertAt = FCString::Atoi(*Words[Next]);
			++Next;
		}
		else if (Op != TEXT("set") && Op != TEXT("merge") && Op != TEXT("append") && Op != TEXT("prepend"))
		{
			return BadArgument(Command);
		}
		if (Words.Num() <= Next)
		{
			return Unknown(Command, Command.Len());
		}
		FNbt Value;
		if (Words[Next] == TEXT("value"))
		{
			FReplies Error;
			if (Words.Num() <= Next + 1)
			{
				return Unknown(Command, Command.Len());
			}
			if (!ParseValue(Next + 1, Value, Error))
			{
				return Error;
			}
		}
		else if (Words[Next] == TEXT("from") && Words.Num() > Next + 2 && Words[Next + 1] == TEXT("storage"))
		{
			FNbt* Source = DataStorage().Find(Qualified(Words[Next + 2]));
			FNbt* Picked = Source ? Source : nullptr;
			if (Picked && Words.Num() > Next + 3)
			{
				TArray<FNbtStep> From;
				if (!ParseNbtPath(Words[Next + 3], From))
				{
					return BadArgument(Command);
				}
				Picked = NbtAt(*Picked, From, false);
				if (!Picked)
				{
					return { Fail(McLang::Format(TEXT("arguments.nbtpath.nothing_found"), { Words[Next + 3] })) };
				}
			}
			if (!Picked)
			{
				return { Fail(McLang::Format(TEXT("arguments.nbtpath.nothing_found"), { TEXT("") })) };
			}
			Value = *Picked;
		}
		else
		{
			return BadArgument(Command);
		}

		if (Steps.Num() == 0)
		{
			if (Op != TEXT("set") && Op != TEXT("merge"))
			{
				return BadArgument(Command);
			}
			if (Value.Type != FNbt::EType::Compound)
			{
				return { Fail(McLang::Format(TEXT("commands.data.modify.expected_object"), { PrintNbt(Value) })) };
			}
			FNbt New = Op == TEXT("set") ? Value : *Root;
			if (Op == TEXT("merge"))
			{
				MergeNbt(New, Value);
			}
			if (New == *Root)
			{
				return { Fail(Changed) };
			}
			*Root = New;
			return { Ok(Modified) };
		}
		FNbt Work = *Root;
		FNbt* Place = NbtAt(Work, Steps, Op != TEXT("merge"));
		if (Op == TEXT("set"))
		{
			if (!Place)
			{
				return { Fail(McLang::Format(TEXT("commands.data.modify.invalid_index"), { Words[After] })) };
			}
			*Place = Value;
		}
		else if (Op == TEXT("merge"))
		{
			if (!Place || Place->Type != FNbt::EType::Compound)
			{
				return { Fail(McLang::Format(TEXT("commands.data.modify.expected_object"), { Place ? PrintNbt(*Place) : FString() })) };
			}
			if (Value.Type != FNbt::EType::Compound)
			{
				return { Fail(McLang::Format(TEXT("commands.data.modify.expected_object"), { PrintNbt(Value) })) };
			}
			MergeNbt(*Place, Value);
		}
		else
		{
			if (!Place)
			{
				return { Fail(McLang::Format(TEXT("arguments.nbtpath.nothing_found"), { Words[After] })) };
			}
			if (Place->Type == FNbt::EType::Compound && Place->Keys.Num() == 0 && Place->Items.Num() == 0)
			{
				Place->Type = FNbt::EType::List;
			}
			if (Place->Type != FNbt::EType::List)
			{
				return { Fail(McLang::Format(TEXT("commands.data.modify.expected_list"), { PrintNbt(*Place) })) };
			}
			int32 At = Op == TEXT("append") ? Place->Items.Num() : Op == TEXT("prepend") ? 0 : InsertAt;
			if (At < 0)
			{
				At = Place->Items.Num() + At + 1;
			}
			if (At < 0 || At > Place->Items.Num())
			{
				return { Fail(McLang::Format(TEXT("commands.data.modify.invalid_index"), { FString::FromInt(InsertAt) })) };
			}
			Place->Items.Insert(Value, At);
		}
		if (Work == *Root)
		{
			return { Fail(Changed) };
		}
		*Root = Work;
		return { Ok(Modified) };
	}

	// /trigger <objective> [add|set <value>]: a player changes their own score on a trigger objective they were enabled for (wording from a ref run).
	FReplies RunTrigger(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() != 2 && Words.Num() != 4)
		{
			return Words.Num() < 2 ? Unknown(Command, Command.Len()) : BadArgument(Command);
		}
		FObjective* Objective = FindObjective(Words[1]);
		if (!Objective)
		{
			return { Fail(FString::Printf(TEXT("Unknown scoreboard objective '%s'"), *Words[1])) };
		}
		int32 Amount = 1;
		bool bSet = false;
		if (Words.Num() == 4)
		{
			double Number = 0.0;
			if ((Words[2] != TEXT("add") && Words[2] != TEXT("set")) || !ParseNumber(Words[3], Number) || Number != FMath::FloorToDouble(Number))
			{
				return BadArgument(Command);
			}
			bSet = Words[2] == TEXT("set");
			Amount = FMath::RoundToInt32(Number);
		}
		TArray<FString> Who;
		FString Error;
		if (!ResolvePlayers(TEXT("@s"), Sender, Target, Who, Error) || Who.Num() != 1)
		{
			return { Fail(McLang::Get(TEXT("argument.entity.notfound.entity"))) };
		}
		if (Objective->Criteria != TEXT("trigger"))
		{
			return { Fail(McLang::Get(TEXT("commands.trigger.failed.invalid"))) };
		}
		if (!Objective->Enabled.Contains(Who[0]))
		{
			return { Fail(McLang::Get(TEXT("commands.trigger.failed.unprimed"))) };
		}
		int32& Score = Objective->Scores.FindOrAdd(Who[0]);
		Score = bSet ? Amount : Score + Amount;
		Objective->Enabled.Remove(Who[0]);
		if (Words.Num() == 2)
		{
			return { Ok(McLang::Format(TEXT("commands.trigger.simple.success"), { Shown(*Objective) })) };
		}
		return { Ok(McLang::Format(bSet ? TEXT("commands.trigger.set.success") : TEXT("commands.trigger.add.success"), { Shown(*Objective), FString::FromInt(Amount) })) };
	}

	// /teammsg and /tm <message>: to everyone on the sender's team (wording from a ref run, cmd_group5.txt).
	FReplies RunTeamMsg(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		const FString Me = SenderName(Sender);
		FTeam* Mine = nullptr;
		for (TPair<FString, FTeam>& Entry : Teams())
		{
			if (Entry.Value.Members.Contains(Me))
			{
				Mine = &Entry.Value;
				break;
			}
		}
		if (!Mine)
		{
			return { Fail(McLang::Get(TEXT("commands.teammsg.failed.noteam"))) };
		}
		const FString Shown = TEXT("[") + (Mine->Display.IsEmpty() ? Mine->Name : Mine->Display) + TEXT("]");
		const FString Text = RestOf(Words, 1);
		for (const FString& Member : Mine->Members)
		{
			if (Member != Me)
			{
				Target.SendTo(Member, Ok(McLang::Format(TEXT("chat.type.team.text"), { Shown, Me, Text })));
			}
		}
		return { Ok(McLang::Format(TEXT("chat.type.team.sent"), { Shown, Me, Text })) };
	}

	// /stopwatch create|query|restart|remove <id>: stopwatches run on real time, kept for the session (wording from a ref run).
	TMap<FString, double>& Stopwatches()
	{
		static TMap<FString, double> All;
		return All;
	}

	FReplies RunStopwatch(const FString& Command, const TArray<FString>& Words)
	{
		if (Words.Num() < 3 || Words.Num() > 4)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Verb = Words[1];
		const FString Id = McCache::Namespaced(Words[2]);
		double* Started = Stopwatches().Find(Id);
		if (Verb == TEXT("create"))
		{
			if (Started)
			{
				return { Fail(McLang::Format(TEXT("commands.stopwatch.already_exists"), { Id })) };
			}
			Stopwatches().Add(Id, FPlatformTime::Seconds());
			return { Ok(McLang::Format(TEXT("commands.stopwatch.create.success"), { Id })) };
		}
		if (Verb != TEXT("query") && Verb != TEXT("restart") && Verb != TEXT("remove"))
		{
			return BadArgument(Command);
		}
		if (!Started)
		{
			return { Fail(McLang::Format(TEXT("commands.stopwatch.does_not_exist"), { Id })) };
		}
		if (Verb == TEXT("restart"))
		{
			*Started = FPlatformTime::Seconds();
			return { Ok(McLang::Format(TEXT("commands.stopwatch.restart.success"), { Id })) };
		}
		if (Verb == TEXT("remove"))
		{
			Stopwatches().Remove(Id);
			return { Ok(McLang::Format(TEXT("commands.stopwatch.remove.success"), { Id })) };
		}
		// To whole milliseconds, as the game shows it.
		FString Seconds = FString::SanitizeFloat(FMath::RoundToDouble((FPlatformTime::Seconds() - *Started) * 1000.0) / 1000.0, 0);
		return { Ok(McLang::Format(TEXT("commands.stopwatch.query"), { Id, Seconds })) };
	}

	// A player's slot by the game's name: group 0 the inventory (0 to 8 the hotbar, 9 to 35 the rest), 1 the armour (head, chest,
	// legs, feet), 2 the offhand. False for a name that is no slot of a player.
	bool ParseSlot(const FString& Name, const IMcCommandTarget& Target, const FString& Player, int32& Group, int32& Index)
	{
		const auto Number = [&](const TCHAR* Prefix, int32 Max, int32& Out)
		{
			if (!Name.StartsWith(Prefix))
			{
				return false;
			}
			const FString Tail = Name.RightChop(FCString::Strlen(Prefix));
			if (Tail.IsEmpty() || !Tail.IsNumeric() || Tail.Contains(TEXT(".")) || Tail.StartsWith(TEXT("-")))
			{
				return false;
			}
			Out = FCString::Atoi(*Tail);
			return Out >= 0 && Out < Max;
		};
		int32 N = 0;
		Group = 0;
		if (Number(TEXT("hotbar."), 9, N) || Number(TEXT("container."), 36, N))
		{
			Index = N;
			return true;
		}
		if (Number(TEXT("inventory."), 27, N))
		{
			Index = N + 9;
			return true;
		}
		if (Name == TEXT("weapon") || Name == TEXT("weapon.mainhand"))
		{
			Index = Target.GetSelectedSlot(Player);
			return true;
		}
		Group = 1;
		if (Name == TEXT("armor.head")) { Index = 0; return true; }
		if (Name == TEXT("armor.chest")) { Index = 1; return true; }
		if (Name == TEXT("armor.legs")) { Index = 2; return true; }
		if (Name == TEXT("armor.feet")) { Index = 3; return true; }
		Group = 2;
		Index = 0;
		return Name == TEXT("weapon.offhand");
	}

	// /item replace entity <players> <slot> with <item> [count] | from entity <players> <slot> (wording from a ref run, cmd_group4.txt).
	// Players only; blocks, modify and the other slots (enderchest, the crafting grid) are not built.
	FReplies RunItem(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 6 || Words[1] != TEXT("replace") || Words[2] != TEXT("entity"))
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[3], Sender, Target, Players, Error))
		{
			// An entity argument: a name nobody has is "No entity was found", not the player argument's wording.
			return { Fail(Words[3].StartsWith(TEXT("@")) ? Error : McLang::Get(TEXT("argument.entity.notfound.entity"))) };
		}
		// Where each word starts, for the context line.
		const auto StartOf = [&](int32 Word)
		{
			int32 At = 0;
			for (int32 I = 0; I < Word; ++I)
			{
				At = Command.Find(Words[I], ESearchCase::CaseSensitive, ESearchDir::FromStart, At) + Words[I].Len();
			}
			return Command.Find(Words[Word], ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
		};
		const auto BadSlot = [&](int32 Word, const FString& Slot, const TCHAR* Registry) -> FReplies
		{
			return { Fail(FString::Printf(TEXT("Can't find element 'minecraft:%s' in registry 'minecraft:%s'"), *Slot, Registry)), Fail(BorderContext(Command, StartOf(Word))) };
		};
		int32 Group = 0;
		int32 Index = 0;
		// The slot is checked against the first player.
		if (Players.IsEmpty() || !ParseSlot(Words[4], Target, Players[0], Group, Index))
		{
			return BadSlot(4, Words[4], TEXT("slot_source"));
		}
		FName Item;
		int32 Count = 1;
		bool bKnownItem = false;
		if (Words[5] == TEXT("with"))
		{
			if (Words.Num() < 7 || Words.Num() > 8)
			{
				return Unknown(Command, Command.Len());
			}
			Item = ItemId(Words[6]);
			if (Target.GetMaxStackSize(Item) <= 0)
			{
				return { Fail(McLang::Format(TEXT("argument.item.id.invalid"), { Item.ToString() })), Fail(BorderContext(Command, StartOf(6))) };
			}
			if (Words.Num() == 8)
			{
				if (!Words[7].IsNumeric() || Words[7].Contains(TEXT(".")) || FCString::Atoi(*Words[7]) < 1 || FCString::Atoi(*Words[7]) > 99)
				{
					return BadArgument(Command);
				}
				Count = FCString::Atoi(*Words[7]);
			}
			bKnownItem = true;
		}
		else if (Words[5] == TEXT("from") && Words.Num() == 9 && Words[6] == TEXT("entity"))
		{
			TArray<FString> From;
			int32 FromGroup = 0;
			int32 FromIndex = 0;
			if (!ResolvePlayers(Words[7], Sender, Target, From, Error))
			{
				return { Fail(Error) };
			}
			if (From.IsEmpty() || !ParseSlot(Words[8], Target, From[0], FromGroup, FromIndex))
			{
				return BadSlot(8, Words[8], TEXT("slot_source"));
			}
			if (!Target.GetSlot(From[0], FromGroup, FromIndex, Item, Count))
			{
				Item = NAME_None;
				Count = 0;
			}
		}
		else
		{
			return Unknown(Command, Command.Len());
		}
		int32 Changed = 0;
		for (const FString& Player : Players)
		{
			int32 PlayerGroup = 0;
			int32 PlayerIndex = 0;
			if (ParseSlot(Words[4], Target, Player, PlayerGroup, PlayerIndex) && Target.SetSlot(Player, PlayerGroup, PlayerIndex, Item, Count))
			{
				++Changed;
			}
		}
		if (Changed == 0)
		{
			return { Fail(McLang::Get(TEXT("commands.item.target.failed"))) };
		}
		const FString Shown = bKnownItem ? TEXT("[") + Target.GetItemName(Item) + TEXT("]") : FString();
		if (Changed == 1)
		{
			return { Ok(bKnownItem ? McLang::Format(TEXT("commands.item.entity.replace.success.single.known_item"), { TEXT("1"), Players[0], Shown })
				: McLang::Format(TEXT("commands.item.entity.replace.success.single"), { TEXT("1"), Players[0] })) };
		}
		return { Ok(bKnownItem ? McLang::Format(TEXT("commands.item.entity.replace.success.multiple.known_item"), { FString::FromInt(Changed), Shown })
			: McLang::Format(TEXT("commands.item.entity.replace.success.multiple"), { FString::FromInt(Changed) })) };
	}

	// /loot give <players> loot <table>: rolls the table and gives the stacks (wording from a ref run, cmd_group4.txt).
	// Only the give target with the loot source is built; mine and kill sources, spawn, insert and replace are not.
	FReplies RunLoot(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3 || Words[1] != TEXT("give") || Words.Num() != 5 || Words[3] != TEXT("loot"))
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[2], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		const FString Table = McCache::Namespaced(Words[4]);
		TArray<FMcItemStack> Stacks;
		static uint64 Draw = 0x6c6f6f74;
		FMcRandom Random(++Draw * 0x9E3779B97F4A7C15ull);
		if (!McLoot::Roll(Table, Random, Stacks))
		{
			int32 At = Command.Find(Words[4], ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			return { Fail(FString::Printf(TEXT("Can't find element '%s' in registry 'minecraft:loot_table'"), *Table)), Fail(BorderContext(Command, At)) };
		}
		int32 Total = 0;
		FName Only;
		for (const FMcItemStack& Stack : Stacks)
		{
			if (Stack.IsEmpty())
			{
				continue;
			}
			Total += Stack.Count;
			Only = Stack.Item;
			for (const FString& Player : Players)
			{
				Target.Give(Player, Stack.Item, Stack.Count);
			}
		}
		const FString Count = FString::FromInt(Total);
		if (Total == 1)
		{
			return { Ok(McLang::Format(TEXT("commands.drop.success.single_with_table"), { Count, TEXT("[") + Target.GetItemName(Only) + TEXT("]"), Table })) };
		}
		return { Ok(McLang::Format(TEXT("commands.drop.success.multiple_with_table"), { Count, Table })) };
	}

	// ---- /attribute (wording from a ref run, cmd_group4.txt) ----
	// The values live here, per player, and only max_health reaches the game (through SetAttribute); the others are kept and
	// reported but change nothing yet. Not kept in the save.
	struct FAttributeDefault
	{
		const TCHAR* Id;
		double Value;
	};

	// Player defaults (minecraft.wiki, "Attribute").
	const FAttributeDefault AttributeDefaults[] = {
		{ TEXT("max_health"), 20.0 }, { TEXT("movement_speed"), (double)0.1f }, { TEXT("attack_damage"), 1.0 }, { TEXT("attack_speed"), 4.0 },
		{ TEXT("attack_knockback"), 0.0 }, { TEXT("armor"), 0.0 }, { TEXT("armor_toughness"), 0.0 }, { TEXT("luck"), 0.0 },
		{ TEXT("knockback_resistance"), 0.0 }, { TEXT("block_interaction_range"), 4.5 }, { TEXT("entity_interaction_range"), 3.0 },
		{ TEXT("gravity"), 0.08 }, { TEXT("jump_strength"), (double)0.42f }, { TEXT("step_height"), 0.6 }, { TEXT("safe_fall_distance"), 3.0 },
		{ TEXT("fall_damage_multiplier"), 1.0 }, { TEXT("scale"), 1.0 }, { TEXT("flying_speed"), (double)0.05f }, { TEXT("max_absorption"), 0.0 },
		{ TEXT("burning_time"), 1.0 }, { TEXT("movement_efficiency"), 0.0 }, { TEXT("oxygen_bonus"), 0.0 }, { TEXT("water_movement_efficiency"), 0.0 },
		{ TEXT("sneaking_speed"), (double)0.3f }, { TEXT("submerged_mining_speed"), 0.2 }, { TEXT("mining_efficiency"), 0.0 },
		{ TEXT("block_break_speed"), 1.0 }, { TEXT("sweeping_damage_ratio"), 0.0 }, { TEXT("explosion_knockback_resistance"), 0.0 },
	};

	struct FAttributeModifier
	{
		double Amount = 0.0;
		int32 Operation = 0;
	};

	struct FAttributeState
	{
		TOptional<double> Base;
		TMap<FString, FAttributeModifier> Modifiers;
	};

	TMap<FString, TMap<FString, FAttributeState>>& AttributeStates()
	{
		static TMap<FString, TMap<FString, FAttributeState>> States;
		return States;
	}

	// Java prints a double as its shortest text that reads back the same, with ".0" on a whole number.
	FString AttrDouble(double Value)
	{
		FString Text;
		for (int32 Digits = 1; Digits <= 17; ++Digits)
		{
			Text = FString::Printf(TEXT("%.*g"), Digits, Value);
			if (FCString::Atod(*Text) == Value)
			{
				break;
			}
		}
		if (Text.Contains(TEXT("e")))
		{
			Text = FString::Printf(TEXT("%.1f"), Value);
		}
		if (!Text.Contains(TEXT(".")))
		{
			Text += TEXT(".0");
		}
		return Text;
	}

	double AttributeValue(double Base, const FAttributeState& State)
	{
		double Add = 0.0;
		double MultBase = 0.0;
		for (const auto& Entry : State.Modifiers)
		{
			if (Entry.Value.Operation == 0)
			{
				Add += Entry.Value.Amount;
			}
			else if (Entry.Value.Operation == 1)
			{
				MultBase += Entry.Value.Amount;
			}
		}
		double Value = Base + Add;
		const double Start = Value;
		Value += Start * MultBase;
		for (const auto& Entry : State.Modifiers)
		{
			if (Entry.Value.Operation == 2)
			{
				Value *= 1.0 + Entry.Value.Amount;
			}
		}
		return Value;
	}

	FReplies RunAttribute(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 4)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		if (Players.Num() > 1)
		{
			return { Fail(McLang::Get(TEXT("argument.entity.toomany"))) };
		}
		const FString Id = McCache::Namespaced(Words[2]);
		const FAttributeDefault* Known = nullptr;
		for (const FAttributeDefault& Candidate : AttributeDefaults)
		{
			if (Id == FString(TEXT("minecraft:")) + Candidate.Id)
			{
				Known = &Candidate;
			}
		}
		if (!Known)
		{
			int32 At = Command.Find(Words[2], ESearchCase::CaseSensitive, ESearchDir::FromStart, Command.Find(Words[1]) + Words[1].Len());
			return { Fail(FString::Printf(TEXT("Can't find element '%s' of type 'minecraft:attribute'"), *Id)), Fail(BorderContext(Command, At + Words[2].Len())) };
		}
		const FString Player = Players[0];
		const FString Shown = McLang::Get(FString(TEXT("attribute.name.")) + Known->Id);
		FAttributeState& State = AttributeStates().FindOrAdd(Player).FindOrAdd(Known->Id);
		const auto Push = [&]()
		{
			Target.SetAttribute(Player, Known->Id, AttributeValue(State.Base.Get(Known->Value), State));
		};
		const double Base = State.Base.Get(Known->Value);
		const FString& Verb = Words[3];
		if (Verb == TEXT("get") && Words.Num() <= 5)
		{
			return { Ok(McLang::Format(TEXT("commands.attribute.value.get.success"), { Shown, Player, AttrDouble(AttributeValue(Base, State)) })) };
		}
		if (Verb == TEXT("base") && Words.Num() >= 5)
		{
			if (Words[4] == TEXT("get") && Words.Num() <= 6)
			{
				return { Ok(McLang::Format(TEXT("commands.attribute.base_value.get.success"), { Shown, Player, AttrDouble(Base) })) };
			}
			if (Words[4] == TEXT("set") && Words.Num() == 6)
			{
				if (!Words[5].IsNumeric())
				{
					return BadArgument(Command);
				}
				State.Base = FCString::Atod(*Words[5]);
				Push();
				return { Ok(McLang::Format(TEXT("commands.attribute.base_value.set.success"), { Shown, Player, AttrDouble(*State.Base) })) };
			}
			if (Words[4] == TEXT("reset") && Words.Num() == 5)
			{
				State.Base.Reset();
				Push();
				return { Ok(McLang::Format(TEXT("commands.attribute.base_value.reset.success"), { Shown, Player, AttrDouble(Known->Value) })) };
			}
		}
		if (Verb == TEXT("modifier") && Words.Num() >= 6)
		{
			const FString Modifier = McCache::Namespaced(Words[5]);
			if (Words[4] == TEXT("add") && Words.Num() == 8)
			{
				const int32 Operation = Words[7] == TEXT("add_value") ? 0 : Words[7] == TEXT("add_multiplied_base") ? 1 : Words[7] == TEXT("add_multiplied_total") ? 2 : -1;
				if (!Words[6].IsNumeric() || Operation < 0)
				{
					return BadArgument(Command);
				}
				if (State.Modifiers.Contains(Modifier))
				{
					return { Fail(McLang::Format(TEXT("commands.attribute.failed.modifier_already_present"), { Modifier, Shown, Player })) };
				}
				State.Modifiers.Add(Modifier, FAttributeModifier{ FCString::Atod(*Words[6]), Operation });
				Push();
				return { Ok(McLang::Format(TEXT("commands.attribute.modifier.add.success"), { Modifier, Shown, Player })) };
			}
			if (Words[4] == TEXT("remove") && Words.Num() == 6)
			{
				if (!State.Modifiers.Remove(Modifier))
				{
					return { Fail(McLang::Format(TEXT("commands.attribute.failed.no_modifier"), { Shown, Player, Modifier })) };
				}
				Push();
				return { Ok(McLang::Format(TEXT("commands.attribute.modifier.remove.success"), { Modifier, Shown, Player })) };
			}
			if (Words[4] == TEXT("value") && Words.Num() >= 7 && Words[5] == TEXT("get"))
			{
				const FString Wanted = McCache::Namespaced(Words[6]);
				const FAttributeModifier* Found = State.Modifiers.Find(Wanted);
				if (!Found)
				{
					return { Fail(McLang::Format(TEXT("commands.attribute.failed.no_modifier"), { Shown, Player, Wanted })) };
				}
				return { Ok(McLang::Format(TEXT("commands.attribute.modifier.value.get.success"), { Wanted, Shown, Player, AttrDouble(Found->Amount) })) };
			}
		}
		return Unknown(Command, Command.Len());
	}

	FReplies RunTag(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3 || Words.Num() > 4)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		const FString& Verb = Words[2];
		if (Verb == TEXT("list"))
		{
			if (Words.Num() != 3)
			{
				return Unknown(Command, Command.Len());
			}
			TArray<FString> All;
			for (const FString& Player : Players)
			{
				All.Append(PlayerTags().FindOrAdd(Player));
			}
			if (Players.Num() == 1)
			{
				return { Ok(All.IsEmpty() ? McLang::Format(TEXT("commands.tag.list.single.empty"), { Players[0] })
					: McLang::Format(TEXT("commands.tag.list.single.success"), { Players[0], FString::FromInt(All.Num()), FString::Join(All, TEXT(", ")) })) };
			}
			return { Ok(All.IsEmpty() ? McLang::Format(TEXT("commands.tag.list.multiple.empty"), { FString::FromInt(Players.Num()) })
				: McLang::Format(TEXT("commands.tag.list.multiple.success"), { FString::FromInt(Players.Num()), FString::FromInt(All.Num()), FString::Join(All, TEXT(", ")) })) };
		}
		if ((Verb != TEXT("add") && Verb != TEXT("remove")) || Words.Num() != 4)
		{
			return Words.Num() == 3 ? Unknown(Command, Command.Len()) : BadArgument(Command);
		}
		const bool bAdd = Verb == TEXT("add");
		int32 Changed = 0;
		for (const FString& Player : Players)
		{
			TArray<FString>& Tags = PlayerTags().FindOrAdd(Player);
			if (bAdd ? !Tags.Contains(Words[3]) : Tags.Contains(Words[3]))
			{
				if (bAdd)
				{
					Tags.Add(Words[3]);
				}
				else
				{
					Tags.Remove(Words[3]);
				}
				++Changed;
			}
		}
		if (Changed == 0)
		{
			return { Fail(McLang::Get(bAdd ? TEXT("commands.tag.add.failed") : TEXT("commands.tag.remove.failed"))) };
		}
		const TCHAR* Key = bAdd ? (Changed == 1 ? TEXT("commands.tag.add.success.single") : TEXT("commands.tag.add.success.multiple"))
			: (Changed == 1 ? TEXT("commands.tag.remove.success.single") : TEXT("commands.tag.remove.success.multiple"));
		return { Ok(McLang::Format(Key, { Words[3], Changed == 1 ? Players[0] : FString::FromInt(Changed) })) };
	}

	FReplies RunStopSound(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2 || Words.Num() > 4)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		static const TCHAR* Sources[] = { TEXT("master"), TEXT("music"), TEXT("record"), TEXT("weather"), TEXT("block"), TEXT("hostile"),
			TEXT("neutral"), TEXT("player"), TEXT("ambient"), TEXT("voice"), TEXT("ui") };
		FString Source;
		if (Words.Num() >= 3 && Words[2] != TEXT("*"))
		{
			bool bKnown = false;
			for (const TCHAR* Each : Sources)
			{
				bKnown |= Words[2] == Each;
			}
			if (!bKnown)
			{
				return BadArgument(Command);
			}
			Source = Words[2];
		}
		FString Sound = Words.Num() == 4 ? Words[3] : FString();
		if (!Sound.IsEmpty() && !Sound.Contains(TEXT(":")))
		{
			Sound = TEXT("minecraft:") + Sound;
		}
		for (const FString& Player : Players)
		{
			Target.StopSounds(Player, Source, Sound);
		}
		if (Source.IsEmpty())
		{
			return { Ok(Sound.IsEmpty() ? McLang::Get(TEXT("commands.stopsound.success.sourceless.any"))
				: McLang::Format(TEXT("commands.stopsound.success.sourceless.sound"), { Sound })) };
		}
		return { Ok(Sound.IsEmpty() ? McLang::Format(TEXT("commands.stopsound.success.source.any"), { Source })
			: McLang::Format(TEXT("commands.stopsound.success.source.sound"), { Sound, Source })) };
	}

	FReplies RunVersion()
	{
		return {
			Ok(TEXT("Server version info:")), Ok(TEXT("id = 26.3")), Ok(TEXT("name = 26.3")), Ok(TEXT("data = 5023")), Ok(TEXT("series = main")),
			Ok(TEXT("protocol = 777 (0x309)")), Ok(TEXT("pack_resource = 97.1")), Ok(TEXT("pack_data = 121.0")), Ok(TEXT("stable = yes")),
		};
	}

	FReplies RunDifficulty(const FString& Command, const TArray<FString>& Words, IMcCommandTarget& Target)
	{
		static const TCHAR* Names[] = { TEXT("peaceful"), TEXT("easy"), TEXT("normal"), TEXT("hard") };
		const auto Shown = [](int32 Level) { return McLang::Get(FString(TEXT("options.difficulty.")) + Names[FMath::Clamp(Level, 0, 3)]); };
		if (Words.Num() == 1)
		{
			return { Ok(McLang::Format(TEXT("commands.difficulty.query"), { Shown(Target.GetDifficulty()) })) };
		}
		if (Words.Num() != 2)
		{
			return Unknown(Command, Command.Len());
		}
		int32 Level = INDEX_NONE;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Level = Words[1] == Names[Index] ? Index : Level;
		}
		if (Level == INDEX_NONE)
		{
			return BadArgument(Command);
		}
		if (Level == Target.GetDifficulty())
		{
			return { Fail(McLang::Format(TEXT("commands.difficulty.failure"), { Shown(Level) })) };
		}
		Target.SetDifficulty(Level);
		return { Ok(McLang::Format(TEXT("commands.difficulty.success"), { Shown(Level) })) };
	}

	FReplies RunGameRule(const FString& Command, const TArray<FString>& Words, IMcCommandTarget& Target)
	{
		if (Words.Num() < 2 || Words.Num() > 3)
		{
			return Unknown(Command, Command.Len());
		}
		const McCommands::FGameRule* Rule = McCommands::FindGameRule(Words[1]);
		if (!Rule)
		{
			return Unknown(Command, Command.Len());
		}
		const FString Set = Target.GetGameRule(Rule->Name);
		const FString Current = Set.IsEmpty() ? FString(Rule->Default) : Set;
		if (Words.Num() == 2)
		{
			return { Ok(McLang::Format(TEXT("commands.gamerule.query"), { Rule->Name, Current })) };
		}
		FString Value = Words[2];
		if (Rule->bInteger ? !(Value.IsNumeric() && !Value.Contains(TEXT("."))) : !(Value == TEXT("true") || Value == TEXT("false")))
		{
			return BadArgument(Command);
		}
		if (Rule->bInteger)
		{
			Value = FString::FromInt(FCString::Atoi(*Value));
		}
		if (Value == Current)
		{
			return { Fail(McLang::Format(TEXT("commands.gamerule.not_set"), { Rule->Name, Current })) };
		}
		Target.SetGameRule(Rule->Name, Value);
		return { Ok(McLang::Format(TEXT("commands.gamerule.set"), { Rule->Name, Value })) };
	}

	FReplies RunMessage(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		const FString Text = RestOf(Words, 2);
		FReplies Replies;
		for (const FString& Player : Players)
		{
			Target.SendTo(Player, Ok(McLang::Format(TEXT("commands.message.display.incoming"), { SenderName(Sender), Text })));
			Replies.Add(Ok(McLang::Format(TEXT("commands.message.display.outgoing"), { Player, Text })));
		}
		return Replies;
	}

	// What is left of a command after its first Count words and the spaces after them, exactly as typed (JSON text keeps its spaces).
	FString RawAfter(const FString& Command, int32 Count)
	{
		int32 At = 0;
		for (int32 Word = 0; Word < Count; ++Word)
		{
			while (At < Command.Len() && Command[At] == TEXT(' '))
			{
				++At;
			}
			while (At < Command.Len() && Command[At] != TEXT(' '))
			{
				++At;
			}
		}
		while (At < Command.Len() && Command[At] == TEXT(' '))
		{
			++At;
		}
		return Command.Mid(At);
	}

	// A JSON text component as legacy text; false, with the game's reply in Error, when it isn't one.
	bool ReadComponent(const FString& Json, FString& OutLegacy, FString& Error)
	{
		TArray<McText::FSpan> Spans;
		if (Json.IsEmpty() || !McText::Parse(Json, Spans))
		{
			Error = McLang::Format(TEXT("argument.component.invalid"), { Json });
			return false;
		}
		OutLegacy = McText::ToLegacy(Spans);
		return true;
	}

	FReplies RunTellraw(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		FString Legacy;
		if (!ReadComponent(RawAfter(Command, 2), Legacy, Error))
		{
			return { Fail(Error) };
		}
		for (const FString& Player : Players)
		{
			Target.SendTo(Player, Ok(Legacy));
		}
		// The game says nothing back: what the player sees is the message.
		return {};
	}

	FReplies RunTitle(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		if (Words.Num() < 3)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		const FString& What = Words[2];
		int32 Kind = -1;
		FString Key, Text;
		int32 Times[3] = { 0, 0, 0 };
		if (What == TEXT("clear") || What == TEXT("reset"))
		{
			if (Words.Num() != 3)
			{
				return Unknown(Command, Command.Len());
			}
			Kind = What == TEXT("clear") ? 3 : 4;
			Key = What == TEXT("clear") ? TEXT("commands.title.cleared") : TEXT("commands.title.reset");
		}
		else if (What == TEXT("title") || What == TEXT("subtitle") || What == TEXT("actionbar"))
		{
			if (Words.Num() < 4 || !ReadComponent(RawAfter(Command, 3), Text, Error))
			{
				return Words.Num() < 4 ? Unknown(Command, Command.Len()) : FReplies{ Fail(Error) };
			}
			Kind = What == TEXT("title") ? 0 : What == TEXT("subtitle") ? 1 : 2;
			Key = TEXT("commands.title.show.") + What;
		}
		else if (What == TEXT("times"))
		{
			if (Words.Num() != 6)
			{
				return Unknown(Command, Command.Len());
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Times[Index] = McCommands::ParseTime(Words[3 + Index]);
				if (Times[Index] < 0)
				{
					return Unknown(Command, Command.Len());
				}
			}
			Kind = 5;
			Key = TEXT("commands.title.times");
		}
		else
		{
			return Unknown(Command, Command.Len());
		}
		for (const FString& Player : Players)
		{
			Target.SendTitle(Player, Kind, Text, Times[0], Times[1], Times[2]);
		}
		if (Players.Num() == 1)
		{
			return { Ok(McLang::Format(Key + TEXT(".single"), { Players[0] })) };
		}
		return { Ok(McLang::Format(Key + TEXT(".multiple"), { FString::FromInt(Players.Num()) })) };
	}

	FReplies RunAccess(const FString& Command, const TArray<FString>& Words, const FMcCommandSender& Sender, IMcCommandTarget& Target)
	{
		const FString& Name = Words[0];
		FMcServerAccess Access = Target.LoadAccess();
		if (Name == TEXT("op") || Name == TEXT("deop"))
		{
			if (Words.Num() != 2)
			{
				return Unknown(Command, Command.Len());
			}
			// A name that isn't online is still a name the files can hold, as the game allows for players it has seen.
			TArray<FString> Players;
			FString Error;
			if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
			{
				if (Words[1].StartsWith(TEXT("@")))
				{
					return { Fail(Error) };
				}
				Players = { Words[1] };
			}
			FReplies Replies;
			for (const FString& Player : Players)
			{
				const bool bIsOp = Access.FindOp(Player) != nullptr;
				if (Name == TEXT("op"))
				{
					if (bIsOp)
					{
						Replies.Add(Fail(McLang::Get(TEXT("commands.op.failed"))));
						continue;
					}
					FMcServerAccess::FOp Op;
					Op.Name = Player;
					Access.Ops.Add(Op);
					Replies.Add(Ok(McLang::Format(TEXT("commands.op.success"), { Player })));
				}
				else
				{
					if (!bIsOp)
					{
						Replies.Add(Fail(McLang::Get(TEXT("commands.deop.failed"))));
						continue;
					}
					Access.Ops.RemoveAll([&Player](const FMcServerAccess::FOp& Op) { return Op.Name.Equals(Player, ESearchCase::IgnoreCase); });
					Replies.Add(Ok(McLang::Format(TEXT("commands.deop.success"), { Player })));
				}
			}
			Target.SaveAccess(Access);
			return Replies;
		}

		// whitelist
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		const FString& Action = Words[1];
		if (Action == TEXT("on") || Action == TEXT("off"))
		{
			const bool bOn = Action == TEXT("on");
			if (Target.IsAllowlistOn() == bOn)
			{
				return { Fail(McLang::Get(bOn ? TEXT("commands.whitelist.alreadyOn") : TEXT("commands.whitelist.alreadyOff"))) };
			}
			Target.SetAllowlistOn(bOn);
			return { Ok(McLang::Get(bOn ? TEXT("commands.whitelist.enabled") : TEXT("commands.whitelist.disabled"))) };
		}
		if (Action == TEXT("list"))
		{
			if (Access.Allowlist.Num() == 0)
			{
				return { Ok(McLang::Get(TEXT("commands.whitelist.none"))) };
			}
			return { Ok(McLang::Format(TEXT("commands.whitelist.list"), { FString::FromInt(Access.Allowlist.Num()), JoinNames(Access.Allowlist) })) };
		}
		if (Action == TEXT("reload"))
		{
			// The files are read afresh at every join anyway.
			return { Ok(McLang::Get(TEXT("commands.whitelist.reloaded"))) };
		}
		if ((Action == TEXT("add") || Action == TEXT("remove")) && Words.Num() == 3)
		{
			const FString& Player = Words[2];
			const bool bListed = Access.IsAllowlisted(Player);
			if (Action == TEXT("add"))
			{
				if (bListed)
				{
					return { Fail(McLang::Get(TEXT("commands.whitelist.add.failed"))) };
				}
				Access.Allowlist.Add(Player);
				Target.SaveAccess(Access);
				return { Ok(McLang::Format(TEXT("commands.whitelist.add.success"), { Player })) };
			}
			if (!bListed)
			{
				return { Fail(McLang::Get(TEXT("commands.whitelist.remove.failed"))) };
			}
			Access.Allowlist.RemoveAll([&Player](const FString& Listed) { return Listed.Equals(Player, ESearchCase::IgnoreCase); });
			Target.SaveAccess(Access);
			return { Ok(McLang::Format(TEXT("commands.whitelist.remove.success"), { Player })) };
		}
		return BadArgument(Command);
	}
}

TArray<FMcCommandReply> McCommands::HandleChat(const FString& Line, const FMcCommandSender& Sender, IMcCommandTarget& Target)
{
	// Characters the game won't take in chat: the section sign (formatting codes) and control characters.
	FString Text;
	for (const TCHAR C : Line.Left(Line.StartsWith(TEXT("/")) ? MaxCommandLength : MaxChatLength))
	{
		if (C >= 32 && C != 127 && C != TEXT('§'))
		{
			Text.AppendChar(C);
		}
	}
	Text.TrimStartAndEndInline();
	if (Text.IsEmpty())
	{
		return {};
	}
	if (Text.StartsWith(TEXT("/")))
	{
		return Execute(Text.Mid(1), Sender, Target);
	}
	Target.Broadcast(McLang::Format(TEXT("chat.type.text"), { SenderName(Sender), Text }));
	return {};
}

TArray<FMcCommandReply> McCommands::Execute(const FString& Command, const FMcCommandSender& Sender, IMcCommandTarget& Target)
{
	TArray<FString> Words;
	Command.ParseIntoArrayWS(Words);
	if (Words.Num() == 0)
	{
		return Unknown(Command, 0);
	}
	// minecraft:tp is tp.
	FString Name = Words[0].ToLower();
	Name.RemoveFromStart(TEXT("minecraft:"));
	Words[0] = Name;
	const FCommandInfo* Info = FindCommand(Name);
	if (!Info || Info->Level > Sender.PermissionLevel)
	{
		return Unknown(Command, Words[0].Len());
	}

	if (Name == TEXT("help"))
	{
		FReplies Replies;
		if (Words.Num() >= 2)
		{
			const FCommandInfo* Asked = FindCommand(Words[1]);
			if (!Asked || Asked->Level > Sender.PermissionLevel)
			{
				return { Fail(McLang::Get(TEXT("commands.help.failed"))) };
			}
			return { Ok(Asked->Usage) };
		}
		for (const FCommandInfo& Each : Commands)
		{
			if (Each.Level <= Sender.PermissionLevel)
			{
				Replies.Add(Ok(Each.Usage));
			}
		}
		return Replies;
	}
	if (Name == TEXT("list"))
	{
		const TArray<FString> Players = Target.GetPlayerNames();
		return { Ok(McLang::Format(TEXT("commands.list.players"), { FString::FromInt(Players.Num()), FString::FromInt(Target.GetMaxPlayers()), JoinNames(Players) })) };
	}
	if (Name == TEXT("me"))
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		Target.Broadcast(McLang::Format(TEXT("chat.type.emote"), { SenderName(Sender), RestOf(Words, 1) }));
		return {};
	}
	if (Name == TEXT("say"))
	{
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		Target.Broadcast(McLang::Format(TEXT("chat.type.announcement"), { SenderName(Sender), RestOf(Words, 1) }));
		return {};
	}
	if (Name == TEXT("msg") || Name == TEXT("tell") || Name == TEXT("w"))
	{
		return RunMessage(Command, Words, Sender, Target);
	}
	if (Name == TEXT("tellraw"))
	{
		return RunTellraw(Command, Words, Sender, Target);
	}
	if (Name == TEXT("title"))
	{
		return RunTitle(Command, Words, Sender, Target);
	}
	if (Name == TEXT("attribute"))
	{
		return RunAttribute(Command, Words, Sender, Target);
	}
	if (Name == TEXT("loot"))
	{
		return RunLoot(Command, Words, Sender, Target);
	}
	if (Name == TEXT("seed"))
	{
		return { Ok(McLang::Format(TEXT("commands.seed.success"), { TEXT("[") + LexToString(Target.GetSeed()) + TEXT("]") })) };
	}
	if (Name == TEXT("gamemode"))
	{
		return RunGameMode(Command, Words, Sender, Target);
	}
	if (Name == TEXT("time"))
	{
		return RunTime(Command, Words, Target);
	}
	if (Name == TEXT("weather"))
	{
		return RunWeather(Command, Words, Target);
	}
	if (Name == TEXT("tp") || Name == TEXT("teleport"))
	{
		return RunTeleport(Command, Words, Sender, Target);
	}
	if (Name == TEXT("give"))
	{
		return RunGive(Command, Words, Sender, Target);
	}
	if (Name == TEXT("kill"))
	{
		return RunKill(Command, Words, Sender, Target);
	}
	if (Name == TEXT("kick"))
	{
		if (!Target.IsDedicated())
		{
			return { Fail(McLang::Get(TEXT("commands.kick.singleplayer.failed"))) };
		}
		if (Words.Num() < 2)
		{
			return Unknown(Command, Command.Len());
		}
		TArray<FString> Players;
		FString Error;
		if (!ResolvePlayers(Words[1], Sender, Target, Players, Error))
		{
			return { Fail(Error) };
		}
		const FString Reason = Words.Num() > 2 ? RestOf(Words, 2) : McLang::Get(TEXT("multiplayer.disconnect.kicked"));
		FReplies Replies;
		for (const FString& Player : Players)
		{
			Target.Kick(Player, Reason);
			Replies.Add(Ok(McLang::Format(TEXT("commands.kick.success"), { Player, Reason })));
		}
		return Replies;
	}
	if (Name == TEXT("op") || Name == TEXT("deop") || Name == TEXT("whitelist"))
	{
		return RunAccess(Command, Words, Sender, Target);
	}
	if (Name == TEXT("datapack"))
	{
		return RunDataPack(Command, Words, Sender, Target);
	}
	if (Name == TEXT("function"))
	{
		return RunFunction(Command, Words, Sender, Target);
	}
	if (Name == TEXT("reload"))
	{
		McData::Reload();
		return { Ok(McLang::Get(TEXT("commands.reload.success"))) };
	}
	if (Name == TEXT("playsound"))
	{
		return RunPlaySound(Command, Words, Sender, Target);
	}
	if (Name == TEXT("setblock"))
	{
		return RunSetBlock(Command, Words, Sender, Target);
	}
	if (Name == TEXT("fill"))
	{
		return RunFill(Command, Words, Sender, Target);
	}
	if (Name == TEXT("summon"))
	{
		return RunSummon(Command, Words, Sender, Target);
	}
	if (Name == TEXT("particle"))
	{
		return RunParticle(Command, Words, Sender, Target);
	}
	if (Name == TEXT("xp") || Name == TEXT("experience"))
	{
		return RunExperience(Command, Words, Sender, Target);
	}
	if (Name == TEXT("clear"))
	{
		return RunClear(Command, Words, Sender, Target);
	}
	if (Name == TEXT("advancement"))
	{
		return RunAdvancement(Command, Words, Sender, Target);
	}
	if (Name == TEXT("recipe"))
	{
		return RunRecipe(Command, Words, Sender, Target);
	}
	if (Name == TEXT("effect"))
	{
		return RunEffect(Command, Words, Sender, Target);
	}
	if (Name == TEXT("enchant"))
	{
		return RunEnchant(Command, Words, Sender, Target);
	}
	if (Name == TEXT("clone"))
	{
		return RunClone(Command, Words, Sender, Target);
	}
	if (Name == TEXT("setworldspawn"))
	{
		return RunSetWorldSpawn(Command, Words, Sender, Target);
	}
	if (Name == TEXT("spawnpoint"))
	{
		return RunSpawnPoint(Command, Words, Sender, Target);
	}
	if (Name == TEXT("worldborder"))
	{
		return RunWorldBorder(Command, Words, Target);
	}
	if (Name == TEXT("tick"))
	{
		return RunTick(Command, Words, Target);
	}
	if (Name == TEXT("damage"))
	{
		return RunDamage(Command, Words, Sender, Target);
	}
	if (Name == TEXT("rotate"))
	{
		return RunRotate(Command, Words, Sender, Target);
	}
	if (Name == TEXT("scoreboard"))
	{
		return RunScoreboard(Command, Words, Sender, Target);
	}
	if (Name == TEXT("execute"))
	{
		return RunExecute(Command, Words, Sender, Target);
	}
	if (Name == TEXT("locate"))
	{
		return RunLocate(Command, Words, Sender, Target);
	}
	if (Name == TEXT("swing"))
	{
		return RunSwing(Command, Words, Sender, Target);
	}
	if (Name == TEXT("spreadplayers"))
	{
		return RunSpreadPlayers(Command, Words, Sender, Target);
	}
	if (Name == TEXT("teammsg") || Name == TEXT("tm"))
	{
		return RunTeamMsg(Command, Words, Sender, Target);
	}
	if (Name == TEXT("stopwatch"))
	{
		return RunStopwatch(Command, Words);
	}
	if (Name == TEXT("item"))
	{
		return RunItem(Command, Words, Sender, Target);
	}
	if (Name == TEXT("team"))
	{
		return RunTeam(Command, Words, Sender, Target);
	}
	if (Name == TEXT("bossbar"))
	{
		return RunBossbar(Command, Words, Sender, Target);
	}
	if (Name == TEXT("trigger"))
	{
		return RunTrigger(Command, Words, Sender, Target);
	}
	if (Name == TEXT("data"))
	{
		LoadStorage(Target);
		FReplies Replies = RunData(Command, Words, Sender, Target);
		if (Words.Num() > 1 && Words[1] != TEXT("get"))
		{
			SaveStorage(Target);
		}
		return Replies;
	}
	if (Name == TEXT("forceload"))
	{
		return RunForceLoad(Command, Words, Sender, Target);
	}
	if (Name == TEXT("random"))
	{
		return RunRandom(Command, Words, Sender);
	}
	if (Name == TEXT("defaultgamemode"))
	{
		return RunDefaultGameMode(Command, Words, Target);
	}
	if (Name == TEXT("tag"))
	{
		return RunTag(Command, Words, Sender, Target);
	}
	if (Name == TEXT("stopsound"))
	{
		return RunStopSound(Command, Words, Sender, Target);
	}
	if (Name == TEXT("version"))
	{
		return RunVersion();
	}
	if (Name == TEXT("difficulty"))
	{
		return RunDifficulty(Command, Words, Target);
	}
	if (Name == TEXT("gamerule"))
	{
		return RunGameRule(Command, Words, Target);
	}
	return Unknown(Command, Words[0].Len());
}

TConstArrayView<McCommands::FGameRule> McCommands::GetGameRules()
{
	static const FGameRule Rules[] = {
		{ TEXT("allowEnteringNetherUsingPortals"), false, TEXT("true") },
		{ TEXT("allowFireTicksAwayFromPlayer"), false, TEXT("false") },
		{ TEXT("announceAdvancements"), false, TEXT("true") },
		{ TEXT("blockExplosionDropDecay"), false, TEXT("true") },
		{ TEXT("commandBlockOutput"), false, TEXT("true") },
		{ TEXT("commandBlocksEnabled"), false, TEXT("true") },
		{ TEXT("commandModificationBlockLimit"), true, TEXT("32768") },
		{ TEXT("disableElytraMovementCheck"), false, TEXT("false") },
		{ TEXT("disablePlayerMovementCheck"), false, TEXT("false") },
		{ TEXT("disableRaids"), false, TEXT("false") },
		{ TEXT("doDaylightCycle"), false, TEXT("true") },
		{ TEXT("doEntityDrops"), false, TEXT("true") },
		{ TEXT("doFireTick"), false, TEXT("true") },
		{ TEXT("doImmediateRespawn"), false, TEXT("false") },
		{ TEXT("doInsomnia"), false, TEXT("true") },
		{ TEXT("doLimitedCrafting"), false, TEXT("false") },
		{ TEXT("doMobLoot"), false, TEXT("true") },
		{ TEXT("doMobSpawning"), false, TEXT("true") },
		{ TEXT("doPatrolSpawning"), false, TEXT("true") },
		{ TEXT("doTileDrops"), false, TEXT("true") },
		{ TEXT("doTraderSpawning"), false, TEXT("true") },
		{ TEXT("doVinesSpread"), false, TEXT("true") },
		{ TEXT("doWardenSpawning"), false, TEXT("true") },
		{ TEXT("doWeatherCycle"), false, TEXT("true") },
		{ TEXT("drowningDamage"), false, TEXT("true") },
		{ TEXT("enableCommandBlocks"), false, TEXT("true") },
		{ TEXT("enderPearlsVanishOnDeath"), false, TEXT("true") },
		{ TEXT("entitiesWithPassengersCanUsePortals"), false, TEXT("false") },
		{ TEXT("fallDamage"), false, TEXT("true") },
		{ TEXT("fireDamage"), false, TEXT("true") },
		{ TEXT("forgiveDeadPlayers"), false, TEXT("true") },
		{ TEXT("freezeDamage"), false, TEXT("true") },
		{ TEXT("globalSoundEvents"), false, TEXT("true") },
		{ TEXT("keepInventory"), false, TEXT("false") },
		{ TEXT("lavaSourceConversion"), false, TEXT("false") },
		{ TEXT("locatorBar"), false, TEXT("true") },
		{ TEXT("logAdminCommands"), false, TEXT("true") },
		{ TEXT("maxCommandChainLength"), true, TEXT("65536") },
		{ TEXT("maxCommandForkCount"), true, TEXT("65536") },
		{ TEXT("maxEntityCramming"), true, TEXT("24") },
		{ TEXT("minecartMaxSpeed"), true, TEXT("8") },
		{ TEXT("mobExplosionDropDecay"), false, TEXT("true") },
		{ TEXT("mobGriefing"), false, TEXT("true") },
		{ TEXT("naturalRegeneration"), false, TEXT("true") },
		{ TEXT("playersNetherPortalCreativeDelay"), true, TEXT("0") },
		{ TEXT("playersNetherPortalDefaultDelay"), true, TEXT("80") },
		{ TEXT("playersSleepingPercentage"), true, TEXT("100") },
		{ TEXT("projectilesCanBreakBlocks"), false, TEXT("true") },
		{ TEXT("pvp"), false, TEXT("true") },
		{ TEXT("randomTickSpeed"), true, TEXT("3") },
		{ TEXT("reducedDebugInfo"), false, TEXT("false") },
		{ TEXT("sendCommandFeedback"), false, TEXT("true") },
		{ TEXT("showDeathMessages"), false, TEXT("true") },
		{ TEXT("snowAccumulationHeight"), true, TEXT("1") },
		{ TEXT("spawnChunkRadius"), true, TEXT("2") },
		{ TEXT("spawnerBlocksWork"), false, TEXT("true") },
		{ TEXT("spawnMonsters"), false, TEXT("true") },
		{ TEXT("spawnRadius"), true, TEXT("10") },
		{ TEXT("spectatorsGenerateChunks"), false, TEXT("true") },
		{ TEXT("tntExplodes"), false, TEXT("true") },
		{ TEXT("tntExplosionDropDecay"), false, TEXT("false") },
		{ TEXT("universalAnger"), false, TEXT("false") },
		{ TEXT("waterSourceConversion"), false, TEXT("true") },
	};
	return Rules;
}

const McCommands::FGameRule* McCommands::FindGameRule(const FString& Name)
{
	for (const FGameRule& Rule : GetGameRules())
	{
		if (Name == Rule.Name)
		{
			return &Rule;
		}
	}
	return nullptr;
}

int32 McCommands::GetPermissionLevel(const FString& Player, bool bHost, const FMcServerAccess& Access)
{
	if (bHost)
	{
		return 4;
	}
	const FMcServerAccess::FOp* Op = Access.FindOp(Player);
	return Op ? Op->Level : 0;
}

int32 McCommands::ParseTime(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return -1;
	}
	FString Number = Text;
	double Scale = 1.0;
	const TCHAR Unit = Text[Text.Len() - 1];
	if (Unit == 'd' || Unit == 's' || Unit == 't')
	{
		Number = Text.LeftChop(1);
		Scale = Unit == 'd' ? 24000.0 : Unit == 's' ? 20.0 : 1.0;
	}
	if (Number.IsEmpty() || !Number.IsNumeric() || Number.StartsWith(TEXT("-")))
	{
		return -1;
	}
	return FMath::RoundToInt(FCString::Atod(*Number) * Scale);
}

TArray<FString> McCommands::GetFunctionTag(const FString& Tag)
{
	TArray<FString> Ids;
	CollectTagFunctions(Tag.StartsWith(TEXT("#")) ? Tag.RightChop(1) : Tag, Ids, 0);
	return Ids;
}

TArray<FMcCommandReply> McCommands::RunFunctions(const TArray<FString>& Ids, const FMcCommandSender& Sender, IMcCommandTarget& Target)
{
	FReplies Replies;
	for (const FString& Id : Ids)
	{
		RunFunctionLines(Id, Sender, Target, Replies, 0);
	}
	return Replies;
}

FString McCommands::BossbarLines(const FString& Player)
{
	TArray<FString> Keys;
	Bossbars().GetKeys(Keys);
	Keys.Sort();
	TArray<FString> Lines;
	for (const FString& Key : Keys)
	{
		const FBossbar& Bar = Bossbars()[Key];
		if (Bar.bVisible && Bar.Players.Contains(Player))
		{
			const float Fraction = Bar.Max > 0 ? FMath::Clamp(float(Bar.Value) / float(Bar.Max), 0.0f, 1.0f) : 0.0f;
			Lines.Add(Bar.Name + TEXT("\t") + FString::SanitizeFloat(Fraction) + TEXT("\t") + Bar.Color + TEXT("\t") + Bar.Style);
		}
	}
	return FString::Join(Lines, TEXT("\n"));
}

FMcWorldBorderState& McCommands::GetWorldBorder()
{
	static FMcWorldBorderState State;
	return State;
}
