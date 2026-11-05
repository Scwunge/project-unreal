#include "McItemRegistry.h"
#include "McLoot.h"
#include "McRandom.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLootMobTest, "McClient.McGen.LootMob",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// How many of Rolls rolls of a mob's table give an item whose name starts with Prefix, and the most of it in one roll.
	int32 CountRolls(const TCHAR* Table, const McLoot::FMobContext& Context, const TCHAR* Prefix, int32 Rolls, int32* OutMost = nullptr)
	{
		const McLoot::FMobScope Scope(Context);
		FMcRandom Random(12345);
		int32 With = 0;
		int32 Most = 0;
		for (int32 Roll = 0; Roll < Rolls; ++Roll)
		{
			TArray<FMcItemStack> Out;
			McLoot::RollEntity(Table, false, Random, Out);
			int32 Count = 0;
			for (const FMcItemStack& Stack : Out)
			{
				Count += Stack.Item.ToString().StartsWith(Prefix) ? Stack.Count : 0;
			}
			With += Count > 0 ? 1 : 0;
			Most = FMath::Max(Most, Count);
		}
		if (OutMost)
		{
			*OutMost = Most;
		}
		return With;
	}
}

bool FMcLootMobTest::RunTest(const FString& Parameters)
{
	// The conditions about a mob that its loot table asks: what the file says (cache/.../loot_table/entities), not what
	// the game was seen to do.
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	constexpr int32 Rolls = 4000;
	McLoot::FMobContext Alone;
	McLoot::FMobContext ByPlayer;
	ByPlayer.bKilledByPlayer = true;
	ByPlayer.Killer = TEXT("minecraft:player");

	// A zombie's iron ingot, carrot and potato need a kill by a player and a 2.5 per cent chance (no Looting here);
	// the red mushroom needs a vehicle that is a zombie horse.
	TestEqual(TEXT("an unattended zombie drops no iron"), CountRolls(TEXT("minecraft:entities/zombie"), Alone, TEXT("minecraft:iron_ingot"), Rolls), 0);
	TestEqual(TEXT("a zombie that rides nothing drops no red mushroom"), CountRolls(TEXT("minecraft:entities/zombie"), ByPlayer, TEXT("minecraft:red_mushroom"), Rolls), 0);
	const int32 Irons = CountRolls(TEXT("minecraft:entities/zombie"), ByPlayer, TEXT("minecraft:iron_ingot"), Rolls);
	// (The pool's own 2.5 per cent chance is random_chance_with_enchanted_bonus, the Looting work's.)
	TestTrue(*FString::Printf(TEXT("a zombie killed by a player can drop iron (%d of %d)"), Irons, Rolls), Irons > 0);
	McLoot::FMobContext Rider = ByPlayer;
	Rider.Vehicle = TEXT("minecraft:zombie_horse");
	int32 Most = 0;
	const int32 Mushrooms = CountRolls(TEXT("minecraft:entities/zombie"), Rider, TEXT("minecraft:red_mushroom"), Rolls, &Most);
	TestTrue(*FString::Printf(TEXT("a zombie on a zombie horse drops red mushrooms about half the time (%d of %d)"), Mushrooms, Rolls), Mushrooms > Rolls / 3 && Mushrooms < Rolls * 2 / 3);

	// Rotten flesh needs nothing.
	TestTrue(TEXT("rotten flesh drops from any zombie"), CountRolls(TEXT("minecraft:entities/zombie"), Alone, TEXT("minecraft:rotten_flesh"), Rolls) > Rolls / 2);

	// A creeper gives a music disc to a skeleton's kill, and only then.
	McLoot::FMobContext Shot;
	Shot.Killer = TEXT("minecraft:skeleton");
	Shot.DirectKiller = TEXT("minecraft:arrow");
	Shot.bProjectile = true;
	TestEqual(TEXT("a creeper killed by a player gives no disc"), CountRolls(TEXT("minecraft:entities/creeper"), ByPlayer, TEXT("minecraft:music_disc"), 400), 0);
	const int32 Discs = CountRolls(TEXT("minecraft:entities/creeper"), Shot, TEXT("minecraft:music_disc"), 400);
	TestEqual(TEXT("a creeper killed by a skeleton always drops a disc"), Discs, 400);

	// A ghast's tears disc: a fireball that a player deflected, and the kill is the player's.
	McLoot::FMobContext Fireball = ByPlayer;
	Fireball.DirectKiller = TEXT("minecraft:fireball");
	Fireball.bProjectile = true;
	TestEqual(TEXT("a ghast killed by a player's arrow gives no tears disc"), CountRolls(TEXT("minecraft:entities/ghast"), ByPlayer, TEXT("minecraft:music_disc_tears"), 200), 0);
	TestEqual(TEXT("a ghast killed by its own fireball and a player gives the tears disc"), CountRolls(TEXT("minecraft:entities/ghast"), Fireball, TEXT("minecraft:music_disc_tears"), 200), 200);

	// A slime of size 1 gives balls; a bigger one none.
	McLoot::FMobContext Small;
	Small.CubeSize = 1;
	McLoot::FMobContext Big;
	Big.CubeSize = 4;
	TestTrue(TEXT("a small slime gives slime balls"), CountRolls(TEXT("minecraft:entities/slime"), Small, TEXT("minecraft:slime_ball"), 200) > 100);
	TestEqual(TEXT("a big slime gives none"), CountRolls(TEXT("minecraft:entities/slime"), Big, TEXT("minecraft:slime_ball"), 200), 0);

	// A burning chicken drops cooked chicken, a cold one raw.
	McLoot::FMobContext Burning;
	Burning.bOnFire = true;
	TestEqual(TEXT("a chicken that is not burning drops no cooked chicken"), CountRolls(TEXT("minecraft:entities/chicken"), Alone, TEXT("minecraft:cooked_chicken"), 200), 0);
	TestTrue(TEXT("a chicken that is not burning drops raw chicken"), CountRolls(TEXT("minecraft:entities/chicken"), Alone, TEXT("minecraft:chicken"), 200) > 0);
	TestEqual(TEXT("a burning chicken drops no raw chicken"), CountRolls(TEXT("minecraft:entities/chicken"), Burning, TEXT("minecraft:chicken"), 200), 0);
	TestTrue(TEXT("a burning chicken drops cooked chicken"), CountRolls(TEXT("minecraft:entities/chicken"), Burning, TEXT("minecraft:cooked_chicken"), 200) > 0);

	// A sheep drops wool of its own dye (1 is orange in the game's order), none once sheared.
	McLoot::FMobContext Orange;
	Orange.DyeColor = 1;
	TestEqual(TEXT("an orange sheep drops no white wool"), CountRolls(TEXT("minecraft:entities/sheep"), Orange, TEXT("minecraft:white_wool"), 100), 0);
	TestEqual(TEXT("an orange sheep drops orange wool"), CountRolls(TEXT("minecraft:entities/sheep"), Orange, TEXT("minecraft:orange_wool"), 100), 100);
	McLoot::FMobContext Sheared = Orange;
	Sheared.bSheared = true;
	TestEqual(TEXT("a sheared sheep drops no wool"), CountRolls(TEXT("minecraft:entities/sheep"), Sheared, TEXT("minecraft:orange_wool"), 100), 0);
	// Totals over 3000 kills, beside what the game gave for 300 (loot spawn ... kill, as a player): an orange sheep 1.45
	// mutton and 0.98 orange wool each, a small slime 0.95 slime balls, a zombie 1.04 rotten flesh.
	const auto Mean = [](const TCHAR* Table, const McLoot::FMobContext& Context, const TCHAR* Prefix)
	{
		const McLoot::FMobScope Scope(Context);
		FMcRandom Random(777);
		int32 Total = 0;
		for (int32 Roll = 0; Roll < 3000; ++Roll)
		{
			TArray<FMcItemStack> Out;
			McLoot::RollEntity(Table, false, Random, Out);
			for (const FMcItemStack& Stack : Out)
			{
				Total += Stack.Item.ToString().StartsWith(Prefix) ? Stack.Count : 0;
			}
		}
		return Total / 3000.0;
	};
	const double Mutton = Mean(TEXT("minecraft:entities/sheep"), Orange, TEXT("minecraft:mutton"));
	const double Balls = Mean(TEXT("minecraft:entities/slime"), Small, TEXT("minecraft:slime_ball"));
	const double Flesh = Mean(TEXT("minecraft:entities/zombie"), ByPlayer, TEXT("minecraft:rotten_flesh"));
	AddInfo(FString::Printf(TEXT("per kill: mutton %.2f, slime balls %.2f, rotten flesh %.2f"), Mutton, Balls, Flesh));
	TestTrue(TEXT("a sheep gives 1.5 mutton on average"), FMath::Abs(Mutton - 1.5) < 0.1);
	TestTrue(TEXT("a small slime gives 1 slime ball on average"), FMath::Abs(Balls - 1.0) < 0.1);
	TestTrue(TEXT("a zombie gives 1 rotten flesh on average"), FMath::Abs(Flesh - 1.0) < 0.1);
	return true;
}

#endif
