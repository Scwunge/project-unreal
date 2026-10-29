#include "McItemRegistry.h"
#include "McRecipe.h"
#include "McWorkstationMenus.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorkstationTest, "McClient.McItem.Workstations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWorkstationTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()) || !TestTrue(TEXT("recipes load"), FMcRecipeBook::Get().EnsureLoaded()))
	{
		return false;
	}
	const TSharedPtr<FMcContainer> Inventory = MakeShared<FMcContainer>(36);
	TArray<FMcItemStack> Dropped;

	// ---- Stonecutter: stone cuts into 11 things; choosing one fills the result; taking it uses one stone. ----
	{
		FMcStonecutterMenu Menu(Inventory);
		const int32 PlayerStart = 2;
		Inventory->Set(0, FMcItemStack(TEXT("minecraft:stone"), 3));
		int32 StoneSlot = PlayerStart;
		while (StoneSlot < Menu.NumSlots() && Menu.GetStack(StoneSlot).IsEmpty())
		{
			++StoneSlot;
		}
		Menu.Click(StoneSlot, 0, EMcClickType::QuickMove, Dropped);
		TestEqual(TEXT("shift-click puts stone in the input"), Menu.GetStack(FMcStonecutterMenu::InputSlot).Count, 3);
		TestEqual(TEXT("stone has 11 cuts"), Menu.GetRecipes().Num(), 11);
		TestTrue(TEXT("nothing chosen yet"), Menu.GetStack(FMcStonecutterMenu::ResultSlot).IsEmpty());
		int32 Slab = -1;
		for (int32 Index = 0; Index < Menu.GetRecipes().Num(); ++Index)
		{
			if (Menu.GetRecipes()[Index]->Result.Item == TEXT("minecraft:stone_slab"))
			{
				Slab = Index;
			}
		}
		TestTrue(TEXT("slabs are a cut"), Slab >= 0);
		TestFalse(TEXT("no such button"), Menu.ClickButton(99));
		TestTrue(TEXT("choose slabs"), Menu.ClickButton(Slab));
		TestEqual(TEXT("two slabs"), Menu.GetStack(FMcStonecutterMenu::ResultSlot), FMcItemStack(TEXT("minecraft:stone_slab"), 2));

		// The choice reaches another copy of the menu (the owner's) with the window.
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Menu.SerializeState(Writer);
		FMcStonecutterMenu Copy(MakeShared<FMcContainer>(36));
		Copy.SetStack(FMcStonecutterMenu::InputSlot, Menu.GetStack(FMcStonecutterMenu::InputSlot));
		FMemoryReader Reader(Bytes);
		Copy.SerializeState(Reader);
		TestEqual(TEXT("the copy has the choice"), Copy.GetSelected(), Slab);

		Menu.Click(FMcStonecutterMenu::ResultSlot, 0, EMcClickType::Pickup, Dropped);
		TestEqual(TEXT("the slabs are carried"), Menu.GetCarried(), FMcItemStack(TEXT("minecraft:stone_slab"), 2));
		TestEqual(TEXT("one stone used"), Menu.GetStack(FMcStonecutterMenu::InputSlot).Count, 2);
		TestEqual(TEXT("still chosen, so more is ready"), Menu.GetStack(FMcStonecutterMenu::ResultSlot).Count, 2);
		Menu.SetCarried(FMcItemStack());

		// Shift-clicking the result cuts the rest of the input into the inventory.
		Menu.Click(FMcStonecutterMenu::ResultSlot, 0, EMcClickType::QuickMove, Dropped);
		TestEqual(TEXT("slabs in the inventory"), Inventory->CountItem(TEXT("minecraft:stone_slab")), 4);
		TestTrue(TEXT("and uses the stone"), Menu.GetStack(FMcStonecutterMenu::InputSlot).IsEmpty());

		// A different item clears the choice.
		Menu.SetStack(FMcStonecutterMenu::InputSlot, FMcItemStack(TEXT("minecraft:granite"), 1));
		TestEqual(TEXT("granite clears it"), Menu.GetSelected(), -1);
		TestTrue(TEXT("and the result"), Menu.GetStack(FMcStonecutterMenu::ResultSlot).IsEmpty());

		Menu.Close(Dropped);
		TestEqual(TEXT("the input goes back on closing"), Inventory->CountItem(TEXT("minecraft:granite")), 1);
	}

	// ---- Smithing: a template, a diamond sword and netherite make a netherite sword that keeps its damage. ----
	{
		Inventory->Clear();
		FMcSmithingMenu Menu(Inventory);
		TestEqual(TEXT("templates go first"), FMcSmithingMenu::FindInputSlot(FMcItemStack(TEXT("minecraft:netherite_upgrade_smithing_template"))), FMcSmithingMenu::TemplateSlot);
		TestEqual(TEXT("swords second"), FMcSmithingMenu::FindInputSlot(FMcItemStack(TEXT("minecraft:diamond_sword"))), FMcSmithingMenu::BaseSlot);
		TestEqual(TEXT("netherite third"), FMcSmithingMenu::FindInputSlot(FMcItemStack(TEXT("minecraft:netherite_ingot"))), FMcSmithingMenu::AdditionSlot);
		TestEqual(TEXT("dirt nowhere"), FMcSmithingMenu::FindInputSlot(FMcItemStack(TEXT("minecraft:dirt"))), -1);

		FMcItemStack Sword(TEXT("minecraft:diamond_sword"));
		Sword.SetDamage(100);
		Menu.SetStack(FMcSmithingMenu::TemplateSlot, FMcItemStack(TEXT("minecraft:netherite_upgrade_smithing_template")));
		Menu.SetStack(FMcSmithingMenu::BaseSlot, Sword);
		TestTrue(TEXT("no result without the netherite"), Menu.GetStack(FMcSmithingMenu::ResultSlot).IsEmpty());
		Menu.SetStack(FMcSmithingMenu::AdditionSlot, FMcItemStack(TEXT("minecraft:netherite_ingot"), 2));
		const FMcItemStack& Result = Menu.GetStack(FMcSmithingMenu::ResultSlot);
		TestEqual(TEXT("a netherite sword"), Result.Item, FName(TEXT("minecraft:netherite_sword")));
		TestEqual(TEXT("that keeps its damage"), Result.GetDamage(), 100);
		Menu.Click(FMcSmithingMenu::ResultSlot, 0, EMcClickType::QuickMove, Dropped);
		TestEqual(TEXT("into the inventory"), Inventory->CountItem(TEXT("minecraft:netherite_sword")), 1);
		TestTrue(TEXT("the template is used"), Menu.GetStack(FMcSmithingMenu::TemplateSlot).IsEmpty());
		TestTrue(TEXT("the sword is used"), Menu.GetStack(FMcSmithingMenu::BaseSlot).IsEmpty());
		TestEqual(TEXT("one netherite is used"), Menu.GetStack(FMcSmithingMenu::AdditionSlot).Count, 1);
		Menu.Close(Dropped);
		TestEqual(TEXT("the netherite goes back on closing"), Inventory->CountItem(TEXT("minecraft:netherite_ingot")), 1);
	}

	// ---- Grindstone ----
	{
		Inventory->Clear();
		FMcItemStack Sword(TEXT("minecraft:iron_sword"));
		TMap<FName, int32> Enchants;
		Enchants.Add(TEXT("minecraft:sharpness"), 3);
		Enchants.Add(TEXT("minecraft:vanishing_curse"), 1);
		Sword.SetEnchantments(Enchants);
		const FMcItemStack Ground = FMcGrindstoneMenu::GetResult(Sword, FMcItemStack());
		TestEqual(TEXT("sharpness goes"), Ground.GetEnchantmentLevel(TEXT("minecraft:sharpness")), 0);
		TestEqual(TEXT("the curse stays"), Ground.GetEnchantmentLevel(TEXT("minecraft:vanishing_curse")), 1);
		TestTrue(TEXT("a plain sword alone makes nothing"), FMcGrindstoneMenu::GetResult(FMcItemStack(TEXT("minecraft:iron_sword")), FMcItemStack()).IsEmpty());
		// Sharpness III's minimum cost is 1 + 2 * 11.
		TestEqual(TEXT("worth sharpness III's cost"), FMcGrindstoneMenu::GetExperienceValue(Sword, FMcItemStack()), 23);

		// Two worn swords: their durability added, with 5% of the whole more.
		FMcItemStack Worn(TEXT("minecraft:iron_sword"));
		const int32 Max = Worn.GetMaxDamage();
		Worn.SetDamage(Max - 50);
		FMcItemStack Other = Worn;
		Other.SetDamage(Max - 60);
		const FMcItemStack Joined = FMcGrindstoneMenu::GetResult(Worn, Other);
		TestEqual(TEXT("joined durability"), Max - Joined.GetDamage(), 110 + Max * 5 / 100);
		TestTrue(TEXT("different items don't join"), FMcGrindstoneMenu::GetResult(Worn, FMcItemStack(TEXT("minecraft:golden_sword"))).IsEmpty());

		// A book: its stored enchantments go, and it becomes a plain book.
		FMcItemStack Book(TEXT("minecraft:enchanted_book"));
		const TSharedRef<FJsonObject> Stored = MakeShared<FJsonObject>();
		Stored->SetNumberField(TEXT("minecraft:mending"), 1);
		Book.Set(TEXT("minecraft:stored_enchantments"), MakeShared<FJsonValueObject>(Stored));
		TestEqual(TEXT("a book loses mending"), FMcGrindstoneMenu::GetResult(Book, FMcItemStack()).Item, FName(TEXT("minecraft:book")));

		// The menu: taking the result uses both inputs and earns between half and all of the value.
		FMcGrindstoneMenu Menu(Inventory);
		Menu.Random = []() { return 0.0f; };
		Menu.SetStack(FMcGrindstoneMenu::TopSlot, Sword);
		TestFalse(TEXT("a result is ready"), Menu.GetStack(FMcGrindstoneMenu::ResultSlot).IsEmpty());
		Menu.Click(FMcGrindstoneMenu::ResultSlot, 0, EMcClickType::QuickMove, Dropped);
		TestEqual(TEXT("the sword is in the inventory"), Inventory->CountItem(TEXT("minecraft:iron_sword")), 1);
		TestTrue(TEXT("the input is used"), Menu.GetStack(FMcGrindstoneMenu::TopSlot).IsEmpty());
		TestEqual(TEXT("half the value, rounded up"), Menu.TakeExperience(), 12);
		TestEqual(TEXT("asked once"), Menu.TakeExperience(), 0);
	}

	// ---- Loom: a banner and a dye offer the 32 free patterns; the one chosen goes on in the dye's colour. ----
	{
		const TSharedPtr<FMcContainer> Bag = MakeShared<FMcContainer>(36);
		FMcLoomMenu Menu(Bag);
		TestTrue(TEXT("nothing offered at first"), Menu.GetPatterns().IsEmpty());
		Menu.SetStack(FMcLoomMenu::BannerSlot, FMcItemStack(TEXT("minecraft:white_banner"), 2));
		Menu.Click(FMcLoomMenu::BannerSlot, 0, EMcClickType::Pickup, Dropped);
		Menu.Click(FMcLoomMenu::BannerSlot, 0, EMcClickType::Pickup, Dropped);
		Menu.SetStack(FMcLoomMenu::DyeSlot, FMcItemStack(TEXT("minecraft:red_dye"), 2));
		Menu.Click(FMcLoomMenu::DyeSlot, 0, EMcClickType::Pickup, Dropped);
		Menu.Click(FMcLoomMenu::DyeSlot, 0, EMcClickType::Pickup, Dropped);
		TestEqual(TEXT("32 patterns need no item"), Menu.GetPatterns().Num(), 32);
		const int32 Border = Menu.GetPatterns().IndexOfByKey(FName(TEXT("minecraft:border")));
		TestTrue(TEXT("borders are one"), Border >= 0);
		TestFalse(TEXT("no such button"), Menu.ClickButton(99));
		TestTrue(TEXT("choose the border"), Menu.ClickButton(Border));
		const FMcItemStack Result = Menu.GetStack(FMcLoomMenu::ResultSlot);
		TestEqual(TEXT("one banner"), Result.Count, 1);
		const TSharedPtr<FJsonValue> Layers = Result.Get(TEXT("minecraft:banner_patterns"));
		TestTrue(TEXT("with one layer"), Layers.IsValid() && Layers->AsArray().Num() == 1);
		if (Layers.IsValid() && Layers->AsArray().Num() == 1)
		{
			const TSharedPtr<FJsonObject> Layer = Layers->AsArray()[0]->AsObject();
			TestEqual(TEXT("the pattern"), Layer->GetStringField(TEXT("pattern")), FString(TEXT("minecraft:border")));
			TestEqual(TEXT("in the dye's colour"), Layer->GetStringField(TEXT("color")), FString(TEXT("red")));
		}
		Menu.Click(FMcLoomMenu::ResultSlot, 0, EMcClickType::Pickup, Dropped);
		TestEqual(TEXT("the banner is carried"), Menu.GetCarried().Item, FName(TEXT("minecraft:white_banner")));
		TestEqual(TEXT("one banner used"), Menu.GetStack(FMcLoomMenu::BannerSlot).Count, 1);
		TestEqual(TEXT("one dye used"), Menu.GetStack(FMcLoomMenu::DyeSlot).Count, 1);
		TestEqual(TEXT("and the choice stays"), Menu.GetSelected(), Border);
		TestFalse(TEXT("so another is ready"), Menu.GetStack(FMcLoomMenu::ResultSlot).IsEmpty());

		// A pattern item narrows the list to its own pattern, and a banner with six layers takes no more.
		Menu.SetCarried(FMcItemStack());
		Menu.SetStack(FMcLoomMenu::PatternSlot, FMcItemStack(TEXT("minecraft:flower_banner_pattern")));
		Menu.Click(FMcLoomMenu::PatternSlot, 0, EMcClickType::Pickup, Dropped);
		Menu.Click(FMcLoomMenu::PatternSlot, 0, EMcClickType::Pickup, Dropped);
		TestEqual(TEXT("the flower pattern alone"), Menu.GetPatterns().Num(), 1);
		TestEqual(TEXT("it is the flower"), Menu.GetPatterns()[0], FName(TEXT("minecraft:flower")));
		FMcItemStack Busy(TEXT("minecraft:blue_banner"));
		TArray<TSharedPtr<FJsonValue>> Six;
		for (int32 Index = 0; Index < FMcLoomMenu::MaxPatterns; ++Index)
		{
			const TSharedRef<FJsonObject> Layer = MakeShared<FJsonObject>();
			Layer->SetStringField(TEXT("pattern"), TEXT("minecraft:cross"));
			Layer->SetStringField(TEXT("color"), TEXT("black"));
			Six.Add(MakeShared<FJsonValueObject>(Layer));
		}
		Busy.Set(TEXT("minecraft:banner_patterns"), MakeShared<FJsonValueArray>(Six));
		Menu.SetStack(FMcLoomMenu::BannerSlot, Busy);
		Menu.Click(FMcLoomMenu::BannerSlot, 0, EMcClickType::Pickup, Dropped);
		Menu.Click(FMcLoomMenu::BannerSlot, 0, EMcClickType::Pickup, Dropped);
		TestTrue(TEXT("six layers take no more"), Menu.GetPatterns().IsEmpty());
	}
	return true;
}

#endif
