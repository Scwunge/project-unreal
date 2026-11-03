#include "McCommands.h"
#include "McLang.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// A world of named players and nothing else, recording what the commands did.
	class FFakeTarget : public IMcCommandTarget
	{
	public:
		struct FPlayer
		{
			FString Name;
			bool bCreative = false;
			FVector Feet = FVector::ZeroVector;
			bool bDead = false;
			TArray<TPair<FName, int32>> Given;
			TArray<FMcCommandReply> Told;
		};
		TArray<FPlayer> Players;
		TArray<FString> Broadcasts;
		TArray<FString> Kicked;
		double DayTime = 0.0;
		EMcWeather Weather = EMcWeather::Clear;
		FMcServerAccess Access;
		bool bAllowlist = false;
		bool bDedicated = false;

		FPlayer* Find(const FString& Name)
		{
			return Players.FindByPredicate([&](const FPlayer& P) { return P.Name.Equals(Name, ESearchCase::IgnoreCase); });
		}
		const FPlayer* Find(const FString& Name) const { return const_cast<FFakeTarget*>(this)->Find(Name); }

		virtual TArray<FString> GetPlayerNames() const override
		{
			TArray<FString> Names;
			for (const FPlayer& P : Players)
			{
				Names.Add(P.Name);
			}
			return Names;
		}
		virtual int32 GetMaxPlayers() const override { return 20; }
		virtual bool IsDedicated() const override { return bDedicated; }
		virtual void SendTo(const FString& Player, const FMcCommandReply& Line) override
		{
			if (FPlayer* P = Find(Player))
			{
				P->Told.Add(Line);
			}
		}
		virtual void Broadcast(const FString& Line) override { Broadcasts.Add(Line); }
		virtual bool IsCreative(const FString& Player) const override { return Find(Player) && Find(Player)->bCreative; }
		virtual void SetCreative(const FString& Player, bool bCreative) override
		{
			if (FPlayer* P = Find(Player))
			{
				P->bCreative = bCreative;
			}
		}
		virtual bool GetPosition(const FString& Player, FVector& OutFeet) const override
		{
			const FPlayer* P = Find(Player);
			if (P)
			{
				OutFeet = P->Feet;
			}
			return P != nullptr;
		}
		virtual void Teleport(const FString& Player, const FVector& Feet) override
		{
			if (FPlayer* P = Find(Player))
			{
				P->Feet = Feet;
			}
		}
		virtual void Kill(const FString& Player) override
		{
			if (FPlayer* P = Find(Player))
			{
				P->bDead = true;
			}
		}
		virtual void Kick(const FString& Player, const FString& Reason) override { Kicked.Add(Player); }
		virtual int32 GetMaxStackSize(FName Item) const override
		{
			return Item == FName(TEXT("minecraft:diamond")) ? 64 : Item == FName(TEXT("minecraft:diamond_sword")) ? 1 : 0;
		}
		virtual FString GetItemName(FName Item) const override { return Item == FName(TEXT("minecraft:diamond")) ? TEXT("Diamond") : TEXT("Diamond Sword"); }
		virtual void Give(const FString& Player, FName Item, int32 Count) override
		{
			if (FPlayer* P = Find(Player))
			{
				P->Given.Add({ Item, Count });
			}
		}
		virtual double GetDayTime() const override { return DayTime; }
		virtual void SetDayTime(double Ticks) override { DayTime = Ticks; }
		virtual double GetGameTime() const override { return 12345.0; }
		virtual void SetWeather(EMcWeather InWeather, int32 DurationTicks) override { Weather = InWeather; }
		virtual int64 GetSeed() const override { return -42; }
		virtual FMcServerAccess LoadAccess() const override { return Access; }
		virtual void SaveAccess(const FMcServerAccess& InAccess) override { Access = InAccess; }
		virtual bool IsAllowlistOn() const override { return bAllowlist; }
		virtual void SetAllowlistOn(bool bOn) override { bAllowlist = bOn; }

		int32 Difficulty = 2;
		TMap<FString, FString> Rules;
		// A world of air from y 0 to 15 near the origin; nothing is loaded beyond.
		TMap<FIntVector, FMcStateId> Blocks;
		TArray<TPair<FName, FVector>> Summoned;
		int32 XpLevel = 5;
		int32 XpPoints = 3;

		virtual int32 GetDifficulty() const override { return Difficulty; }
		virtual void SetDifficulty(int32 InDifficulty) override { Difficulty = InDifficulty; }
		virtual void PlaySoundAt(FName Event, const FVector& Position, float Volume, float Pitch) override { Played = Event.ToString() + FString::Printf(TEXT(" %.1f %.1f %.1f %.1f"), Position.X, Volume, Pitch, Position.Z); }
		FString Played;
		virtual FString GetGameRule(const FString& Rule) const override { const FString* Value = Rules.Find(Rule); return Value ? *Value : FString(); }
		virtual void SetGameRule(const FString& Rule, const FString& Value) override { Rules.Add(Rule, Value); }
		virtual bool GetBlock(const FIntVector& Pos, FMcStateId& OutState) const override
		{
			if (FMath::Abs(Pos.X) > 32 || FMath::Abs(Pos.Z) > 32 || Pos.Y < 0 || Pos.Y > 15)
			{
				return false;
			}
			const FMcStateId* State = Blocks.Find(Pos);
			OutState = State ? *State : 0;
			return true;
		}
		virtual void SetBlock(const FIntVector& Pos, FMcStateId State) override { Blocks.Add(Pos, State); }
		virtual bool Summon(FName Kind, const FVector& Feet) override { Summoned.Add({ Kind, Feet }); return true; }
		virtual void GetXp(const FString& Player, int32& OutLevel, int32& OutPoints) const override { OutLevel = XpLevel; OutPoints = XpPoints; }
		virtual void AddXp(const FString& Player, int32 Amount, bool bLevels) override { (bLevels ? XpLevel : XpPoints) += Amount; }
		virtual bool SetXp(const FString& Player, int32 Amount, bool bLevels) override
		{
			if (!bLevels && Amount > 17)
			{
				return false;
			}
			(bLevels ? XpLevel : XpPoints) = Amount;
			return true;
		}
		TMap<FName, TPair<int32, int32>> Effects;
		FName HeldItem;
		TMap<FName, int32> HeldEnchantments;
		virtual bool IsEffect(FName Effect) const override { return Effect == TEXT("speed") || Effect == TEXT("poison"); }
		virtual bool GiveEffect(const FString& Player, FName Effect, int32 Amplifier, int32 DurationTicks) override
		{
			if (TPair<int32, int32>* Have = Effects.Find(Effect))
			{
				if (Amplifier < Have->Key)
				{
					return false;
				}
			}
			Effects.Add(Effect, TPair<int32, int32>(Amplifier, DurationTicks));
			return true;
		}
		TSet<FString> Advancements;
		virtual int32 SetAdvancement(const FString& Player, const FString& Id, bool bGrant, FString& OutTitle) override
		{
			if (Id != TEXT("story/mine_stone"))
			{
				return -1;
			}
			OutTitle = TEXT("[Stone Age]");
			const FString Key = Player + Id;
			if (!bGrant)
			{
				return Advancements.Remove(Key) > 0 ? 1 : 0;
			}
			if (Advancements.Contains(Key))
			{
				return 0;
			}
			Advancements.Add(Key);
			return 1;
		}
		virtual int32 ClearEffects(const FString& Player, FName Effect) override
		{
			const int32 Had = Effect.IsNone() ? Effects.Num() : Effects.Contains(Effect) ? 1 : 0;
			if (Effect.IsNone())
			{
				Effects.Reset();
			}
			else
			{
				Effects.Remove(Effect);
			}
			return Had;
		}
		virtual int32 GetEnchantmentMaxLevel(FName Enchantment) const override { return Enchantment == TEXT("minecraft:sharpness") ? 5 : Enchantment == TEXT("minecraft:silk_touch") ? 1 : 0; }
		virtual EEnchantResult Enchant(const FString& Player, FName Enchantment, int32 Level) override
		{
			if (HeldItem.IsNone())
			{
				return EEnchantResult::NoItem;
			}
			if (Enchantment != TEXT("minecraft:sharpness"))
			{
				return EEnchantResult::Unsupported;
			}
			HeldEnchantments.Add(Enchantment, Level);
			return EEnchantResult::Done;
		}
		virtual int32 Clear(const FString& Player, FName Item, int32 MaxCount, bool bTest) override
		{
			FPlayer* P = Find(Player);
			int32 Found = 0;
			for (TPair<FName, int32>& Given : P->Given)
			{
				if (Item.IsNone() || Given.Key == Item)
				{
					const int32 Take = MaxCount < 0 ? Given.Value : FMath::Min(Given.Value, MaxCount - Found);
					Found += bTest ? Given.Value : Take;
					Given.Value -= bTest ? 0 : Take;
				}
			}
			return Found;
		}
	};

	FMcCommandSender Op(const FString& Name, int32 Level = 4)
	{
		FMcCommandSender Sender;
		Sender.Name = Name;
		Sender.PermissionLevel = Level;
		return Sender;
	}

	bool AnyError(const TArray<FMcCommandReply>& Replies)
	{
		return Replies.ContainsByPredicate([](const FMcCommandReply& R) { return R.bError; });
	}

	FString Joined(const TArray<FMcCommandReply>& Replies)
	{
		FString Out;
		for (const FMcCommandReply& R : Replies)
		{
			Out += R.Text + TEXT("\n");
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCommandsTest, "McClient.McServer.Commands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcCommandsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("the jar's text is read"), McLang::Get(TEXT("chat.type.text")), FString(TEXT("<%s> %s")));
	TestEqual(TEXT("numbered arguments"), McLang::FormatPattern(TEXT("%2$s then %1$s, 100%%"), { TEXT("a"), TEXT("b") }), FString(TEXT("b then a, 100%")));

	TestEqual(TEXT("ticks"), McCommands::ParseTime(TEXT("100")), 100);
	TestEqual(TEXT("days"), McCommands::ParseTime(TEXT("2d")), 48000);
	TestEqual(TEXT("seconds"), McCommands::ParseTime(TEXT("5s")), 100);
	TestEqual(TEXT("not a time"), McCommands::ParseTime(TEXT("soon")), -1);

	FMcServerAccess Access;
	Access.Ops.Add({ TEXT("Alex"), 2, false });
	TestEqual(TEXT("the host has every level"), McCommands::GetPermissionLevel(TEXT("Steve"), true, Access), 4);
	TestEqual(TEXT("an op has theirs"), McCommands::GetPermissionLevel(TEXT("alex"), false, Access), 2);
	TestEqual(TEXT("anyone else has none"), McCommands::GetPermissionLevel(TEXT("Steve"), false, Access), 0);

	FFakeTarget Target;
	Target.Players.Add({ TEXT("Steve") });
	Target.Players.Add({ TEXT("Alex") });
	Target.Players[1].Feet = FVector(10, 64, -3);

	// Plain text is said to everyone.
	TestEqual(TEXT("text gets no reply"), McCommands::HandleChat(TEXT("  hello  "), Op(TEXT("Steve"), 0), Target).Num(), 0);
	TestEqual(TEXT("text is broadcast"), Target.Broadcasts.Num() > 0 ? Target.Broadcasts.Last() : FString(), FString(TEXT("<Steve> hello")));
	TestEqual(TEXT("blank lines say nothing"), McCommands::HandleChat(TEXT("   "), Op(TEXT("Steve"), 0), Target).Num(), 0);
	TestEqual(TEXT("formatting codes are dropped"), (McCommands::HandleChat(TEXT("a§cb"), Op(TEXT("Steve"), 0), Target), Target.Broadcasts.Last()), FString(TEXT("<Steve> acb")));

	// A command the sender can't use is unknown to them.
	TArray<FMcCommandReply> Replies = McCommands::HandleChat(TEXT("/gamemode creative"), Op(TEXT("Steve"), 0), Target);
	TestTrue(TEXT("no permission is an error"), AnyError(Replies));
	TestFalse(TEXT("and changes nothing"), Target.Find(TEXT("Steve"))->bCreative);
	TestTrue(TEXT("unknown command"), Joined(McCommands::HandleChat(TEXT("/fly"), Op(TEXT("Steve")), Target)).Contains(TEXT("<--[HERE]")));

	// /gamemode
	Replies = McCommands::HandleChat(TEXT("/gamemode creative"), Op(TEXT("Steve")), Target);
	TestFalse(TEXT("gamemode ok"), AnyError(Replies));
	TestTrue(TEXT("gamemode creative"), Target.Find(TEXT("Steve"))->bCreative);
	McCommands::HandleChat(TEXT("/gamemode survival @a"), Op(TEXT("Steve")), Target);
	TestFalse(TEXT("gamemode @a"), Target.Find(TEXT("Steve"))->bCreative || Target.Find(TEXT("Alex"))->bCreative);

	// /playsound: the event without its namespace, the position (the sender's feet by default), volume and pitch
	Replies = McCommands::HandleChat(TEXT("/playsound minecraft:zz.add master @s 4 70 5 2 1.5"), Op(TEXT("Steve")), Target);
	TestFalse(TEXT("playsound ok"), AnyError(Replies));
	TestEqual(TEXT("playsound passes its arguments on"), Target.Played, FString(TEXT("zz.add 4.5 2.0 1.5 5.5")));
	TestTrue(TEXT("playsound with a bad source"), AnyError(McCommands::HandleChat(TEXT("/playsound zz.add nowhere"), Op(TEXT("Steve")), Target)));

	// /time
	McCommands::HandleChat(TEXT("/time set night"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("time set night"), Target.DayTime, 13000.0);
	McCommands::HandleChat(TEXT("/time add 1d"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("time add"), Target.DayTime, 37000.0);
	TestTrue(TEXT("time set nonsense"), AnyError(McCommands::HandleChat(TEXT("/time set later"), Op(TEXT("Steve")), Target)));

	// /weather
	McCommands::HandleChat(TEXT("/weather thunder"), Op(TEXT("Steve")), Target);
	TestTrue(TEXT("weather thunder"), Target.Weather == EMcWeather::Thunder);

	// /give: the count, an unknown item, and a player who isn't on.
	McCommands::HandleChat(TEXT("/give Alex minecraft:diamond 70"), Op(TEXT("Steve")), Target);
	const FFakeTarget::FPlayer& Alex = *Target.Find(TEXT("Alex"));
	TestTrue(TEXT("give"), Alex.Given.Num() == 1 && Alex.Given[0].Value == 70);
	McCommands::HandleChat(TEXT("/give @s diamond"), Op(TEXT("Steve")), Target);
	TestTrue(TEXT("give without the namespace"), Target.Find(TEXT("Steve"))->Given.Num() == 1 && Target.Find(TEXT("Steve"))->Given[0].Key == FName(TEXT("minecraft:diamond")));
	TestTrue(TEXT("give nothing"), AnyError(McCommands::HandleChat(TEXT("/give Alex minecraft:unobtainium"), Op(TEXT("Steve")), Target)));
	TestTrue(TEXT("give nobody"), AnyError(McCommands::HandleChat(TEXT("/give Herobrine minecraft:diamond"), Op(TEXT("Steve")), Target)));

	// /tp: to a player, to coordinates (whole x and z land mid-block), and relative.
	McCommands::HandleChat(TEXT("/tp Alex"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("tp to a player"), Target.Find(TEXT("Steve"))->Feet, FVector(10, 64, -3));
	McCommands::HandleChat(TEXT("/tp 0 70 0"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("tp to coordinates"), Target.Find(TEXT("Steve"))->Feet, FVector(0.5, 70, 0.5));
	McCommands::HandleChat(TEXT("/teleport ~ ~5 ~-1"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("tp relative"), Target.Find(TEXT("Steve"))->Feet, FVector(0.5, 75, -0.5));

	// /kill
	McCommands::HandleChat(TEXT("/kill Alex"), Op(TEXT("Steve")), Target);
	TestTrue(TEXT("kill"), Target.Find(TEXT("Alex"))->bDead);
	TestFalse(TEXT("kill only them"), Target.Find(TEXT("Steve"))->bDead);

	// /list and /seed are for everyone (the seed needs cheats, but the host has them).
	TestTrue(TEXT("list"), Joined(McCommands::HandleChat(TEXT("/list"), Op(TEXT("Steve"), 0), Target)).Contains(TEXT("Steve, Alex")));
	TestTrue(TEXT("seed"), Joined(McCommands::HandleChat(TEXT("/seed"), Op(TEXT("Steve")), Target)).Contains(TEXT("-42")));

	// /msg goes to the one player only.
	const int32 Broadcast = Target.Broadcasts.Num();
	McCommands::HandleChat(TEXT("/msg Alex psst"), Op(TEXT("Steve"), 0), Target);
	TestEqual(TEXT("msg is not broadcast"), Target.Broadcasts.Num(), Broadcast);
	TestTrue(TEXT("msg reaches them"), Target.Find(TEXT("Alex"))->Told.ContainsByPredicate([](const FMcCommandReply& R) { return R.Text.Contains(TEXT("psst")); }));

	// /op and /deop, /whitelist.
	McCommands::HandleChat(TEXT("/op Alex"), Op(TEXT("Steve")), Target);
	TestNotNull(TEXT("op"), Target.Access.FindOp(TEXT("Alex")));
	McCommands::HandleChat(TEXT("/deop Alex"), Op(TEXT("Steve")), Target);
	TestNull(TEXT("deop"), Target.Access.FindOp(TEXT("Alex")));
	McCommands::HandleChat(TEXT("/whitelist add Notch"), Op(TEXT("Steve")), Target);
	TestTrue(TEXT("whitelist add"), Target.Access.IsAllowlisted(TEXT("Notch")));
	McCommands::HandleChat(TEXT("/whitelist on"), Op(TEXT("Steve")), Target);
	TestTrue(TEXT("whitelist on"), Target.bAllowlist);

	// /setblock and /fill.
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (TestTrue(TEXT("blocks load"), Registry.EnsureLoaded()))
	{
		const FMcStateId Stone = Registry.ParseState(TEXT("minecraft:stone")).Get(0);
		const FMcStateId Dirt = Registry.ParseState(TEXT("minecraft:dirt")).Get(0);
		FMcStateId Got = 0;
		TestFalse(TEXT("setblock"), AnyError(McCommands::HandleChat(TEXT("/setblock 1 2 3 stone"), Op(TEXT("Steve")), Target)));
		TestTrue(TEXT("setblock placed"), Target.GetBlock(FIntVector(1, 2, 3), Got) && Got == Stone);
		TestTrue(TEXT("setblock the same again"), AnyError(McCommands::HandleChat(TEXT("/setblock 1 2 3 minecraft:stone"), Op(TEXT("Steve")), Target)));
		TestTrue(TEXT("setblock keep"), AnyError(McCommands::HandleChat(TEXT("/setblock 1 2 3 dirt keep"), Op(TEXT("Steve")), Target)));
		TestTrue(TEXT("setblock unloaded"), Joined(McCommands::HandleChat(TEXT("/setblock 100 2 3 dirt"), Op(TEXT("Steve")), Target)).Contains(TEXT("not loaded")));
		TestTrue(TEXT("setblock with properties"), !AnyError(McCommands::HandleChat(TEXT("/setblock 0 1 0 oak_stairs[facing=east]"), Op(TEXT("Steve")), Target)));
		TestTrue(TEXT("setblock nonsense"), AnyError(McCommands::HandleChat(TEXT("/setblock 0 1 0 not_a_block"), Op(TEXT("Steve")), Target)));

		TestTrue(TEXT("fill"), Joined(McCommands::HandleChat(TEXT("/fill 0 5 0 2 7 2 dirt"), Op(TEXT("Steve")), Target)).Contains(TEXT("27")));
		TestTrue(TEXT("fill hollow"), Joined(McCommands::HandleChat(TEXT("/fill 0 5 0 2 7 2 stone hollow"), Op(TEXT("Steve")), Target)).Contains(TEXT("27")));
		TestTrue(TEXT("hollow empties the middle"), Target.GetBlock(FIntVector(1, 6, 1), Got) && Registry.IsAir(Got));
		TestTrue(TEXT("fill replace a filter"), Joined(McCommands::HandleChat(TEXT("/fill 0 5 0 2 7 2 dirt replace stone"), Op(TEXT("Steve")), Target)).Contains(TEXT("26")));
		TestTrue(TEXT("fill too big"), AnyError(McCommands::HandleChat(TEXT("/fill 0 0 0 200 15 200 dirt"), Op(TEXT("Steve")), Target)));
		McCommands::HandleChat(TEXT("/gamerule commandModificationBlockLimit 8"), Op(TEXT("Steve")), Target);
		TestTrue(TEXT("the limit is the rule's"), AnyError(McCommands::HandleChat(TEXT("/fill 0 5 0 2 7 2 stone"), Op(TEXT("Steve")), Target)));
		(void)Dirt;
	}

	// /gamerule
	TestTrue(TEXT("gamerule query"), Joined(McCommands::HandleChat(TEXT("/gamerule keepInventory"), Op(TEXT("Steve")), Target)).Contains(TEXT("false")));
	TestFalse(TEXT("gamerule set"), AnyError(McCommands::HandleChat(TEXT("/gamerule keepInventory true"), Op(TEXT("Steve")), Target)));
	TestEqual(TEXT("gamerule kept"), Target.GetGameRule(TEXT("keepInventory")), FString(TEXT("true")));
	TestTrue(TEXT("gamerule wrong type"), AnyError(McCommands::HandleChat(TEXT("/gamerule randomTickSpeed fast"), Op(TEXT("Steve")), Target)));
	TestTrue(TEXT("gamerule unknown"), AnyError(McCommands::HandleChat(TEXT("/gamerule flying true"), Op(TEXT("Steve")), Target)));

	// /difficulty and /summon
	McCommands::HandleChat(TEXT("/difficulty peaceful"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("difficulty"), Target.Difficulty, 0);
	TestTrue(TEXT("no monsters in peaceful"), AnyError(McCommands::HandleChat(TEXT("/summon zombie"), Op(TEXT("Steve")), Target)));
	TestFalse(TEXT("summon a pig"), AnyError(McCommands::HandleChat(TEXT("/summon pig 3 4 5"), Op(TEXT("Steve")), Target)));
	TestTrue(TEXT("summoned where asked"), Target.Summoned.Num() == 1 && Target.Summoned[0].Value == FVector(3.5, 4, 5.5));
	TestTrue(TEXT("summon nothing"), AnyError(McCommands::HandleChat(TEXT("/summon dragon_of_doom"), Op(TEXT("Steve")), Target)));

	// /xp and /clear
	McCommands::HandleChat(TEXT("/xp add Steve 3 levels"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("xp add levels"), Target.XpLevel, 8);
	TestTrue(TEXT("xp query"), Joined(McCommands::HandleChat(TEXT("/xp query Steve points"), Op(TEXT("Steve")), Target)).Contains(TEXT("has 3 experience points")));
	TestTrue(TEXT("xp set beyond the level"), AnyError(McCommands::HandleChat(TEXT("/xp set Steve 500"), Op(TEXT("Steve")), Target)));
	TestTrue(TEXT("clear counts"), Joined(McCommands::HandleChat(TEXT("/clear Alex diamond 0"), Op(TEXT("Steve")), Target)).Contains(TEXT("Found 70")));
	TestTrue(TEXT("clear some"), Joined(McCommands::HandleChat(TEXT("/clear Alex diamond 10"), Op(TEXT("Steve")), Target)).Contains(TEXT("Removed 10")));
	TestTrue(TEXT("clear the rest"), Joined(McCommands::HandleChat(TEXT("/clear Alex"), Op(TEXT("Steve")), Target)).Contains(TEXT("Removed 60")));
	TestTrue(TEXT("nothing left"), AnyError(McCommands::HandleChat(TEXT("/clear Alex"), Op(TEXT("Steve")), Target)));

	// /effect and /enchant
	TestTrue(TEXT("effect give"), Joined(McCommands::HandleChat(TEXT("/effect give Steve speed 10 1"), Op(TEXT("Steve")), Target)).Contains(TEXT("Applied effect Speed to Steve")));
	TestEqual(TEXT("it lasts 10 seconds at level II"), Target.Effects.FindRef(TEXT("speed")), TPair<int32, int32>(1, 200));
	TestTrue(TEXT("a weaker one does not replace it"), AnyError(McCommands::HandleChat(TEXT("/effect give Steve speed 99 0"), Op(TEXT("Steve")), Target)));
	TestTrue(TEXT("no such effect"), AnyError(McCommands::HandleChat(TEXT("/effect give Steve flying"), Op(TEXT("Steve")), Target)));
	TestTrue(TEXT("amplifier past 255"), AnyError(McCommands::HandleChat(TEXT("/effect give Steve poison 5 256"), Op(TEXT("Steve")), Target)));
	McCommands::HandleChat(TEXT("/effect give Steve minecraft:poison infinite"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("infinite lasts"), Target.Effects.FindRef(TEXT("poison")).Value, 1000000000);
	TestTrue(TEXT("clear one"), Joined(McCommands::HandleChat(TEXT("/effect clear Steve speed"), Op(TEXT("Steve")), Target)).Contains(TEXT("Removed effect Speed from Steve")));
	TestTrue(TEXT("clear all"), Joined(McCommands::HandleChat(TEXT("/effect clear Steve"), Op(TEXT("Steve")), Target)).Contains(TEXT("Removed every effect from Steve")));
	TestTrue(TEXT("nothing to clear"), AnyError(McCommands::HandleChat(TEXT("/effect clear Steve"), Op(TEXT("Steve")), Target)));

	// /advancement
	TestTrue(TEXT("grant"), Joined(McCommands::HandleChat(TEXT("/advancement grant Steve only story/mine_stone"), Op(TEXT("Steve")), Target)).Contains(TEXT("Granted the advancement [Stone Age] to Steve")));
	TestTrue(TEXT("already made"), AnyError(McCommands::HandleChat(TEXT("/advancement grant Steve only story/mine_stone"), Op(TEXT("Steve")), Target)));
	TestTrue(TEXT("revoke"), Joined(McCommands::HandleChat(TEXT("/advancement revoke Steve only story/mine_stone"), Op(TEXT("Steve")), Target)).Contains(TEXT("Revoked the advancement [Stone Age] from Steve")));
	TestTrue(TEXT("unknown"), Joined(McCommands::HandleChat(TEXT("/advancement grant Steve only story/nothing"), Op(TEXT("Steve")), Target)).Contains(TEXT("Unknown advancement")));
	TestTrue(TEXT("cheats only"), AnyError(McCommands::HandleChat(TEXT("/effect give Steve speed"), Op(TEXT("Steve"), 0), Target)));
	TestTrue(TEXT("enchant with nothing held"), Joined(McCommands::HandleChat(TEXT("/enchant Steve sharpness 3"), Op(TEXT("Steve")), Target)).Contains(TEXT("is not holding any item")));
	Target.HeldItem = TEXT("minecraft:diamond_sword");
	TestTrue(TEXT("enchant"), Joined(McCommands::HandleChat(TEXT("/enchant Steve sharpness 3"), Op(TEXT("Steve")), Target)).Contains(TEXT("Applied enchantment Sharpness III to Steve's item")));
	TestEqual(TEXT("the level is set"), Target.HeldEnchantments.FindRef(TEXT("minecraft:sharpness")), 3);
	TestTrue(TEXT("past the maximum"), Joined(McCommands::HandleChat(TEXT("/enchant Steve sharpness 6"), Op(TEXT("Steve")), Target)).Contains(TEXT("6 is higher than the maximum level of 5")));
	TestTrue(TEXT("an item the enchantment does not suit"), Joined(McCommands::HandleChat(TEXT("/enchant Steve silk_touch"), Op(TEXT("Steve")), Target)).Contains(TEXT("cannot support that enchantment")));
	TestTrue(TEXT("no such enchantment"), AnyError(McCommands::HandleChat(TEXT("/enchant Steve gravity"), Op(TEXT("Steve")), Target)));

	// The console has every level and no @s.
	Target.bDedicated = true;
	FMcCommandSender Console;
	Console.PermissionLevel = 4;
	McCommands::HandleChat(TEXT("/kick Alex"), Console, Target);
	TestTrue(TEXT("kick from the console"), Target.Kicked.Contains(TEXT("Alex")));
	TestTrue(TEXT("@s from the console"), AnyError(McCommands::HandleChat(TEXT("/kill @s"), Console, Target)));
	return true;
}

#endif
