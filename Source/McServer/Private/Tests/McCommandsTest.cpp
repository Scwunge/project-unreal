#include "McCommands.h"
#include "McLang.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
		TArray<FString> Titles;
		virtual void SendTitle(const FString& Player, int32 Kind, const FString& Text, int32 FadeIn, int32 Stay, int32 FadeOut) override
		{
			Titles.Add(FString::Printf(TEXT("%s:%d:%s:%d,%d,%d"), *Player, Kind, *Text, FadeIn, Stay, FadeOut));
		}
		FString WorldFolder;
		virtual FString GetWorldFolder() const override { return WorldFolder; }
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
		TMap<int32, TPair<FName, int32>> Slots;
		virtual bool SetSlot(const FString& Player, int32 Group, int32 Index, FName Item, int32 Count) override
		{
			Slots.Add(Group * 100 + Index, TPair<FName, int32>(Item, Count));
			return true;
		}
		virtual bool GetSlot(const FString& Player, int32 Group, int32 Index, FName& OutItem, int32& OutCount) const override
		{
			const TPair<FName, int32>* Found = Slots.Find(Group * 100 + Index);
			if (!Found || Found->Value <= 0)
			{
				return false;
			}
			OutItem = Found->Key;
			OutCount = Found->Value;
			return true;
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

// /tellraw sends JSON text to the players as legacy text and says nothing back; /title sends titles, subtitles, action bars,
// clears, resets and times, with the game's replies.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTitleCommandsTest, "McClient.McServer.TextCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcTitleCommandsTest::RunTest(const FString& Parameters)
{
	FFakeTarget Target;
	Target.Players.Add({ TEXT("Steve") });
	const auto Op = [](const TCHAR* Name) { FMcCommandSender Sender; Sender.Name = Name; Sender.PermissionLevel = 4; return Sender; };
	const TArray<FMcCommandReply> Tell = McCommands::Execute(TEXT("tellraw Steve {\"text\":\"a  b\",\"color\":\"red\"}"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("tellraw says nothing back"), Tell.Num(), 0);
	TestEqual(TEXT("the player was told, spaces kept"), Target.Players[0].Told.Num() == 1 ? Target.Players[0].Told[0].Text : FString(), FString(TEXT("\u00a7ca  b")));
	const TArray<FMcCommandReply> Bad = McCommands::Execute(TEXT("tellraw Steve {\"text\":"), Op(TEXT("Steve")), Target);
	TestTrue(TEXT("bad JSON is an error"), Bad.Num() == 1 && Bad[0].bError);

	const TArray<FMcCommandReply> Shown = McCommands::Execute(TEXT("title Steve title {\"text\":\"Hi\",\"color\":\"gold\"}"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("title sent"), Target.Titles.Num() == 1 ? Target.Titles[0] : FString(), FString(TEXT("Steve:0:\u00a76Hi:0,0,0")));
	TestEqual(TEXT("its reply"), Shown.Num() == 1 ? Shown[0].Text : FString(), FString(TEXT("Showing new title for Steve")));
	McCommands::Execute(TEXT("title Steve subtitle \"sub\""), Op(TEXT("Steve")), Target);
	McCommands::Execute(TEXT("title Steve actionbar \"bar\""), Op(TEXT("Steve")), Target);
	McCommands::Execute(TEXT("title Steve times 5 40 10"), Op(TEXT("Steve")), Target);
	McCommands::Execute(TEXT("title Steve clear"), Op(TEXT("Steve")), Target);
	McCommands::Execute(TEXT("title Steve reset"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("six titles in all"), Target.Titles.Num(), 6);
	if (Target.Titles.Num() == 6)
	{
		TestEqual(TEXT("subtitle"), Target.Titles[1], FString(TEXT("Steve:1:sub:0,0,0")));
		TestEqual(TEXT("action bar"), Target.Titles[2], FString(TEXT("Steve:2:bar:0,0,0")));
		TestEqual(TEXT("times"), Target.Titles[3], FString(TEXT("Steve:5::5,40,10")));
		TestEqual(TEXT("clear"), Target.Titles[4], FString(TEXT("Steve:3::0,0,0")));
		TestEqual(TEXT("reset"), Target.Titles[5], FString(TEXT("Steve:4::0,0,0")));
	}
	const TArray<FMcCommandReply> Cleared = McCommands::Execute(TEXT("title Steve clear"), Op(TEXT("Steve")), Target);
	TestEqual(TEXT("clear's reply"), Cleared.Num() == 1 ? Cleared[0].Text : FString(), FString(TEXT("Cleared titles for Steve")));

	// Measured against the game: /random, /tag, /stopsound, /defaultgamemode.
	const auto First = [&](const TCHAR* Line) { const TArray<FMcCommandReply> Got = McCommands::Execute(Line, Op(TEXT("Steve")), Target); return Got.Num() ? Got[0].Text : FString(); };
	TestEqual(TEXT("an empty range"), First(TEXT("random roll 1..1")), FString(TEXT("The range of the random value must be at least 1")));
	TestTrue(TEXT("a roll names who rolled"), First(TEXT("random roll 1..6")).StartsWith(TEXT("Steve rolled ")));
	TestEqual(TEXT("reset one"), First(TEXT("random reset minecraft:foo")), FString(TEXT("Reset random sequence minecraft:foo")));
	TestEqual(TEXT("tag add"), First(TEXT("tag Steve add foo")), FString(TEXT("Added tag 'foo' to Steve")));
	TestEqual(TEXT("tag twice"), First(TEXT("tag Steve add foo")), FString(TEXT("Target either already has the tag or has too many tags")));
	TestEqual(TEXT("tag list"), First(TEXT("tag Steve list")), FString(TEXT("Steve has 1 tag(s): foo")));
	TestEqual(TEXT("tag remove"), First(TEXT("tag Steve remove foo")), FString(TEXT("Removed tag 'foo' from Steve")));
	TestEqual(TEXT("tag remove twice"), First(TEXT("tag Steve remove foo")), FString(TEXT("Target does not have this tag")));
	TestEqual(TEXT("stopsound"), First(TEXT("stopsound Steve master pig")), FString(TEXT("Stopped sound 'minecraft:pig' on source 'master'")));
	TestEqual(TEXT("defaultgamemode"), First(TEXT("defaultgamemode creative")), FString(TEXT("The default game mode is now Creative Mode")));
	// Measured against the game: /setworldspawn and /spawnpoint.
	TestEqual(TEXT("setworldspawn"), First(TEXT("setworldspawn 1 2 3")), FString(TEXT("Set the world spawn point to 1, 2, 3 [0.0, 0.0] in minecraft:overworld")));
	TestEqual(TEXT("spawnpoint"), First(TEXT("spawnpoint Steve 4 5 6")), FString(TEXT("Set spawn point to 4, 5, 6 [0.0, 0.0] in minecraft:overworld for Steve")));
	// Measured against the game: /worldborder.
	TestEqual(TEXT("border get"), First(TEXT("worldborder get")), FString(TEXT("The world border is currently 59999968 block(s) wide")));
	TestEqual(TEXT("border set"), First(TEXT("worldborder set 100")), FString(TEXT("Set the world border to 100.0 block(s) wide")));
	TestEqual(TEXT("border same"), First(TEXT("worldborder set 100")), FString(TEXT("Nothing changed. The world border is already that size")));
	TestEqual(TEXT("border grow"), First(TEXT("worldborder set 200 10")), FString(TEXT("Growing the world border to 200.0 block(s) wide over 0.50 second(s)")));
	TestEqual(TEXT("border add"), First(TEXT("worldborder add 10")), FString(TEXT("Set the world border to 210.0 block(s) wide")));
	TestEqual(TEXT("border centre"), First(TEXT("worldborder center 5 6")), FString(TEXT("Set the center of the world border to 5.50, 6.50")));
	TestEqual(TEXT("border too small"), First(TEXT("worldborder set 0")), FString(TEXT("World border cannot be smaller than 1 block wide")));
	TestEqual(TEXT("border too big"), First(TEXT("worldborder add 59999900")), FString(TEXT("World border cannot be bigger than 5.9999968E7 blocks wide")));
	TestEqual(TEXT("border too far"), First(TEXT("worldborder center 30000000 0")), FString(TEXT("World border cannot be further out than 2.9999984E7 blocks")));
	TestEqual(TEXT("border warning time"), First(TEXT("worldborder warning time 5")), FString(TEXT("Set the world border warning time to 0.25 second(s)")));
	// Measured against the game: /tick.
	TestEqual(TEXT("tick rate"), First(TEXT("tick rate 30")), FString(TEXT("Set the target tick rate to 30.0 per second")));
	TestEqual(TEXT("tick step needs a freeze"), First(TEXT("tick step 2")), FString(TEXT("Unable to step the game - the game must be frozen first")));
	TestEqual(TEXT("tick freeze"), First(TEXT("tick freeze")), FString(TEXT("The game is frozen")));
	// Measured against the game: /damage.
	TestEqual(TEXT("damage negative"), First(TEXT("damage Steve -1")), FString(TEXT("Float must not be less than 0.0: found -1.0")));
	TestEqual(TEXT("damage type"), First(TEXT("damage Steve 1 minecraft:nonsense")), FString(TEXT("Can't find element 'minecraft:nonsense' of type 'minecraft:damage_type'")));
	TestEqual(TEXT("damage nobody"), First(TEXT("damage Nobody 1")), FString(TEXT("No entity was found")));
	TestEqual(TEXT("rotate"), First(TEXT("rotate Steve 90 0")), FString(TEXT("Rotated Steve")));
	// Measured against the game: /scoreboard.
	TestEqual(TEXT("objective list empty"), First(TEXT("scoreboard objectives list")), FString(TEXT("There are no objectives")));
	TestEqual(TEXT("objective add"), First(TEXT("scoreboard objectives add kills dummy")), FString(TEXT("Created new objective [kills]")));
	TestEqual(TEXT("objective twice"), First(TEXT("scoreboard objectives add kills dummy")), FString(TEXT("An objective already exists by that name")));
	TestEqual(TEXT("score set"), First(TEXT("scoreboard players set Foo kills 5")), FString(TEXT("Set [kills] for Foo to 5")));
	TestEqual(TEXT("score add"), First(TEXT("scoreboard players add Foo kills 3")), FString(TEXT("Added 3 to [kills] for Foo (now 8)")));
	TestEqual(TEXT("score remove"), First(TEXT("scoreboard players remove Foo kills 2")), FString(TEXT("Removed 2 from [kills] for Foo (now 6)")));
	TestEqual(TEXT("score get"), First(TEXT("scoreboard players get Foo kills")), FString(TEXT("Foo has 6 [kills]")));
	TestEqual(TEXT("score operation"), First(TEXT("scoreboard players operation Foo kills += Foo kills")), FString(TEXT("Set [kills] for Foo to 12")));
	TestEqual(TEXT("score enable"), First(TEXT("scoreboard players enable Foo kills")), FString(TEXT("Enable only works on trigger-objectives")));
	TestEqual(TEXT("unknown objective"), First(TEXT("scoreboard players set Foo nope 1")), FString(TEXT("Unknown scoreboard objective 'nope'")));
	TestEqual(TEXT("score reset"), First(TEXT("scoreboard players reset Foo kills")), FString(TEXT("Reset [kills] for Foo")));
	TestEqual(TEXT("objective remove"), First(TEXT("scoreboard objectives remove kills")), FString(TEXT("Removed objective [kills]")));
	// Measured against the game: /execute.
	TestEqual(TEXT("execute needs more"), First(TEXT("execute")), FString(TEXT("Unknown or incomplete command. See below for error")));
	TestEqual(TEXT("execute run"), First(TEXT("execute run scoreboard objectives list")), FString(TEXT("There are no objectives")));
	TestEqual(TEXT("execute as"), First(TEXT("execute as Steve at Steve run scoreboard objectives list")), FString(TEXT("There are no objectives")));
	TestEqual(TEXT("execute if entity"), First(TEXT("execute if entity Steve")), FString(TEXT("Test passed. Count: 1")));
	TestEqual(TEXT("execute unless entity"), First(TEXT("execute unless entity Steve")), FString(TEXT("Test failed. Count: 1")));
	// Measured against the game: /locate biome.
	TestEqual(TEXT("locate unknown biome"), First(TEXT("locate biome minecraft:nonsense")), FString(TEXT("Can't find element 'minecraft:nonsense' of type 'minecraft:worldgen/biome'")));
	TestEqual(TEXT("locate needs a biome"), First(TEXT("locate biome")), FString(TEXT("Unknown or incomplete command. See below for error")));
	TestEqual(TEXT("swing"), First(TEXT("swing Steve")), FString(TEXT("Made Steve swing an arm")));
	// Measured against the game: /spreadplayers (one entity lands on its own: distance 0.00).
	First(TEXT("fill -20 0 -20 20 0 20 stone"));
	TestEqual(TEXT("spreadplayers"), First(TEXT("spreadplayers 0 0 5 20 false @s")), FString(TEXT("Spread 1 entity/entities around 0.5, 0.5 with an average distance of 0.00 block(s) apart")));
	TestEqual(TEXT("spreadplayers teams"), First(TEXT("spreadplayers 0 0 5 20 true @s")), FString(TEXT("Spread 1 team(s) around 0.5, 0.5 with an average distance of 0.00 block(s) apart")));
	TestEqual(TEXT("spreadplayers nobody"), First(TEXT("spreadplayers 0 0 5 20 false Nobody")), FString(TEXT("No entity was found")));
	// Measured against the game: /team.
	TestEqual(TEXT("team none"), First(TEXT("team list")), FString(TEXT("There are no teams")));
	TestEqual(TEXT("team add"), First(TEXT("team add red")), FString(TEXT("Created team [red]")));
	TestEqual(TEXT("team add again"), First(TEXT("team add red")), FString(TEXT("A team already exists by that name")));
	TestEqual(TEXT("team add display"), First(TEXT("team add blue {\"text\":\"Blue Team\"}")), FString(TEXT("Created team [Blue Team]")));
	TestEqual(TEXT("team list"), First(TEXT("team list")), FString(TEXT("There are 2 team(s): [Blue Team], [red]")));
	TestEqual(TEXT("team empty list"), First(TEXT("team list red")), FString(TEXT("There are no members on team [red]")));
	TestEqual(TEXT("team join"), First(TEXT("team join red @s")), FString(TEXT("Added Steve to team [red]")));
	TestEqual(TEXT("team members"), First(TEXT("team list red")), FString(TEXT("Team [red] has 1 member(s): Steve")));
	TestEqual(TEXT("team join unknown"), First(TEXT("team join nope @s")), FString(TEXT("Unknown team 'nope'")));
	TestEqual(TEXT("team leave"), First(TEXT("team leave @s")), FString(TEXT("Removed Steve from any team")));
	TestEqual(TEXT("team leave none"), First(TEXT("team leave @s")), FString(TEXT("Removed 0 members from any team")));
	TestEqual(TEXT("team color"), First(TEXT("team modify red color red")), FString(TEXT("Updated the color for team [red] to red")));
	TestEqual(TEXT("team color same"), First(TEXT("team modify red color red")), FString(TEXT("Nothing changed. That team already has that color")));
	TestEqual(TEXT("team color nonsense"), First(TEXT("team modify red color nonsense")), FString(TEXT("Unknown color 'nonsense'")));
	TestEqual(TEXT("team friendly fire"), First(TEXT("team modify red friendlyFire false")), FString(TEXT("Disabled friendly fire for team [red]")));
	TestEqual(TEXT("team nametag"), First(TEXT("team modify red nametagVisibility never")), FString(TEXT("Nametag visibility for team [red] is now \"Never\"")));
	TestEqual(TEXT("team display name"), First(TEXT("team modify red displayName {\"text\":\"Reds\"}")), FString(TEXT("Updated the name of team [Reds]")));
	TestEqual(TEXT("team empty"), First(TEXT("team empty red")), FString(TEXT("Nothing changed. That team is already empty")));
	TestEqual(TEXT("team remove"), First(TEXT("team remove red")), FString(TEXT("Removed team [Reds]")));
	TestEqual(TEXT("team removed"), First(TEXT("team remove red")), FString(TEXT("Unknown team 'red'")));
	// Measured against the game: /bossbar.
	TestEqual(TEXT("bossbar none"), First(TEXT("bossbar list")), FString(TEXT("There are no custom bossbars active")));
	TestEqual(TEXT("bossbar add"), First(TEXT("bossbar add foo Foo")), FString(TEXT("Created custom bossbar [Foo]")));
	TestEqual(TEXT("bossbar add component"), First(TEXT("bossbar add minecraft:bar {\"text\":\"Bar Name\"}")), FString(TEXT("Created custom bossbar [Bar Name]")));
	TestEqual(TEXT("bossbar add again"), First(TEXT("bossbar add foo Again")), FString(TEXT("A bossbar already exists with the ID 'minecraft:foo'")));
	TestEqual(TEXT("bossbar list"), First(TEXT("bossbar list")), FString(TEXT("There are 2 custom bossbar(s) active: [Bar Name], [Foo]")));
	TestEqual(TEXT("bossbar get max"), First(TEXT("bossbar get foo max")), FString(TEXT("Custom bossbar [Foo] has a maximum of 100")));
	TestEqual(TEXT("bossbar get players"), First(TEXT("bossbar get foo players")), FString(TEXT("Custom bossbar [Foo] has no players currently online")));
	TestEqual(TEXT("bossbar set value"), First(TEXT("bossbar set foo value 50")), FString(TEXT("Custom bossbar [Foo] has changed value to 50")));
	TestEqual(TEXT("bossbar set value same"), First(TEXT("bossbar set foo value 50")), FString(TEXT("Nothing changed. That's already the value of this bossbar")));
	TestEqual(TEXT("bossbar set max 0"), First(TEXT("bossbar set foo max 0")), FString(TEXT("Integer must not be less than 1: found 0")));
	TestEqual(TEXT("bossbar set color"), First(TEXT("bossbar set foo color red")), FString(TEXT("Custom bossbar [Foo] has changed color")));
	TestEqual(TEXT("bossbar set name"), First(TEXT("bossbar set foo name {\"text\":\"New\"}")), FString(TEXT("Custom bossbar [New] has been renamed")));
	TestEqual(TEXT("bossbar hide"), First(TEXT("bossbar set foo visible false")), FString(TEXT("Custom bossbar [New] is now hidden")));
	TestEqual(TEXT("bossbar players"), First(TEXT("bossbar set foo players @s")), FString(TEXT("Custom bossbar [New] now has 1 player(s): Steve")));
	TestEqual(TEXT("bossbar players none"), First(TEXT("bossbar set foo players")), FString(TEXT("Custom bossbar [New] no longer has any players")));
	TestEqual(TEXT("bossbar unknown"), First(TEXT("bossbar get nope value")), FString(TEXT("No bossbar exists with the ID 'minecraft:nope'")));
	TestEqual(TEXT("bossbar remove"), First(TEXT("bossbar remove foo")), FString(TEXT("Removed custom bossbar [New]")));
	TestEqual(TEXT("bossbar remove last"), First(TEXT("bossbar remove bar")), FString(TEXT("Removed custom bossbar [Bar Name]")));
	// Measured against the game: /data (storage; entity Pos).
	TestEqual(TEXT("data empty"), First(TEXT("data get storage test:a")), FString(TEXT("Storage test:a has the following contents: {}")));
	TestEqual(TEXT("data merge"), First(TEXT("data merge storage test:a {x:1,y:\"hi\",z:[1,2,3],w:{a:1b,b:2.5d}}")), FString(TEXT("Modified storage test:a")));
	TestEqual(TEXT("data get all"), First(TEXT("data get storage test:a")), FString(TEXT("Storage test:a has the following contents: {w: {a: 1b, b: 2.5d}, x: 1, y: \"hi\", z: [1, 2, 3]}")));
	TestEqual(TEXT("data get path"), First(TEXT("data get storage test:a z[1]")), FString(TEXT("Storage test:a has the following contents: 2")));
	TestEqual(TEXT("data get scaled"), First(TEXT("data get storage test:a w.b 2")), FString(TEXT("w.b in storage test:a after scale factor of 2.00 is 5")));
	TestEqual(TEXT("data get none"), First(TEXT("data get storage test:a nope")), FString(TEXT("Found no elements matching nope")));
	TestEqual(TEXT("data set"), First(TEXT("data modify storage test:a x set value 5")), FString(TEXT("Modified storage test:a")));
	TestEqual(TEXT("data set same"), First(TEXT("data modify storage test:a x set value 5")), FString(TEXT("Nothing changed. The specified properties already have these values")));
	First(TEXT("data modify storage test:a z append value 4"));
	First(TEXT("data modify storage test:a z prepend value 0"));
	First(TEXT("data modify storage test:a z insert 1 value 9"));
	TestEqual(TEXT("data list ops"), First(TEXT("data get storage test:a z")), FString(TEXT("Storage test:a has the following contents: [0, 9, 1, 2, 3, 4]")));
	First(TEXT("data modify storage test:a q set from storage test:a x"));
	First(TEXT("data modify storage test:a w merge value {c:3}"));
	TestEqual(TEXT("data merge value"), First(TEXT("data get storage test:a w")), FString(TEXT("Storage test:a has the following contents: {a: 1b, b: 2.5d, c: 3}")));
	TestEqual(TEXT("data remove"), First(TEXT("data remove storage test:a y")), FString(TEXT("Modified storage test:a")));
	TestEqual(TEXT("data remove again"), First(TEXT("data remove storage test:a y")), FString(TEXT("Nothing changed. The specified properties already have these values")));
	// Storage is written to the world's folder after a change.
	const FString StorageFolder = FPaths::ProjectSavedDir() / TEXT("McCommandsTestWorld");
	Target.WorldFolder = StorageFolder;
	First(TEXT("data merge storage test:saved {n:7}"));
	FString StorageText;
	TestTrue(TEXT("storage file written"), FFileHelper::LoadFileToString(StorageText, *(StorageFolder / TEXT("data") / TEXT("command_storage.snbt"))) && StorageText.Contains(TEXT("n: 7")));
	Target.WorldFolder.Reset();
	TestEqual(TEXT("data bad snbt"), First(TEXT("data merge storage test:a {x:")), FString(TEXT("Expected a valid unquoted string")));
	TestEqual(TEXT("data nobody"), First(TEXT("data get entity Nobody")), FString(TEXT("No entity was found")));
	// Measured against the game: /item.
	TestEqual(TEXT("item replace"), First(TEXT("item replace entity Steve weapon.mainhand with minecraft:diamond 5")), FString(TEXT("Replaced 1 slot(s) on Steve with [Diamond]")));
	TestEqual(TEXT("item hotbar"), First(TEXT("item replace entity Steve hotbar.1 with minecraft:diamond_sword")), FString(TEXT("Replaced 1 slot(s) on Steve with [Diamond Sword]")));
	TestEqual(TEXT("item unknown"), First(TEXT("item replace entity Steve weapon.mainhand with minecraft:nonsense")), FString(TEXT("Unknown item 'minecraft:nonsense'")));
	TestEqual(TEXT("item bad slot"), First(TEXT("item replace entity Steve nosuch.slot with minecraft:stick")), FString(TEXT("Can't find element 'minecraft:nosuch.slot' in registry 'minecraft:slot_source'")));
	TestEqual(TEXT("item nobody"), First(TEXT("item replace entity Nobody weapon.mainhand with minecraft:stick")), FString(TEXT("No entity was found")));
	TestEqual(TEXT("item from"), First(TEXT("item replace entity Steve hotbar.2 from entity Steve hotbar.1")), FString(TEXT("Replaced 1 slot(s) on Steve")));
	// Measured against the game: /teammsg and /stopwatch.
	First(TEXT("team add msgteam"));
	TestEqual(TEXT("teammsg without a team"), First(TEXT("teammsg hi")), FString(TEXT("You must be on a team to message your team")));
	First(TEXT("team join msgteam Steve"));
	TestEqual(TEXT("teammsg"), First(TEXT("teammsg hello team")), FString(TEXT("-> [msgteam] <Steve> hello team")));
	TestEqual(TEXT("tm"), First(TEXT("tm again")), FString(TEXT("-> [msgteam] <Steve> again")));
	First(TEXT("team remove msgteam"));
	TestEqual(TEXT("stopwatch create"), First(TEXT("stopwatch create sw")), FString(TEXT("Created stopwatch 'minecraft:sw'")));
	TestEqual(TEXT("stopwatch twice"), First(TEXT("stopwatch create sw")), FString(TEXT("Stopwatch 'minecraft:sw' already exists")));
	TestTrue(TEXT("stopwatch query"), First(TEXT("stopwatch query sw")).StartsWith(TEXT("Stopwatch 'minecraft:sw' has run for ")));
	TestEqual(TEXT("stopwatch restart"), First(TEXT("stopwatch restart sw")), FString(TEXT("Restarted stopwatch 'minecraft:sw'")));
	TestEqual(TEXT("stopwatch remove"), First(TEXT("stopwatch remove sw")), FString(TEXT("Removed stopwatch 'minecraft:sw'")));
	TestEqual(TEXT("stopwatch gone"), First(TEXT("stopwatch remove sw")), FString(TEXT("Stopwatch 'minecraft:sw' does not exist")));
	// Measured against the game: /trigger.
	TestEqual(TEXT("trigger unknown"), First(TEXT("trigger nope")), FString(TEXT("Unknown scoreboard objective 'nope'")));
	First(TEXT("scoreboard objectives add trg trigger"));
	TestEqual(TEXT("trigger unprimed"), First(TEXT("trigger trg")), FString(TEXT("You cannot trigger this objective yet")));
	First(TEXT("scoreboard players enable Steve trg"));
	TestEqual(TEXT("trigger"), First(TEXT("trigger trg")), FString(TEXT("Triggered [trg]")));
	TestEqual(TEXT("trigger once"), First(TEXT("trigger trg")), FString(TEXT("You cannot trigger this objective yet")));
	First(TEXT("scoreboard players enable Steve trg"));
	TestEqual(TEXT("trigger add"), First(TEXT("trigger trg add 5")), FString(TEXT("Triggered [trg] (added 5 to value)")));
	First(TEXT("scoreboard players enable Steve trg"));
	TestEqual(TEXT("trigger set"), First(TEXT("trigger trg set 3")), FString(TEXT("Triggered [trg] (set value to 3)")));
	TestEqual(TEXT("trigger value"), First(TEXT("scoreboard players get Steve trg")), FString(TEXT("Steve has 3 [trg]")));
	First(TEXT("scoreboard objectives remove trg"));
	// Measured against the game: /forceload.
	TestEqual(TEXT("forceload none"), First(TEXT("forceload query")), FString(TEXT("No force loaded chunks were found in minecraft:overworld")));
	TestEqual(TEXT("forceload add"), First(TEXT("forceload add 0 0")), FString(TEXT("Marked chunk [0, 0] in minecraft:overworld to be force loaded")));
	TestEqual(TEXT("forceload add again"), First(TEXT("forceload add 0 0")), FString(TEXT("No chunks were marked for force loading")));
	TestEqual(TEXT("forceload range"), First(TEXT("forceload add 0 0 32 32")), FString(TEXT("Marked 8 chunks in minecraft:overworld from [0, 0] to [2, 2] to be force loaded")));
	TestEqual(TEXT("forceload too big"), First(TEXT("forceload add 0 0 300 300")), FString(TEXT("Too many chunks in the specified area (maximum 256, but specified 361)")));
	TestEqual(TEXT("forceload query one"), First(TEXT("forceload query 100 100")), FString(TEXT("Chunk at [6, 6] in minecraft:overworld is not marked for force loading")));
	TestEqual(TEXT("forceload remove all"), First(TEXT("forceload remove all")), FString(TEXT("Unmarked all force loaded chunks in minecraft:overworld")));
	return true;
}

#endif
