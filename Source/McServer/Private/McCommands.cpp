#include "McCommands.h"
#include "McLang.h"
#include "McCache.h"
#include "McMob.h"
#include "McVehicle.h"

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
		{ TEXT("clear"), 2, TEXT("/clear [<targets>] [<item>] [<maxCount>]") },
		{ TEXT("deop"), 3, TEXT("/deop <targets>") },
		{ TEXT("difficulty"), 2, TEXT("/difficulty [peaceful|easy|normal|hard]") },
		{ TEXT("effect"), 2, TEXT("/effect (clear|give) ...") },
		{ TEXT("enchant"), 2, TEXT("/enchant <targets> <enchantment> [<level>]") },
		{ TEXT("experience"), 2, TEXT("/experience (add|query|set) ...") },
		{ TEXT("fill"), 2, TEXT("/fill <from> <to> <block> [destroy|hollow|keep|outline|replace]") },
		{ TEXT("gamemode"), 2, TEXT("/gamemode <gamemode> [<target>]") },
		{ TEXT("gamerule"), 2, TEXT("/gamerule <rule> [<value>]") },
		{ TEXT("give"), 2, TEXT("/give <targets> <item> [<count>]") },
		{ TEXT("help"), 0, TEXT("/help [<command>]") },
		{ TEXT("kick"), 3, TEXT("/kick <targets> [<reason>]") },
		{ TEXT("kill"), 2, TEXT("/kill [<targets>]") },
		{ TEXT("list"), 0, TEXT("/list") },
		{ TEXT("me"), 0, TEXT("/me <action>") },
		{ TEXT("msg"), 0, TEXT("/msg <targets> <message>") },
		{ TEXT("op"), 3, TEXT("/op <targets>") },
		{ TEXT("say"), 2, TEXT("/say <message>") },
		{ TEXT("seed"), 2, TEXT("/seed") },
		{ TEXT("setblock"), 2, TEXT("/setblock <pos> <block> [destroy|keep|replace]") },
		{ TEXT("summon"), 2, TEXT("/summon <entity> [<pos>]") },
		{ TEXT("teleport"), 2, TEXT("/teleport <location> | <destination> | <targets> (<location>|<destination>)") },
		{ TEXT("tell"), 0, TEXT("/tell -> msg") },
		{ TEXT("time"), 2, TEXT("/time (add|query|set) ...") },
		{ TEXT("tp"), 2, TEXT("/tp -> teleport") },
		{ TEXT("w"), 0, TEXT("/w -> msg") },
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
	for (const TCHAR C : Line.Left(MaxChatLength))
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
	if (Name == TEXT("xp") || Name == TEXT("experience"))
	{
		return RunExperience(Command, Words, Sender, Target);
	}
	if (Name == TEXT("clear"))
	{
		return RunClear(Command, Words, Sender, Target);
	}
	if (Name == TEXT("effect"))
	{
		return RunEffect(Command, Words, Sender, Target);
	}
	if (Name == TEXT("enchant"))
	{
		return RunEnchant(Command, Words, Sender, Target);
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
