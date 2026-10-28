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
	return true;
}

#endif
