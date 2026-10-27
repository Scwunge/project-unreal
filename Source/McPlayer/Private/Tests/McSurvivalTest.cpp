#include "Dom/JsonObject.h"
#include "McBlockBreak.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McContainer.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McItemRegistry.h"
#include "McPlayerMenu.h"
#include "McSurvival.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FMcItemStack S(const TCHAR* Name, int32 Count = 1)
	{
		return FMcItemStack(FName(Name), Count);
	}

	bool LoadRegistries()
	{
		return FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded();
	}

	// Ticks the rules with nothing around.
	void TickMany(FMcPlayerStats& Stats, int32 Ticks, McSurvival::EDifficulty Difficulty = McSurvival::EDifficulty::Normal, const McSurvival::FEnvironment& Environment = McSurvival::FEnvironment())
	{
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			McSurvival::Tick(Stats, Environment, Difficulty);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFallDamageTest, "McClient.McPlayer.FallDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFallDamageTest::RunTest(const FString& Parameters)
{
	// Distance minus 3, rounded up.
	TestEqual(TEXT("3 blocks is free"), McSurvival::GetFallDamage(3.0f), 0);
	TestEqual(TEXT("a bit over 3 is 1"), McSurvival::GetFallDamage(3.1f), 1);
	TestEqual(TEXT("4 blocks is 1"), McSurvival::GetFallDamage(4.0f), 1);
	TestEqual(TEXT("10 blocks is 7"), McSurvival::GetFallDamage(10.0f), 7);
	TestEqual(TEXT("23 blocks is 20, a whole bar"), McSurvival::GetFallDamage(23.0f), 20);

	// A fall seen tick by tick: 5 blocks down, then the ground.
	McSurvival::FFallTracker Tracker;
	float Y = 10.0f;
	int32 Damage = 0;
	for (int32 Tick = 0; Tick < 5; ++Tick)
	{
		Damage += Tracker.Update(Y, Y - 1.0f, false, false);
		Y -= 1.0f;
	}
	TestEqual(TEXT("nothing while in the air"), Damage, 0);
	TestEqual(TEXT("the fall so far"), Tracker.Distance, 5.0f);
	TestEqual(TEXT("landing after 5 blocks hurts for 2"), Tracker.Update(Y, Y, true, false), 2);
	TestEqual(TEXT("the distance is cleared"), Tracker.Distance, 0.0f);

	// Water, a ladder or a web end a fall without damage.
	McSurvival::FFallTracker Water;
	Water.Distance = 30.0f;
	TestEqual(TEXT("landing in water is free"), Water.Update(5.0f, 4.0f, true, true), 0);
	TestEqual(TEXT("and clears the fall"), Water.Distance, 0.0f);

	// A jump up that comes down to the same height is no fall.
	McSurvival::FFallTracker Jump;
	Jump.Update(0.0f, 1.0f, false, false);
	Jump.Update(1.0f, 0.0f, false, false);
	TestEqual(TEXT("a jump back to the start is nothing"), Jump.Update(0.0f, 0.0f, true, false), 0);

	// The damage reaches health through the rules, as Fall (which armour does not reduce).
	FMcPlayerStats Stats;
	Stats.Armor = 20;
	const float Taken = McSurvival::Hurt(Stats, 7.0f, McSurvival::EDamage::Fall);
	TestEqual(TEXT("armour does not soften a fall"), Taken, 7.0f);
	TestEqual(TEXT("health after the fall"), Stats.Health, 13.0f);
	TestEqual(TEXT("the hurt flash"), Stats.HurtTime, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDrowningTest, "McClient.McPlayer.Drowning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDrowningTest::RunTest(const FString& Parameters)
{
	FMcPlayerStats Stats;
	// Hungry, so that no regeneration muddies the count.
	Stats.Food = 10;
	Stats.Saturation = 0.0f;
	McSurvival::FEnvironment Under;
	Under.bHeadInWater = true;
	Under.bFeetInWater = true;

	TickMany(Stats, 100, McSurvival::EDifficulty::Normal, Under);
	TestEqual(TEXT("breath drops a tick at a time"), Stats.Air, 200);
	TickMany(Stats, 219, McSurvival::EDifficulty::Normal, Under);
	TestEqual(TEXT("one tick before the breath is gone"), Stats.Air, -19);
	TestEqual(TEXT("no damage yet"), Stats.Health, 20.0f);
	TickMany(Stats, 1, McSurvival::EDifficulty::Normal, Under);
	TestEqual(TEXT("at -20 the breath resets"), Stats.Air, 0);
	TestEqual(TEXT("and it costs 2 health"), Stats.Health, 18.0f);
	TickMany(Stats, 20, McSurvival::EDifficulty::Normal, Under);
	TestEqual(TEXT("another 2 after 20 more ticks"), Stats.Health, 16.0f);

	// Back in the air the breath comes back four a tick.
	TickMany(Stats, 10);
	TestEqual(TEXT("breath returns"), Stats.Air, 40);
	TickMany(Stats, 100);
	TestEqual(TEXT("up to full"), Stats.Air, Stats.MaxAir);

	// Drowning goes through armour.
	FMcPlayerStats Armoured;
	Armoured.Armor = 20;
	Armoured.Air = -19;
	TickMany(Armoured, 1, McSurvival::EDifficulty::Normal, Under);
	TestEqual(TEXT("armour does not help against drowning"), Armoured.Health, 18.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFoodTest, "McClient.McPlayer.FoodAndRegeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFoodTest::RunTest(const FString& Parameters)
{
	using McSurvival::EDifficulty;

	// Exhaustion: every 4 takes saturation first, then food.
	{
		FMcPlayerStats Stats;
		McSurvival::AddExhaustion(Stats, 4.0f);
		McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		TestEqual(TEXT("saturation pays first"), Stats.Saturation, 4.0f);
		TestEqual(TEXT("food untouched"), Stats.Food, 20);
		Stats.Saturation = 0.0f;
		McSurvival::AddExhaustion(Stats, 4.0f);
		McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		TestEqual(TEXT("then food"), Stats.Food, 19);
		TestEqual(TEXT("exhaustion is spent"), Stats.Exhaustion, 0.0f);
	}
	// Full food with saturation: heals every 10 ticks by saturation / 6 (at most 1), and it costs that much in exhaustion.
	{
		FMcPlayerStats Stats;
		Stats.Health = 10.0f;
		Stats.Saturation = 3.0f;
		for (int32 Tick = 0; Tick < 9; ++Tick)
		{
			McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		}
		TestEqual(TEXT("nothing in the first 9 ticks"), Stats.Health, 10.0f);
		McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		TestEqual(TEXT("the tenth heals saturation / 6"), Stats.Health, 10.5f);
		TestEqual(TEXT("and spends it as exhaustion"), Stats.Exhaustion, 3.0f);
	}
	{
		FMcPlayerStats Stats;
		Stats.Health = 10.0f;
		Stats.Saturation = 20.0f;
		Stats.Food = 20;
		for (int32 Tick = 0; Tick < 10; ++Tick)
		{
			McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		}
		TestEqual(TEXT("a lot of saturation heals 1"), Stats.Health, 11.0f);
	}
	// Food 18 or more without saturation: 1 health every 80 ticks.
	{
		FMcPlayerStats Stats;
		Stats.Health = 10.0f;
		Stats.Food = 18;
		Stats.Saturation = 0.0f;
		for (int32 Tick = 0; Tick < 79; ++Tick)
		{
			McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		}
		TestEqual(TEXT("not yet"), Stats.Health, 10.0f);
		McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		TestEqual(TEXT("the 80th heals 1"), Stats.Health, 11.0f);
		TestEqual(TEXT("at a cost of 6 exhaustion"), Stats.Exhaustion, 6.0f);
	}
	// Under 18: no healing.
	{
		FMcPlayerStats Stats;
		Stats.Health = 10.0f;
		Stats.Food = 17;
		Stats.Saturation = 0.0f;
		for (int32 Tick = 0; Tick < 400; ++Tick)
		{
			McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		}
		TestEqual(TEXT("hungry players do not heal"), Stats.Health, 10.0f);
	}
	// Starving: 1 damage every 80 ticks; Easy stops at 10, Normal at 1, Hard goes on.
	{
		FMcPlayerStats Stats;
		Stats.Food = 0;
		Stats.Saturation = 0.0f;
		Stats.Health = 20.0f;
		for (int32 Tick = 0; Tick < 80; ++Tick)
		{
			McSurvival::TickNeeds(Stats, EDifficulty::Normal);
		}
		TestEqual(TEXT("starving hurts"), Stats.Health, 19.0f);
	}
	{
		FMcPlayerStats Easy;
		Easy.Food = 0;
		Easy.Saturation = 0.0f;
		Easy.Health = 10.0f;
		FMcPlayerStats Normal = Easy;
		Normal.Health = 1.0f;
		FMcPlayerStats Hard = Easy;
		Hard.Health = 1.0f;
		for (int32 Tick = 0; Tick < 80 * 3; ++Tick)
		{
			McSurvival::TickNeeds(Easy, EDifficulty::Easy);
			McSurvival::TickNeeds(Normal, EDifficulty::Normal);
			McSurvival::TickNeeds(Hard, EDifficulty::Hard);
			// The hurt immunity must not hide the hits.
			Easy.InvulnTicks = Normal.InvulnTicks = Hard.InvulnTicks = 0;
		}
		TestEqual(TEXT("Easy stops at 10"), Easy.Health, 10.0f);
		TestEqual(TEXT("Normal stops at half a heart"), Normal.Health, 1.0f);
		TestTrue(TEXT("Hard kills"), McSurvival::IsDead(Hard));
	}
	// Peaceful: health comes back a point a second and hunger stands still.
	{
		FMcPlayerStats Stats;
		Stats.Health = 10.0f;
		Stats.Food = 5;
		McSurvival::AddExhaustion(Stats, 10.0f);
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			McSurvival::TickNeeds(Stats, EDifficulty::Peaceful);
		}
		TestEqual(TEXT("peaceful heals a point a second"), Stats.Health, 11.0f);
		TestEqual(TEXT("and does not spend food"), Stats.Food, 5);
	}

	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	// Eating: nutrition to food, saturation to saturation, never more than food.
	{
		FMcPlayerStats Stats;
		Stats.Food = 10;
		Stats.Saturation = 0.0f;
		FMcFoodData Food;
		Food.Nutrition = 5;
		Food.Saturation = 6.0f;
		TestTrue(TEXT("can eat when hungry"), McSurvival::CanEat(Stats, Food));
		McSurvival::Eat(Stats, Food);
		TestEqual(TEXT("food goes up"), Stats.Food, 15);
		TestEqual(TEXT("saturation goes up"), Stats.Saturation, 6.0f);
		Stats.Food = 20;
		Stats.Saturation = 20.0f;
		TestFalse(TEXT("cannot eat when full"), McSurvival::CanEat(Stats, Food));
		Food.bCanAlwaysEat = true;
		TestTrue(TEXT("unless the food says so (golden apple)"), McSurvival::CanEat(Stats, Food));
		Stats.Food = 15;
		Stats.Saturation = 14.0f;
		Food.bCanAlwaysEat = false;
		McSurvival::Eat(Stats, Food);
		TestEqual(TEXT("saturation never past food"), Stats.Saturation, 20.0f);
	}
	// The real items carry the food component.
	FMcFoodData Bread;
	TestTrue(TEXT("bread is food"), FMcItemRegistry::Get().GetFood(TEXT("minecraft:bread"), Bread));
	TestEqual(TEXT("bread feeds 5"), Bread.Nutrition, 5);
	FMcFoodData Stone;
	TestFalse(TEXT("stone is not"), FMcItemRegistry::Get().GetFood(TEXT("minecraft:stone"), Stone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcArmorMathTest, "McClient.McPlayer.ArmorAndDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcArmorMathTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	// damage * (1 - min(20, max(A / 5, A - D / (2 + T / 4))) / 25)
	TestEqual(TEXT("no armour"), McSurvival::ReduceByArmor(10.0f, 0.0f, 0.0f), 10.0f);
	TestEqual(TEXT("armour 20 against 10"), McSurvival::ReduceByArmor(10.0f, 20.0f, 0.0f), 4.0f);
	TestEqual(TEXT("armour 10 against 5"), McSurvival::ReduceByArmor(5.0f, 10.0f, 0.0f), 3.5f);
	TestEqual(TEXT("small hits are cut the most: armour 10 against 1"), McSurvival::ReduceByArmor(1.0f, 10.0f, 0.0f), 1.0f * (1.0f - 9.5f / 25.0f));
	TestEqual(TEXT("toughness helps big hits"), McSurvival::ReduceByArmor(20.0f, 20.0f, 8.0f), 20.0f * (1.0f - 15.0f / 25.0f));
	TestEqual(TEXT("the cut is capped at 80%"), McSurvival::ReduceByArmor(10.0f, 100.0f, 100.0f), 2.0f);

	TestTrue(TEXT("falls bypass armour"), McSurvival::BypassesArmor(McSurvival::EDamage::Fall));
	TestTrue(TEXT("starving does"), McSurvival::BypassesArmor(McSurvival::EDamage::Starve));
	TestFalse(TEXT("a bite does not"), McSurvival::BypassesArmor(McSurvival::EDamage::Mob));

	// What is worn: an iron chestplate is 6 points.
	FMcContainer Worn(4);
	Worn.Set(1, S(TEXT("minecraft:iron_chestplate")));
	float Points = 0.0f, Toughness = 0.0f;
	McSurvival::GetArmorValues(Worn, Points, Toughness);
	TestEqual(TEXT("an iron chestplate gives 6"), Points, 6.0f);
	Worn.Set(0, S(TEXT("minecraft:iron_helmet")));
	Worn.Set(2, S(TEXT("minecraft:iron_leggings")));
	Worn.Set(3, S(TEXT("minecraft:iron_boots")));
	McSurvival::GetArmorValues(Worn, Points, Toughness);
	TestEqual(TEXT("a full iron set gives 15"), Points, 15.0f);

	// A bite through the full set, and the wear it leaves.
	FMcPlayerStats Stats;
	Stats.Armor = 15;
	const float Taken = McSurvival::Hurt(Stats, 8.0f, McSurvival::EDamage::Mob, &Worn);
	TestEqual(TEXT("armour 15 against 8"), Taken, McSurvival::ReduceByArmor(8.0f, 15.0f, 0.0f));
	TestEqual(TEXT("health goes down by it"), Stats.Health, 20.0f - Taken);
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		TestFalse(*FString::Printf(TEXT("piece %d is still worn"), Slot), Worn.Get(Slot).IsEmpty());
	}

	// Immunity: for the first 10 ticks after a hit only a bigger hit counts, by the difference.
	FMcPlayerStats Immune;
	TestEqual(TEXT("first hit"), McSurvival::Hurt(Immune, 5.0f, McSurvival::EDamage::Generic), 5.0f);
	TestEqual(TEXT("immune to an equal hit"), McSurvival::Hurt(Immune, 5.0f, McSurvival::EDamage::Generic), 0.0f);
	TestEqual(TEXT("immune to a smaller one"), McSurvival::Hurt(Immune, 3.0f, McSurvival::EDamage::Generic), 0.0f);
	TestEqual(TEXT("a bigger one gets through by the difference"), McSurvival::Hurt(Immune, 8.0f, McSurvival::EDamage::Generic), 3.0f);
	TestEqual(TEXT("health"), Immune.Health, 12.0f);
	TestEqual(TEXT("the void ignores immunity"), McSurvival::Hurt(Immune, 4.0f, McSurvival::EDamage::Void), 4.0f);
	// After 10 ticks a new hit counts in full again (but not before the 20 are over, the immunity just shortens).
	FMcPlayerStats Later;
	McSurvival::Hurt(Later, 5.0f, McSurvival::EDamage::Generic);
	TickMany(Later, 10);
	TestEqual(TEXT("10 ticks on, the same hit counts"), McSurvival::Hurt(Later, 5.0f, McSurvival::EDamage::Generic), 5.0f);

	// Absorption goes first; death at zero.
	FMcPlayerStats Gold;
	Gold.Absorption = 4.0f;
	McSurvival::Hurt(Gold, 6.0f, McSurvival::EDamage::Generic);
	TestEqual(TEXT("absorption takes 4"), Gold.Health, 18.0f);
	TestEqual(TEXT("and is used up"), Gold.Absorption, 0.0f);
	FMcPlayerStats Dying;
	Dying.Health = 3.0f;
	McSurvival::Hurt(Dying, 10.0f, McSurvival::EDamage::Generic);
	TestTrue(TEXT("dead"), McSurvival::IsDead(Dying));

	// Lava and fire: lava hurts 4 and sets the player alight, the burning goes on a point a second, water puts it out.
	FMcPlayerStats Burning;
	Burning.Food = 10;
	Burning.Saturation = 0.0f;
	McSurvival::FEnvironment Lava;
	Lava.bInLava = true;
	McSurvival::Tick(Burning, Lava, McSurvival::EDifficulty::Normal);
	TestEqual(TEXT("lava hurts 4"), Burning.Health, 16.0f);
	TestTrue(TEXT("and sets alight"), Burning.OnFireTicks > 250);
	TickMany(Burning, 40);
	TestTrue(TEXT("burning goes on"), Burning.Health < 16.0f);
	McSurvival::FEnvironment Wet;
	Wet.bFeetInWater = true;
	McSurvival::Tick(Burning, Wet, McSurvival::EDifficulty::Normal);
	TestEqual(TEXT("water puts it out"), Burning.OnFireTicks, 0);
	FMcPlayerStats Fire;
	McSurvival::FEnvironment InFire;
	InFire.bInFire = true;
	McSurvival::Tick(Fire, InFire, McSurvival::EDifficulty::Normal);
	TestEqual(TEXT("fire hurts 1"), Fire.Health, 19.0f);
	TestTrue(TEXT("and burns for 8 seconds"), Fire.OnFireTicks >= 150);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcExperienceTest, "McClient.McPlayer.Experience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcExperienceTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("level 0 to 1"), McSurvival::GetXpToNextLevel(0), 7);
	TestEqual(TEXT("level 15 to 16"), McSurvival::GetXpToNextLevel(15), 37);
	TestEqual(TEXT("level 16 to 17"), McSurvival::GetXpToNextLevel(16), 42);
	TestEqual(TEXT("level 30 to 31"), McSurvival::GetXpToNextLevel(30), 112);
	TestEqual(TEXT("level 31 to 32"), McSurvival::GetXpToNextLevel(31), 121);
	TestEqual(TEXT("points at level 16"), McSurvival::GetTotalXpForLevel(16), 352);
	TestEqual(TEXT("points at level 31"), McSurvival::GetTotalXpForLevel(31), 1507);

	FMcPlayerStats Stats;
	McSurvival::AddXp(Stats, 3);
	TestEqual(TEXT("3 points is still level 0"), Stats.XpLevel, 0);
	TestEqual(TEXT("with 3 sevenths of the bar"), Stats.XpProgress, 3.0f / 7.0f);
	McSurvival::AddXp(Stats, 4);
	TestEqual(TEXT("7 points make level 1"), Stats.XpLevel, 1);
	TestEqual(TEXT("with an empty bar"), Stats.XpProgress, 0.0f);
	McSurvival::AddXp(Stats, 352 - 7 + 21);
	TestEqual(TEXT("352 + 21 in all is level 16"), Stats.XpLevel, 16);
	TestEqual(TEXT("half way to 17"), Stats.XpProgress, 0.5f);
	McSurvival::AddXp(Stats, -100000);
	TestEqual(TEXT("it cannot go below zero"), Stats.XpLevel, 0);

	FMcPlayerStats Dead;
	Dead.XpLevel = 5;
	TestEqual(TEXT("death drops 7 a level"), McSurvival::GetDeathXp(Dead), 35);
	Dead.XpLevel = 30;
	TestEqual(TEXT("and at most 100"), McSurvival::GetDeathXp(Dead), 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPickupTest, "McClient.McPlayer.PickupIntoInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPickupTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	FMcEntityManager Manager(World);
	const FBox Player(FVector(4.7, 1.0, 4.7), FVector(5.3, 2.8, 5.3));

	FMcContainer Inventory(36);
	Manager.DropItem(FVector(5.0, 1.0, 5.0), S(TEXT("minecraft:dirt"), 5), 10);
	TestEqual(TEXT("items in their pickup delay stay"), McSurvival::CollectItems(Manager, Player, Inventory), 0);
	TestTrue(TEXT("and the inventory is empty"), Inventory.IsEmpty());
	for (int32 Tick = 0; Tick < 12; ++Tick)
	{
		Manager.Tick();
	}
	TestEqual(TEXT("after the delay they go in"), McSurvival::CollectItems(Manager, Player, Inventory), 5);
	TestEqual(TEXT("the inventory holds them"), Inventory.CountItem(TEXT("minecraft:dirt")), 5);
	Manager.Tick();
	int32 Left = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		Left += Entity->GetType() == FMcItemEntity::StaticType() ? 1 : 0;
	}
	TestEqual(TEXT("and the entity is gone"), Left, 0);

	// They merge into a started stack.
	Manager.DropItem(FVector(5.0, 1.0, 5.0), S(TEXT("minecraft:dirt"), 60), 0);
	TestEqual(TEXT("60 more"), McSurvival::CollectItems(Manager, Player, Inventory), 60);
	TestEqual(TEXT("65 dirt"), Inventory.CountItem(TEXT("minecraft:dirt")), 65);
	TestEqual(TEXT("in two stacks, 64 and 1"), Inventory.Get(0).Count + Inventory.Get(1).Count, 65);

	// A full inventory leaves them on the ground, and a nearly full one takes what fits.
	FMcContainer Full(36);
	for (int32 Slot = 0; Slot < 36; ++Slot)
	{
		Full.Set(Slot, S(TEXT("minecraft:cobblestone"), 64));
	}
	Full.Set(35, S(TEXT("minecraft:dirt"), 60));
	Manager.DropItem(FVector(5.0, 1.0, 5.0), S(TEXT("minecraft:dirt"), 10), 0);
	TestEqual(TEXT("only 4 fit"), McSurvival::CollectItems(Manager, Player, Full), 4);
	TestEqual(TEXT("the stack is full"), Full.Get(35).Count, 64);
	TestEqual(TEXT("again, none fit"), McSurvival::CollectItems(Manager, Player, Full), 0);
	TArray<FMcEntity*> Near;
	Manager.FindInBox(Player.ExpandBy(1.0), Near);
	int32 Remaining = 0;
	for (FMcEntity* Entity : Near)
	{
		if (Entity->GetType() == FMcItemEntity::StaticType() && !Entity->bRemoved)
		{
			Remaining += static_cast<FMcItemEntity*>(Entity)->Stack.Count;
		}
	}
	TestEqual(TEXT("6 stay on the ground"), Remaining, 6);

	// Out of reach stays out of reach.
	FMcContainer Far(36);
	Manager.DropItem(FVector(12.0, 1.0, 12.0), S(TEXT("minecraft:stone"), 3), 0);
	McSurvival::CollectItems(Manager, Player, Far);
	TestEqual(TEXT("items far away are not taken"), Far.CountItem(TEXT("minecraft:stone")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMenuRpcTest, "McClient.McPlayer.MenuRpcValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcMenuRpcTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	using McMenuRpc::EResult;
	const TSharedPtr<FMcContainer> Inventory = MakeShared<FMcContainer>(36);
	const TSharedPtr<FMcContainer> Armor = MakeShared<FMcContainer>(4);
	const TSharedPtr<FMcContainer> Offhand = MakeShared<FMcContainer>(1);
	FMcPlayerMenu Menu(Inventory, Armor, Offhand);
	const int32 Window = 0;
	const int32 Pickup = static_cast<int32>(EMcClickType::Pickup);
	const int32 QuickMove = static_cast<int32>(EMcClickType::QuickMove);
	const int32 Swap = static_cast<int32>(EMcClickType::Swap);
	const int32 Throw = static_cast<int32>(EMcClickType::Throw);
	const int32 Clone = static_cast<int32>(EMcClickType::Clone);
	const int32 QuickCraft = static_cast<int32>(EMcClickType::QuickCraft);
	const int32 PickupAll = static_cast<int32>(EMcClickType::PickupAll);

	TestEqual(TEXT("an ordinary click"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 0, Pickup, false), EResult::Ok);
	TestEqual(TEXT("a right click"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 1, Pickup, false), EResult::Ok);
	TestEqual(TEXT("a shift click"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 0, QuickMove, false), EResult::Ok);
	TestEqual(TEXT("a number key"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 8, Swap, false), EResult::Ok);
	TestEqual(TEXT("Q"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 1, Throw, false), EResult::Ok);
	TestEqual(TEXT("a double click"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 0, PickupAll, false), EResult::Ok);
	TestEqual(TEXT("outside the window throws the cursor stack"), McMenuRpc::ValidateClick(Menu, Window, Window, McMenuRpc::OutsideSlot, 0, Pickup, false), EResult::Ok);

	TestEqual(TEXT("a click for a window that is not open"), McMenuRpc::ValidateClick(Menu, Window, 3, 10, 0, Pickup, false), EResult::WrongWindow);
	TestEqual(TEXT("a click with no window open"), McMenuRpc::ValidateClick(Menu, -1, Window, 10, 0, Pickup, false), EResult::WrongWindow);
	TestEqual(TEXT("a slot past the end"), McMenuRpc::ValidateClick(Menu, Window, Window, Menu.NumSlots(), 0, Pickup, false), EResult::BadSlot);
	TestEqual(TEXT("a negative slot"), McMenuRpc::ValidateClick(Menu, Window, Window, -5, 0, Pickup, false), EResult::BadSlot);
	TestEqual(TEXT("outside is only for plain clicks"), McMenuRpc::ValidateClick(Menu, Window, Window, McMenuRpc::OutsideSlot, 0, QuickMove, false), EResult::BadSlot);
	TestEqual(TEXT("a button that is not a button"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 7, Pickup, false), EResult::BadButton);
	TestEqual(TEXT("a number key past 8"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 9, Swap, false), EResult::BadButton);
	TestEqual(TEXT("a type that does not exist"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 0, 99, false), EResult::BadType);
	TestEqual(TEXT("drags have their own message"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 0, QuickCraft, false), EResult::BadType);
	TestEqual(TEXT("clone is creative only"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 0, Clone, false), EResult::NotAllowed);
	TestEqual(TEXT("and fine in creative"), McMenuRpc::ValidateClick(Menu, Window, Window, 10, 0, Clone, true), EResult::Ok);

	TArray<int32> Slots = { 10, 11, 12 };
	TestEqual(TEXT("a drag"), McMenuRpc::ValidateDrag(Menu, Window, Window, 0, Slots, false), EResult::Ok);
	TestEqual(TEXT("a one-each drag"), McMenuRpc::ValidateDrag(Menu, Window, Window, 1, Slots, false), EResult::Ok);
	TestEqual(TEXT("the creative drag is for creative"), McMenuRpc::ValidateDrag(Menu, Window, Window, 2, Slots, false), EResult::NotAllowed);
	TestEqual(TEXT("and allowed there"), McMenuRpc::ValidateDrag(Menu, Window, Window, 2, Slots, true), EResult::Ok);
	TestEqual(TEXT("a drag in a window that is gone"), McMenuRpc::ValidateDrag(Menu, Window, 4, 0, Slots, false), EResult::WrongWindow);
	TestEqual(TEXT("a drag button that is not one"), McMenuRpc::ValidateDrag(Menu, Window, Window, 5, Slots, false), EResult::BadButton);
	TArray<int32> Bad = { 10, 999 };
	TestEqual(TEXT("a drag over a slot that is not there"), McMenuRpc::ValidateDrag(Menu, Window, Window, 0, Bad, false), EResult::BadSlot);
	TestEqual(TEXT("an empty drag"), McMenuRpc::ValidateDrag(Menu, Window, Window, 0, TArray<int32>(), false), EResult::BadSlot);
	TArray<int32> Many;
	for (int32 Index = 0; Index < McMenuRpc::MaxDragSlots + 1; ++Index)
	{
		Many.Add(10);
	}
	TestEqual(TEXT("a drag over too many slots"), McMenuRpc::ValidateDrag(Menu, Window, Window, 0, Many, false), EResult::BadSlot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBlockBreakTest, "McClient.McPlayer.BreakingBlocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBlockBreakTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Dirt = State(TEXT("minecraft:dirt"));
	const FMcStateId Bedrock = State(TEXT("minecraft:bedrock"));
	const FMcStateId Obsidian = State(TEXT("minecraft:obsidian"));
	const FMcItemStack Hand;

	TestEqual(TEXT("stone's hardness"), McBlockBreak::GetHardness(Stone), 1.5f);
	TestEqual(TEXT("dirt's hardness"), McBlockBreak::GetHardness(Dirt), 0.5f);
	TestEqual(TEXT("obsidian's hardness"), McBlockBreak::GetHardness(Obsidian), 50.0f);
	TestFalse(TEXT("bedrock does not break"), McBlockBreak::IsBreakable(Bedrock));
	TestTrue(TEXT("stone does"), McBlockBreak::IsBreakable(Stone));

	// By hand: dirt 0.75 s (15 ticks), stone 7.5 s (150 ticks, as nothing is harvested).
	TestEqual(TEXT("dirt by hand"), McBlockBreak::GetBreakTicks(Hand, Dirt, true, false), 15);
	TestEqual(TEXT("stone by hand"), McBlockBreak::GetBreakTicks(Hand, Stone, true, false), 150);
	TestEqual(TEXT("bedrock never"), McBlockBreak::GetBreakTicks(Hand, Bedrock, true, false), 0);
	TestTrue(TEXT("stone needs a tool"), McBlockBreak::RequiresTool(Stone));
	TestFalse(TEXT("dirt does not"), McBlockBreak::RequiresTool(Dirt));

	// With the right tool: wooden pickaxe 1.15 s, stone pickaxe 0.6 s... in ticks, 23 and 12.
	const FMcItemStack Wooden = S(TEXT("minecraft:wooden_pickaxe"));
	const FMcItemStack StonePick = S(TEXT("minecraft:stone_pickaxe"));
	const FMcItemStack IronPick = S(TEXT("minecraft:iron_pickaxe"));
	const FMcItemStack Shovel = S(TEXT("minecraft:wooden_shovel"));
	TestTrue(TEXT("a pickaxe harvests stone"), McBlockBreak::CanHarvest(Wooden, Stone));
	TestFalse(TEXT("a hand does not"), McBlockBreak::CanHarvest(Hand, Stone));
	TestFalse(TEXT("a shovel does not"), McBlockBreak::CanHarvest(Shovel, Stone));
	TestEqual(TEXT("stone with a wooden pickaxe"), McBlockBreak::GetBreakTicks(Wooden, Stone, true, false), 23);
	TestEqual(TEXT("stone with a stone pickaxe"), McBlockBreak::GetBreakTicks(StonePick, Stone, true, false), 12);
	TestEqual(TEXT("dirt with a shovel is quicker"), McBlockBreak::GetBreakTicks(Shovel, Dirt, true, false), 8);
	TestEqual(TEXT("obsidian needs a diamond pickaxe"), McBlockBreak::CanHarvest(IronPick, Obsidian), false);
	// Off the ground or with the head under water: five times slower.
	TestEqual(TEXT("in the air, 5 times slower"), McBlockBreak::GetBreakTicks(Hand, Dirt, false, false), 75);
	TestEqual(TEXT("with the head under water, 5 times slower"), McBlockBreak::GetBreakTicks(Hand, Dirt, true, true), 75);
	TestEqual(TEXT("both, 25 times"), McBlockBreak::GetBreakTicks(Hand, Dirt, false, true), 375);

	// Efficiency adds level^2 + 1 to the speed.
	FMcItemStack Efficient = Wooden;
	TSharedRef<FJsonObject> Enchants = MakeShared<FJsonObject>();
	Enchants->SetNumberField(TEXT("minecraft:efficiency"), 3);
	Efficient.Set(TEXT("minecraft:enchantments"), MakeShared<FJsonValueObject>(Enchants));
	TestTrue(TEXT("efficiency speeds up the pickaxe"), McBlockBreak::GetBreakTicks(Efficient, Stone, true, false) < 23);

	// Tools wear one per block; a hand does not.
	TestEqual(TEXT("a pickaxe wears 1"), McBlockBreak::GetToolWear(Wooden, Stone), 1);
	TestEqual(TEXT("a hand does not"), McBlockBreak::GetToolWear(Hand, Stone), 0);

	// Drops through the jar's loot tables, with the tool as context.
	FRandomStream Random(7);
	TArray<FMcItemStack> Drops;
	McBlockBreak::GetDrops(Stone, Hand, Random, Drops);
	TestEqual(TEXT("stone by hand drops nothing"), Drops.Num(), 0);
	McBlockBreak::GetDrops(Stone, Wooden, Random, Drops);
	if (TestEqual(TEXT("stone with a pickaxe drops one thing"), Drops.Num(), 1))
	{
		TestEqual(TEXT("cobblestone"), Drops[0].Item, FName(TEXT("minecraft:cobblestone")));
	}
	Drops.Reset();
	McBlockBreak::GetDrops(State(TEXT("minecraft:grass_block")), Hand, Random, Drops);
	if (TestEqual(TEXT("grass drops dirt"), Drops.Num(), 1))
	{
		TestEqual(TEXT("dirt"), Drops[0].Item, FName(TEXT("minecraft:dirt")));
	}
	Drops.Reset();
	McBlockBreak::GetDrops(State(TEXT("minecraft:diamond_ore")), Wooden, Random, Drops);
	TestEqual(TEXT("diamond ore with a wooden pickaxe: nothing"), Drops.Num(), 0);
	McBlockBreak::GetDrops(State(TEXT("minecraft:diamond_ore")), IronPick, Random, Drops);
	if (TestEqual(TEXT("with an iron one: a diamond"), Drops.Num(), 1))
	{
		TestEqual(TEXT("diamond"), Drops[0].Item, FName(TEXT("minecraft:diamond")));
	}

	// Items and blocks.
	TestEqual(TEXT("the torch item"), McBlockBreak::GetItemForBlock(State(TEXT("minecraft:wall_torch[facing=north]"))), FName(TEXT("minecraft:torch")));
	TestTrue(TEXT("dirt places dirt"), McBlockBreak::GetBlockForItem(TEXT("minecraft:dirt")).IsSet());
	TestFalse(TEXT("bread places nothing"), McBlockBreak::GetBlockForItem(TEXT("minecraft:bread")).IsSet());
	TestTrue(TEXT("a wall torch is the torch item's block"), McBlockBreak::IsBlockOfItem(State(TEXT("minecraft:wall_torch[facing=north]")), TEXT("minecraft:torch")));
	TestFalse(TEXT("dirt is not stone's"), McBlockBreak::IsBlockOfItem(Dirt, TEXT("minecraft:stone")));
	return true;
}

#endif
