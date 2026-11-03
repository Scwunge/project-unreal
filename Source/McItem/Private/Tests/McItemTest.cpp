#include "Dom/JsonObject.h"
#include "McContainer.h"
#include "McCraftingMenu.h"
#include "McFurnace.h"
#include "McItemRegistry.h"
#include "McItemStack.h"
#include "McRecipe.h"
#include "McPot.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FMcItemStack S(const TCHAR* Name, int32 Count = 1)
	{
		return FMcItemStack(FName(Name), Count);
	}

	TArray<FMcItemStack> Grid(int32 Size, std::initializer_list<const TCHAR*> Names)
	{
		TArray<FMcItemStack> Result;
		Result.SetNum(Size * Size);
		int32 Index = 0;
		for (const TCHAR* Name : Names)
		{
			if (*Name)
			{
				Result[Index] = S(Name);
			}
			++Index;
		}
		return Result;
	}

	TSharedPtr<FMcContainer> Player()
	{
		return MakeShared<FMcContainer>(36);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcItemRegistryTest, "McClient.McItem.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcItemRegistryTest::RunTest(const FString& Parameters)
{
	FMcItemRegistry& Items = FMcItemRegistry::Get();
	if (!TestTrue(TEXT("items load"), Items.EnsureLoaded()))
	{
		return false;
	}
	TestTrue(TEXT("there are well over a thousand items"), Items.NumItems() > 1000);
	const FMcItemDef* Stone = Items.Find(TEXT("minecraft:stone"));
	if (TestNotNull(TEXT("stone exists"), Stone))
	{
		TestTrue(TEXT("the protocol id finds it again"), Items.FindById(Stone->Id) == Stone);
	}
	TestFalse(TEXT("nonsense doesn't exist"), Items.Exists(TEXT("minecraft:nonsense")));

	TestEqual(TEXT("stone stacks to 64"), Items.GetMaxStackSize(TEXT("minecraft:stone")), 64);
	TestEqual(TEXT("ender pearls stack to 16"), Items.GetMaxStackSize(TEXT("minecraft:ender_pearl")), 16);
	TestEqual(TEXT("swords don't stack"), Items.GetMaxStackSize(TEXT("minecraft:diamond_sword")), 1);
	TestEqual(TEXT("a diamond pickaxe lasts 1561"), Items.GetMaxDamage(TEXT("minecraft:diamond_pickaxe")), 1561);
	TestEqual(TEXT("stone doesn't wear out"), Items.GetMaxDamage(TEXT("minecraft:stone")), 0);

	FMcFoodData Food;
	TestTrue(TEXT("bread is food"), Items.GetFood(TEXT("minecraft:bread"), Food));
	TestEqual(TEXT("bread feeds 5"), Food.Nutrition, 5);
	TestFalse(TEXT("stone isn't food"), Items.GetFood(TEXT("minecraft:stone"), Food));

	FMcToolData Tool;
	TestTrue(TEXT("an iron pickaxe is a tool"), Items.GetTool(TEXT("minecraft:iron_pickaxe"), Tool));
	TestTrue(TEXT("it has rules"), Tool.Rules.Num() > 0);

	FMcArmorData Armor;
	TestTrue(TEXT("a diamond chestplate is armour"), Items.GetArmor(TEXT("minecraft:diamond_chestplate"), Armor));
	TestEqual(TEXT("it goes on the chest"), Armor.Slot, FName("chest"));
	TestEqual(TEXT("it gives 8 armour"), Armor.Armor, 8.0);
	TestEqual(TEXT("and 2 toughness"), Armor.Toughness, 2.0);

	TestEqual(TEXT("coal burns 1600 ticks"), Items.GetBurnTime(TEXT("minecraft:coal")), 1600);
	TestEqual(TEXT("a lava bucket burns 20000"), Items.GetBurnTime(TEXT("minecraft:lava_bucket")), 20000);
	TestEqual(TEXT("planks burn 300"), Items.GetBurnTime(TEXT("minecraft:oak_planks")), 300);
	TestEqual(TEXT("stone doesn't burn"), Items.GetBurnTime(TEXT("minecraft:stone")), 0);
	TestEqual(TEXT("coal burns half as long in a blast furnace"), Items.GetBurnTime(TEXT("minecraft:coal"), true), 800);
	TestEqual(TEXT("normal fuel cooks at 1x"), Items.GetFuelSpeed(TEXT("minecraft:coal"), false), 1.0f);
	TestEqual(TEXT("and at 2x in a fast furnace"), Items.GetFuelSpeed(TEXT("minecraft:coal"), true), 2.0f);
	TestEqual(TEXT("milk leaves a bucket"), Items.GetUseRemainder(TEXT("minecraft:milk_bucket")), FName("minecraft:bucket"));
	TestTrue(TEXT("an iron sword can be enchanted at a table"), Items.GetEnchantability(TEXT("minecraft:iron_sword")) > 0);

	TestTrue(TEXT("logs are a tag"), Items.GetTag(TEXT("#minecraft:logs")) != nullptr);
	TestTrue(TEXT("oak logs are logs"), Items.HasTag(TEXT("minecraft:oak_log"), TEXT("minecraft:logs")));
	TestTrue(TEXT("a reference with # matches by tag"), Items.Matches(TEXT("minecraft:oak_planks"), TEXT("#minecraft:planks")));
	TestFalse(TEXT("stone isn't planks"), Items.Matches(TEXT("minecraft:stone"), TEXT("#minecraft:planks")));
	TestTrue(TEXT("nested tags are resolved (logs hold the oak logs, burning logs too)"), Items.HasTag(TEXT("minecraft:oak_log"), TEXT("minecraft:logs_that_burn")));

	TestTrue(TEXT("enchantments load"), Items.GetEnchantments().Num() >= 40);
	const FMcEnchantmentDef* Sharpness = Items.FindEnchantment(TEXT("minecraft:sharpness"));
	if (TestNotNull(TEXT("sharpness exists"), Sharpness))
	{
		TestEqual(TEXT("sharpness goes to V"), Sharpness->MaxLevel, 5);
		TestEqual(TEXT("it costs 1 at level 1"), Sharpness->MinCost(1), 1);
	}
	TestTrue(TEXT("sharpness goes on a diamond sword"), Items.CanEnchant(TEXT("minecraft:diamond_sword"), TEXT("minecraft:sharpness")));
	TestFalse(TEXT("not on stone"), Items.CanEnchant(TEXT("minecraft:stone"), TEXT("minecraft:sharpness")));
	TestFalse(TEXT("sharpness and smite don't mix"), Items.AreCompatible(TEXT("minecraft:sharpness"), TEXT("minecraft:smite")));
	TestTrue(TEXT("sharpness and unbreaking do"), Items.AreCompatible(TEXT("minecraft:sharpness"), TEXT("minecraft:unbreaking")));
	TArray<FName> ForSword;
	Items.GetEnchantmentsFor(TEXT("minecraft:diamond_sword"), ForSword);
	TestTrue(TEXT("a sword takes several"), ForSword.Num() >= 5 && ForSword.Contains(FName("minecraft:looting")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcItemStackTest, "McClient.McItem.Stack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcItemStackTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcItemStack Sword = S(TEXT("minecraft:diamond_sword"));
	TestTrue(TEXT("a sword can be damaged"), Sword.IsDamageable());
	TestEqual(TEXT("new it has no damage"), Sword.GetDamage(), 0);
	Sword.SetDamage(100);
	TestEqual(TEXT("then it has 100"), Sword.GetDamage(), 100);
	TestEqual(TEXT("it's not new"), Sword.IsDamaged(), true);
	FMcItemStack Fresh = S(TEXT("minecraft:diamond_sword"));
	TestFalse(TEXT("a worn sword isn't a new one"), Sword.IsSameItemSameComponents(Fresh));
	Sword.SetDamage(0);
	TestTrue(TEXT("repaired, it is again"), Sword.IsSameItemSameComponents(Fresh) && Sword.Components.IsEmpty());
	TestFalse(TEXT("a hit doesn't break it"), Sword.HurtAndBreak(10));
	TestEqual(TEXT("it took 10"), Sword.GetDamage(), 10);
	TestTrue(TEXT("a huge one does"), Sword.HurtAndBreak(100000));

	FMcItemStack Book = S(TEXT("minecraft:diamond_pickaxe"));
	TMap<FName, int32> Enchantments;
	Enchantments.Add(TEXT("minecraft:efficiency"), 3);
	Enchantments.Add(TEXT("minecraft:unbreaking"), 2);
	Book.SetEnchantments(Enchantments);
	Book.SetCustomName(TEXT("Digger"));
	Book.SetDamage(7);
	TestEqual(TEXT("enchantments read back"), Book.GetEnchantmentLevel(TEXT("minecraft:efficiency")), 3);
	TestEqual(TEXT("the name reads back"), Book.GetCustomName(), FString(TEXT("Digger")));

	// ---- Saving ----
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		TestTrue(TEXT("it saves"), Book.Serialize(Writer));
	}
	FMcItemStack Loaded;
	{
		FMemoryReader Reader(Bytes);
		TestTrue(TEXT("it loads"), Loaded.Serialize(Reader));
	}
	TestTrue(TEXT("what loads is what saved"), Loaded == Book);
	TestEqual(TEXT("with its damage"), Loaded.GetDamage(), 7);
	TestEqual(TEXT("its enchantments"), Loaded.GetEnchantments().Num(), 2);
	TArray<uint8> Garbage = { 1, 0, 0, 0, 'x' };
	FMemoryReader GarbageReader(Garbage);
	FMcItemStack Bad;
	TestFalse(TEXT("garbage doesn't load"), Bad.Serialize(GarbageReader));

	// ---- Stacking ----
	FMcItemStack Stone = S(TEXT("minecraft:stone"), 60);
	TestEqual(TEXT("60 stone has room for 4"), Stone.GetSpaceFor(S(TEXT("minecraft:stone"), 10)), 4);
	TestEqual(TEXT("not for dirt"), Stone.GetSpaceFor(S(TEXT("minecraft:dirt"), 10)), 0);
	FMcItemStack Empty;
	TestEqual(TEXT("an empty slot takes up to a stack"), Empty.GetSpaceFor(S(TEXT("minecraft:stone"), 100)), 64);
	TestEqual(TEXT("pearls take 16"), Empty.GetSpaceFor(S(TEXT("minecraft:ender_pearl"), 30)), 16);
	TestEqual(TEXT("a slot limit of 1 holds"), Empty.GetSpaceFor(S(TEXT("minecraft:stone"), 30), 1), 1);
	const FMcItemStack Half = Stone.Split(25);
	TestEqual(TEXT("splitting takes the amount"), Half.Count, 25);
	TestEqual(TEXT("and leaves the rest"), Stone.Count, 35);
	const FMcItemStack All = Stone.Split(500);
	TestTrue(TEXT("splitting everything empties it"), Stone.IsEmpty() && All.Count == 35);

	FMcItemStack Named = S(TEXT("minecraft:stone"));
	Named.SetCustomName(TEXT("x"));
	TestFalse(TEXT("different components don't stack"), Named.IsSameItemSameComponents(S(TEXT("minecraft:stone"))));
	Named.Reset(TEXT("minecraft:custom_name"));
	TestTrue(TEXT("but equal ones do"), Named.IsSameItemSameComponents(S(TEXT("minecraft:stone"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRecipeTest, "McClient.McItem.Recipes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcRecipeTest::RunTest(const FString& Parameters)
{
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (!TestTrue(TEXT("recipes load"), Book.EnsureLoaded()))
	{
		return false;
	}
	TestTrue(TEXT("over a thousand recipes"), Book.Num() > 1000);
	AddInfo(FString::Printf(TEXT("%d recipes loaded, %d skipped"), Book.Num(), Book.NumSkipped()));
	TestEqual(TEXT("no recipe was dropped"), Book.NumSkipped(), 0);
	FMcCraftingMatch Match;

	// ---- Crafting ----
	TestTrue(TEXT("planks make sticks in a 3x3"), Book.FindCraftingMatch(Grid(3, { TEXT(""), TEXT("minecraft:oak_planks"), TEXT(""), TEXT(""), TEXT("minecraft:oak_planks") }), 3, 3, Match));
	TestEqual(TEXT("4 sticks"), Match.Result.Item.ToString() + FString::FromInt(Match.Result.Count), FString(TEXT("minecraft:stick4")));
	TestTrue(TEXT("they do in a 2x2 as well, and shifted"), Book.FindCraftingMatch(Grid(2, { TEXT(""), TEXT("minecraft:birch_planks"), TEXT(""), TEXT("minecraft:birch_planks") }), 2, 2, Match));
	TestFalse(TEXT("a diagonal doesn't"), Book.FindCraftingMatch(Grid(3, { TEXT("minecraft:oak_planks"), TEXT(""), TEXT(""), TEXT(""), TEXT("minecraft:oak_planks") }), 3, 3, Match));
	TestTrue(TEXT("a crafting table from 4 planks in 2x2"), Book.FindCraftingMatch(Grid(2, { TEXT("minecraft:oak_planks"), TEXT("minecraft:oak_planks"), TEXT("minecraft:oak_planks"), TEXT("minecraft:oak_planks") }), 2, 2, Match) && Match.Result.Item == FName("minecraft:crafting_table"));
	TestTrue(TEXT("a pickaxe"), Book.FindCraftingMatch(Grid(3, { TEXT("minecraft:cobblestone"), TEXT("minecraft:cobblestone"), TEXT("minecraft:cobblestone"), TEXT(""), TEXT("minecraft:stick"), TEXT(""), TEXT(""), TEXT("minecraft:stick") }), 3, 3, Match) && Match.Result.Item == FName("minecraft:stone_pickaxe"));
	// A shaped recipe may be mirrored: an axe either way round.
	TestTrue(TEXT("an axe one way"), Book.FindCraftingMatch(Grid(3, { TEXT("minecraft:cobblestone"), TEXT("minecraft:cobblestone"), TEXT(""), TEXT("minecraft:cobblestone"), TEXT("minecraft:stick"), TEXT(""), TEXT(""), TEXT("minecraft:stick") }), 3, 3, Match) && Match.Result.Item == FName("minecraft:stone_axe"));
	TestTrue(TEXT("an axe the other"), Book.FindCraftingMatch(Grid(3, { TEXT(""), TEXT("minecraft:cobblestone"), TEXT("minecraft:cobblestone"), TEXT(""), TEXT("minecraft:stick"), TEXT("minecraft:cobblestone"), TEXT(""), TEXT("minecraft:stick") }), 3, 3, Match) && Match.Result.Item == FName("minecraft:stone_axe"));
	TestFalse(TEXT("an extra item spoils it"), Book.FindCraftingMatch(Grid(3, { TEXT("minecraft:cobblestone"), TEXT("minecraft:cobblestone"), TEXT("minecraft:cobblestone"), TEXT("minecraft:dirt"), TEXT("minecraft:stick"), TEXT(""), TEXT(""), TEXT("minecraft:stick") }), 3, 3, Match));
	TestTrue(TEXT("shapeless: any order"), Book.FindCraftingMatch(Grid(2, { TEXT("minecraft:flint"), TEXT("minecraft:iron_ingot") }), 2, 2, Match) && Match.Result.Item == FName("minecraft:flint_and_steel"));
	TestTrue(TEXT("shapeless the other way"), Book.FindCraftingMatch(Grid(2, { TEXT("minecraft:iron_ingot"), TEXT(""), TEXT("minecraft:flint") }), 2, 2, Match) && Match.Result.Item == FName("minecraft:flint_and_steel"));
	TestTrue(TEXT("a dye mixes by tag"), Book.FindCraftingMatch(Grid(2, { TEXT("minecraft:red_dye"), TEXT("minecraft:white_dye") }), 2, 2, Match) && Match.Result.Item == FName("minecraft:pink_dye"));
	TestFalse(TEXT("an empty grid makes nothing"), Book.FindCraftingMatch(Grid(3, {}), 3, 3, Match));

	// A bundle dyed: the result keeps the old item's components.
	TArray<FMcItemStack> Dyeing = Grid(2, { TEXT("minecraft:bundle"), TEXT("minecraft:black_dye") });
	Dyeing[0].SetCustomName(TEXT("Mine"));
	TestTrue(TEXT("dyeing a bundle (transmute)"), Book.FindCraftingMatch(Dyeing, 2, 2, Match) && Match.Result.Item == FName("minecraft:black_bundle"));
	TestEqual(TEXT("it keeps its name"), Match.Result.GetCustomName(), FString(TEXT("Mine")));

	// Remainders: a cake leaves its buckets.
	TArray<FMcItemStack> Cake = Grid(3, { TEXT("minecraft:milk_bucket"), TEXT("minecraft:milk_bucket"), TEXT("minecraft:milk_bucket"), TEXT("minecraft:sugar"), TEXT("minecraft:egg"), TEXT("minecraft:sugar"), TEXT("minecraft:wheat"), TEXT("minecraft:wheat"), TEXT("minecraft:wheat") });
	TestTrue(TEXT("a cake"), Book.FindCraftingMatch(Cake, 3, 3, Match) && Match.Result.Item == FName("minecraft:cake"));
	TArray<FMcItemStack> Remainders;
	FMcRecipeBook::GetRemainders(Match.Recipe, Cake, Remainders);
	TestEqual(TEXT("three empty buckets stay"), Remainders[0].Item, FName("minecraft:bucket"));
	TestTrue(TEXT("and nothing else"), Remainders[3].IsEmpty() && Remainders[8].IsEmpty());

	// A decorated pot from four bricks.
	TestTrue(TEXT("a decorated pot"), Book.FindCraftingMatch(Grid(3, { TEXT(""), TEXT("minecraft:brick"), TEXT(""), TEXT("minecraft:brick"), TEXT(""), TEXT("minecraft:brick"), TEXT(""), TEXT("minecraft:brick") }), 3, 3, Match) && Match.Result.Item == FName("minecraft:decorated_pot"));
	// With sherds, the pot keeps them: the top is its back, the left its left, the right its right, the bottom its front.
	TestTrue(TEXT("a decorated pot with sherds"), Book.FindCraftingMatch(Grid(3, { TEXT(""), TEXT("minecraft:angler_pottery_sherd"), TEXT(""), TEXT("minecraft:archer_pottery_sherd"), TEXT(""), TEXT("minecraft:brick"), TEXT(""), TEXT("minecraft:blade_pottery_sherd") }), 3, 3, Match));
	const TArray<FName> PotSides = McPot::GetSides(Match.Result);
	TestTrue(TEXT("its sides back, left, right and front"), PotSides == TArray<FName>({ TEXT("minecraft:angler_pottery_sherd"), TEXT("minecraft:archer_pottery_sherd"), TEXT("minecraft:brick"), TEXT("minecraft:blade_pottery_sherd") }));
	TestEqual(TEXT("a sherd's pattern"), McPot::GetPattern(TEXT("minecraft:arms_up_pottery_sherd")), FString(TEXT("entity/decorated_pot/arms_up_pottery_pattern")));
	FMcItemStack Plain(TEXT("minecraft:decorated_pot"));
	McPot::SetSides(Plain, { TEXT("minecraft:brick"), TEXT("minecraft:brick"), TEXT("minecraft:brick"), TEXT("minecraft:brick") });
	TestFalse(TEXT("a plain pot keeps no sides of its own"), Plain.Components.Contains(TEXT("minecraft:pot_decorations")));

	// ---- The specials ----
	TArray<FMcItemStack> Repair = Grid(2, { TEXT("minecraft:iron_pickaxe"), TEXT("minecraft:iron_pickaxe") });
	Repair[0].SetDamage(200);
	Repair[1].SetDamage(150);
	TestTrue(TEXT("two worn tools combine"), Book.FindCraftingMatch(Repair, 2, 2, Match) && Match.Result.Item == FName("minecraft:iron_pickaxe"));
	const int32 Max = FMcItemRegistry::Get().GetMaxDamage(TEXT("minecraft:iron_pickaxe"));
	TestEqual(TEXT("with the durability added and a bonus of a twentieth"), Match.Result.GetDamage(), Max - ((Max - 200) + (Max - 150) + Max / 20));

	TArray<FMcItemStack> Dye = Grid(2, { TEXT("minecraft:leather_boots"), TEXT("minecraft:red_dye") });
	TestTrue(TEXT("dyeing leather armour"), Book.FindCraftingMatch(Dye, 2, 2, Match) && Match.Result.Has(TEXT("minecraft:dyed_color")));

	TArray<FMcItemStack> Banners = Grid(2, { TEXT("minecraft:white_banner"), TEXT("minecraft:white_banner") });
	TArray<TSharedPtr<FJsonValue>> Patterns;
	Patterns.Add(MakeShared<FJsonValueString>(TEXT("x")));
	Banners[0].Set(TEXT("minecraft:banner_patterns"), MakeShared<FJsonValueArray>(Patterns));
	TestTrue(TEXT("a banner is copied"), Book.FindCraftingMatch(Banners, 2, 2, Match) && Match.Result.Item == FName("minecraft:white_banner") && Match.Result.Has(TEXT("minecraft:banner_patterns")));
	FMcRecipeBook::GetRemainders(Match.Recipe, Banners, Remainders);
	TestTrue(TEXT("and the original is kept"), Remainders[0].Item == FName("minecraft:white_banner") && Remainders[1].IsEmpty());

	// ---- Cooking ----
	const FMcRecipe* Smelt = Book.FindCooking(EMcRecipeType::Smelting, S(TEXT("minecraft:raw_iron")));
	if (TestNotNull(TEXT("iron smelts"), Smelt))
	{
		TestEqual(TEXT("into an ingot"), Smelt->Result.Item, FName("minecraft:iron_ingot"));
		TestEqual(TEXT("in 200 ticks"), Smelt->CookingTime, 200);
		TestEqual(TEXT("worth 0.7 experience"), Smelt->Experience, 0.7f);
	}
	TestNotNull(TEXT("and blasts"), Book.FindCooking(EMcRecipeType::Blasting, S(TEXT("minecraft:raw_iron"))));
	TestNull(TEXT("but food doesn't blast"), Book.FindCooking(EMcRecipeType::Blasting, S(TEXT("minecraft:beef"))));
	TestNotNull(TEXT("beef smokes"), Book.FindCooking(EMcRecipeType::Smoking, S(TEXT("minecraft:beef"))));
	TestNotNull(TEXT("and cooks on a campfire"), Book.FindCooking(EMcRecipeType::CampfireCooking, S(TEXT("minecraft:beef"))));
	TestNull(TEXT("nothing cooks stone bricks"), Book.FindCooking(EMcRecipeType::Smelting, S(TEXT("minecraft:diamond"))));

	// ---- Stonecutting, smithing, brewing ----
	TArray<const FMcRecipe*> Cuts;
	Book.FindStonecutting(S(TEXT("minecraft:stone")), Cuts);
	TestTrue(TEXT("stone cuts into several things"), Cuts.Num() >= 4);

	TestTrue(TEXT("netherite upgrade"), Book.FindSmithing(S(TEXT("minecraft:netherite_upgrade_smithing_template")), S(TEXT("minecraft:diamond_sword")), S(TEXT("minecraft:netherite_ingot")), Match) && Match.Result.Item == FName("minecraft:netherite_sword"));
	FMcItemStack Worn = S(TEXT("minecraft:diamond_sword"));
	Worn.SetDamage(50);
	Book.FindSmithing(S(TEXT("minecraft:netherite_upgrade_smithing_template")), Worn, S(TEXT("minecraft:netherite_ingot")), Match);
	TestEqual(TEXT("the upgrade keeps the wear"), Match.Result.GetDamage(), 50);
	TestFalse(TEXT("the wrong template does nothing"), Book.FindSmithing(S(TEXT("minecraft:stick")), S(TEXT("minecraft:diamond_sword")), S(TEXT("minecraft:netherite_ingot")), Match));
	TestTrue(TEXT("a trim"), Book.FindSmithing(S(TEXT("minecraft:coast_armor_trim_smithing_template")), S(TEXT("minecraft:iron_helmet")), S(TEXT("minecraft:amethyst_shard")), Match) && Match.Result.Has(TEXT("minecraft:trim")));

	FMcItemStack Brewed;
	FMcItemStack Water = S(TEXT("minecraft:potion"));
	TSharedRef<FJsonObject> WaterContents = MakeShared<FJsonObject>();
	WaterContents->SetStringField(TEXT("potion"), TEXT("minecraft:water"));
	Water.Set(TEXT("minecraft:potion_contents"), MakeShared<FJsonValueObject>(WaterContents));
	TestTrue(TEXT("nether wart brews an awkward potion"), Book.FindBrewing(Water, S(TEXT("minecraft:nether_wart")), Brewed) && Brewed.Item == FName("minecraft:potion"));
	TestTrue(TEXT("blaze powder is a reagent"), Book.IsBrewingReagent(S(TEXT("minecraft:blaze_powder"))));
	TestFalse(TEXT("dirt isn't"), Book.IsBrewingReagent(S(TEXT("minecraft:dirt"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcContainerTest, "McClient.McItem.Containers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcContainerTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("recipes load"), FMcRecipeBook::Get().EnsureLoaded()))
	{
		return false;
	}
	TArray<FMcItemStack> Dropped;

	// ---- A chest ----
	const TSharedPtr<FMcContainer> Chest = MakeShared<FMcContainer>(27);
	const TSharedPtr<FMcContainer> Inventory = Player();
	FMcContainerMenu Menu(Chest, Inventory);
	TestEqual(TEXT("27 and 36 slots"), Menu.NumSlots(), 63);
	Chest->Set(0, S(TEXT("minecraft:stone"), 40));
	Chest->Set(1, S(TEXT("minecraft:dirt"), 10));

	Menu.Click(0, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("left click picks up the stack"), Menu.GetCarried().Count == 40 && Menu.GetStack(0).IsEmpty());
	Menu.Click(2, 1, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("right click puts one down"), Menu.GetStack(2).Count == 1 && Menu.GetCarried().Count == 39);
	Menu.Click(2, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("left click on the same adds the rest"), Menu.GetStack(2).Count == 40 && Menu.GetCarried().IsEmpty());
	Menu.Click(2, 1, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("right click takes half, rounded up"), Menu.GetCarried().Count == 20 && Menu.GetStack(2).Count == 20);
	Menu.Click(1, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("a different stack swaps"), Menu.GetCarried().Item == FName("minecraft:dirt") && Menu.GetStack(1).Item == FName("minecraft:stone"));
	Menu.Click(-999, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("clicking outside drops the cursor"), Dropped.Num() == 1 && Dropped[0].Count == 10 && Menu.GetCarried().IsEmpty());
	Dropped.Reset();

	// Shift-click goes to the player, from the end, and back.
	Menu.Click(2, 0, EMcClickType::QuickMove, Dropped);
	TestTrue(TEXT("shift-click moves to the hotbar end"), Menu.GetStack(2).IsEmpty() && Inventory->Get(8).Count == 20);
	Menu.Click(27 + 27 + 8, 0, EMcClickType::QuickMove, Dropped);
	TestTrue(TEXT("and back into the chest"), Menu.GetStack(2).Count == 20 || Menu.GetStack(1).Count == 40);

	// Merging respects the limit.
	Chest->Clear();
	Inventory->Clear();
	Chest->Set(0, S(TEXT("minecraft:stone"), 64));
	Inventory->Set(0, S(TEXT("minecraft:stone"), 60));
	Inventory->Set(1, S(TEXT("minecraft:stone"), 30));
	Menu.Click(0, 0, EMcClickType::QuickMove, Dropped);
	TestEqual(TEXT("all of the stone went in"), Inventory->CountItem(TEXT("minecraft:stone")), 154);
	TestTrue(TEXT("topping up the partial stacks first"), Inventory->Get(0).Count == 64 && Inventory->Get(1).Count == 64);

	// Number keys swap with the hotbar.
	Chest->Clear();
	Inventory->Clear();
	Chest->Set(3, S(TEXT("minecraft:dirt"), 5));
	Inventory->Set(4, S(TEXT("minecraft:stone"), 2));
	Menu.Click(3, 4, EMcClickType::Swap, Dropped);
	TestTrue(TEXT("a number key swaps with that hotbar slot"), Chest->Get(3).Item == FName("minecraft:stone") && Inventory->Get(4).Item == FName("minecraft:dirt"));

	// Double click gathers.
	Chest->Clear();
	Inventory->Clear();
	Chest->Set(0, S(TEXT("minecraft:dirt"), 10));
	Chest->Set(5, S(TEXT("minecraft:dirt"), 20));
	Inventory->Set(2, S(TEXT("minecraft:dirt"), 40));
	Menu.Click(0, 0, EMcClickType::Pickup, Dropped);
	Menu.Click(0, 0, EMcClickType::PickupAll, Dropped);
	TestEqual(TEXT("a double click gathers to a stack"), Menu.GetCarried().Count, 64);
	Menu.Close(Dropped);
	TestEqual(TEXT("closing gives the cursor stack back"), Chest->CountItem(TEXT("minecraft:dirt")) + Inventory->CountItem(TEXT("minecraft:dirt")), 70);

	// A drag across slots.
	Chest->Clear();
	Inventory->Clear();
	Menu.SetCarried(S(TEXT("minecraft:cobblestone"), 10));
	Menu.BeginQuickCraft(0);
	for (const int32 Slot : { 0, 1, 2 })
	{
		Menu.AddQuickCraftSlot(Slot);
	}
	Menu.EndQuickCraft();
	TestTrue(TEXT("a drag splits 10 over 3 slots as 3 each"), Chest->Get(0).Count == 3 && Chest->Get(1).Count == 3 && Chest->Get(2).Count == 3 && Menu.GetCarried().Count == 1);
	Menu.BeginQuickCraft(1);
	for (const int32 Slot : { 3, 4 })
	{
		Menu.AddQuickCraftSlot(Slot);
	}
	Menu.EndQuickCraft();
	TestTrue(TEXT("a right drag puts one in each"), Chest->Get(3).Count == 1 && Chest->Get(4).Count == 0 && Menu.GetCarried().IsEmpty());

	// ---- Crafting ----
	const TSharedPtr<FMcContainer> Backpack = Player();
	FMcCraftingMenu Table(3, Backpack);
	Table.SetStack(1 + 1, S(TEXT("minecraft:oak_planks"), 3));
	Table.SetStack(1 + 4, S(TEXT("minecraft:oak_planks"), 3));
	TestEqual(TEXT("two planks above one another make sticks"), Table.GetStack(0).Item, FName("minecraft:stick"));
	Table.Click(0, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("taking the result takes 4 sticks"), Table.GetCarried().Count == 4 && Table.GetStack(2).Count == 2 && Table.GetStack(5).Count == 2);
	TestEqual(TEXT("and a second batch is ready"), Table.GetStack(0).Count, 4);
	Table.Click(0, 0, EMcClickType::QuickMove, Dropped);
	TestEqual(TEXT("shift-click crafts all it can"), Backpack->CountItem(TEXT("minecraft:stick")), 8);
	TestTrue(TEXT("until the planks run out"), Table.GetStack(0).IsEmpty() && Table.GetStack(2).IsEmpty());
	Table.SetStack(1, S(TEXT("minecraft:milk_bucket")));
	Table.SetStack(2, S(TEXT("minecraft:milk_bucket")));
	Table.SetStack(3, S(TEXT("minecraft:milk_bucket")));
	Table.SetStack(4, S(TEXT("minecraft:sugar")));
	Table.SetStack(5, S(TEXT("minecraft:egg")));
	Table.SetStack(6, S(TEXT("minecraft:sugar")));
	for (const int32 Slot : { 7, 8, 9 })
	{
		Table.SetStack(Slot, S(TEXT("minecraft:wheat")));
	}
	Table.Click(-999, 0, EMcClickType::Pickup, Dropped);
	Table.Click(0, 0, EMcClickType::Pickup, Dropped);
	TestEqual(TEXT("a cake"), Table.GetCarried().Item, FName("minecraft:cake"));
	TestEqual(TEXT("leaves the buckets in the grid"), Table.GetStack(1).Item, FName("minecraft:bucket"));
	Table.Click(-999, 0, EMcClickType::Pickup, Dropped);
	Table.Close(Dropped);
	TestEqual(TEXT("closing returns the buckets"), Backpack->CountItem(TEXT("minecraft:bucket")), 3);

	// ---- A furnace ----
	const TSharedPtr<FMcFurnace> Furnace = MakeShared<FMcFurnace>(EMcFurnaceKind::Furnace);
	Furnace->GetItems()->Set(FMcFurnace::InputSlot, S(TEXT("minecraft:raw_iron"), 2));
	Furnace->GetItems()->Set(FMcFurnace::FuelSlot, S(TEXT("minecraft:coal"), 1));
	TestFalse(TEXT("unlit at first"), Furnace->IsLit());
	Furnace->Tick();
	TestTrue(TEXT("it takes the coal"), Furnace->IsLit() && Furnace->GetItems()->Get(FMcFurnace::FuelSlot).IsEmpty());
	TestEqual(TEXT("which burns 1600"), Furnace->GetLitTotalTime(), 1600);
	for (int32 Tick = 0; Tick < 198; ++Tick)
	{
		Furnace->Tick();
	}
	TestTrue(TEXT("200 ticks in: not yet"), Furnace->GetItems()->Get(FMcFurnace::OutputSlot).IsEmpty());
	Furnace->Tick();
	TestEqual(TEXT("then an ingot"), Furnace->GetItems()->Get(FMcFurnace::OutputSlot).Item, FName("minecraft:iron_ingot"));
	TestEqual(TEXT("one raw iron is left"), Furnace->GetItems()->Get(FMcFurnace::InputSlot).Count, 1);
	for (int32 Tick = 0; Tick < 200; ++Tick)
	{
		Furnace->Tick();
	}
	TestEqual(TEXT("and the second one comes"), Furnace->GetItems()->Get(FMcFurnace::OutputSlot).Count, 2);
	TestTrue(TEXT("the experience comes with it"), Furnace->TakeExperience(0.0f) >= 1);
	TestEqual(TEXT("and only once"), Furnace->TakeExperience(0.0f), 0);

	// A blast furnace is twice as fast.
	FMcFurnace Blast(EMcFurnaceKind::BlastFurnace);
	Blast.GetItems()->Set(FMcFurnace::InputSlot, S(TEXT("minecraft:raw_iron"), 1));
	Blast.GetItems()->Set(FMcFurnace::FuelSlot, S(TEXT("minecraft:coal"), 1));
	for (int32 Tick = 0; Tick < 101; ++Tick)
	{
		Blast.Tick();
	}
	TestEqual(TEXT("a blast furnace smelts in 100 ticks"), Blast.GetItems()->Get(FMcFurnace::OutputSlot).Item, FName("minecraft:iron_ingot"));
	// Nothing smelts into a full output of another item.
	FMcFurnace Blocked;
	Blocked.GetItems()->Set(FMcFurnace::InputSlot, S(TEXT("minecraft:raw_iron")));
	Blocked.GetItems()->Set(FMcFurnace::FuelSlot, S(TEXT("minecraft:coal")));
	Blocked.GetItems()->Set(FMcFurnace::OutputSlot, S(TEXT("minecraft:stone")));
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Blocked.Tick();
	}
	TestFalse(TEXT("a blocked furnace doesn't burn fuel"), Blocked.IsLit());
	// A lava bucket leaves a bucket.
	FMcFurnace Lava;
	Lava.GetItems()->Set(FMcFurnace::InputSlot, S(TEXT("minecraft:raw_iron")));
	Lava.GetItems()->Set(FMcFurnace::FuelSlot, S(TEXT("minecraft:lava_bucket")));
	Lava.Tick();
	TestEqual(TEXT("a lava bucket leaves a bucket"), Lava.GetItems()->Get(FMcFurnace::FuelSlot).Item, FName("minecraft:bucket"));

	// Saving.
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		TestTrue(TEXT("a furnace saves"), Furnace->Serialize(Writer));
	}
	FMcFurnace Back;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("and loads"), Back.Serialize(Reader));
	TestTrue(TEXT("with its output"), Back.GetItems()->Get(FMcFurnace::OutputSlot) == Furnace->GetItems()->Get(FMcFurnace::OutputSlot));

	// The menu: shift-click sends ore to the input and coal to the fuel.
	const TSharedPtr<FMcContainer> Mine = Player();
	const TSharedPtr<FMcFurnace> Fresh = MakeShared<FMcFurnace>();
	FMcFurnaceMenu FurnaceMenu(Fresh, Mine);
	Mine->Set(0, S(TEXT("minecraft:raw_iron"), 5));
	Mine->Set(1, S(TEXT("minecraft:coal"), 5));
	Mine->Set(2, S(TEXT("minecraft:stone"), 5));
	FurnaceMenu.Click(3 + 27, 0, EMcClickType::QuickMove, Dropped);
	FurnaceMenu.Click(3 + 28, 0, EMcClickType::QuickMove, Dropped);
	FurnaceMenu.Click(3 + 29, 0, EMcClickType::QuickMove, Dropped);
	TestEqual(TEXT("the ore went to the input"), Fresh->GetItems()->Get(FMcFurnace::InputSlot).Item, FName("minecraft:raw_iron"));
	TestEqual(TEXT("the coal to the fuel"), Fresh->GetItems()->Get(FMcFurnace::FuelSlot).Item, FName("minecraft:coal"));
	TestTrue(TEXT("the stone stayed out"), Fresh->GetItems()->Get(FMcFurnace::OutputSlot).IsEmpty());
	FurnaceMenu.SetCarried(S(TEXT("minecraft:stone")));
	FurnaceMenu.Click(2, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("nothing can be put in the output"), Fresh->GetItems()->Get(FMcFurnace::OutputSlot).IsEmpty());
	return true;
}

#endif
