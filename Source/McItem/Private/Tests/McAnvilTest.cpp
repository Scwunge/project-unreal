#include "McAnvilMenu.h"
#include "McItemRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcAnvilTest, "McClient.McItem.Anvil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcAnvilTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const TSharedPtr<FMcContainer> Inventory = MakeShared<FMcContainer>(36);
	TArray<FMcItemStack> Dropped;
	FMcAnvilMenu Menu(Inventory);
	Menu.SetPlayerLevels(30, false);
	const auto RepairCost = [](const FMcItemStack& Stack)
	{
		const TSharedPtr<FJsonValue> Value = Stack.Get(TEXT("minecraft:repair_cost"));
		return Value.IsValid() ? static_cast<int32>(Value->AsNumber()) : 0;
	};

	// Renaming: one level; the name is set and the penalty doesn't grow.
	Menu.SetStack(FMcAnvilMenu::LeftSlot, FMcItemStack(TEXT("minecraft:iron_sword")));
	TestTrue(TEXT("nothing to do yet"), Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty());
	TestTrue(TEXT("a name"), Menu.SetText(TEXT("Edge")));
	TestEqual(TEXT("a rename costs 1"), Menu.GetCost(), 1);
	TestEqual(TEXT("named"), Menu.GetStack(FMcAnvilMenu::ResultSlot).GetCustomName(), FString(TEXT("Edge")));
	TestEqual(TEXT("no penalty for a rename"), RepairCost(Menu.GetStack(FMcAnvilMenu::ResultSlot)), 0);
	TestFalse(TEXT("too long a name"), Menu.SetText(FString::ChrN(51, TEXT('a'))));
	Menu.Click(FMcAnvilMenu::ResultSlot, 0, EMcClickType::Pickup, Dropped);
	TestEqual(TEXT("taken"), Menu.GetCarried().GetCustomName(), FString(TEXT("Edge")));
	TestEqual(TEXT("a level spent"), Menu.TakeLevelsSpent(), 1);
	TestTrue(TEXT("the left is used"), Menu.GetStack(FMcAnvilMenu::LeftSlot).IsEmpty());
	Menu.SetCarried(FMcItemStack());

	// Repair with iron: each ingot mends a quarter (62 of 250); three for a sword missing 186.
	FMcItemStack Worn(TEXT("minecraft:iron_sword"));
	Worn.SetDamage(186);
	Menu.SetStack(FMcAnvilMenu::LeftSlot, Worn);
	Menu.SetStack(FMcAnvilMenu::RightSlot, FMcItemStack(TEXT("minecraft:iron_ingot"), 5));
	TestEqual(TEXT("mended"), Menu.GetStack(FMcAnvilMenu::ResultSlot).GetDamage(), 0);
	TestEqual(TEXT("3 ingots, 3 levels"), Menu.GetCost(), 3);
	TestEqual(TEXT("penalty 1 after"), RepairCost(Menu.GetStack(FMcAnvilMenu::ResultSlot)), 1);
	Menu.Click(FMcAnvilMenu::ResultSlot, 0, EMcClickType::Pickup, Dropped);
	TestEqual(TEXT("two ingots left"), Menu.GetStack(FMcAnvilMenu::RightSlot).Count, 2);
	Menu.SetCarried(FMcItemStack());
	Menu.SetStack(FMcAnvilMenu::RightSlot, FMcItemStack());
	Menu.TakeLevelsSpent();

	// An undamaged item can't be mended; dirt doesn't go onto a sword.
	Menu.SetStack(FMcAnvilMenu::LeftSlot, FMcItemStack(TEXT("minecraft:iron_sword")));
	Menu.SetStack(FMcAnvilMenu::RightSlot, FMcItemStack(TEXT("minecraft:iron_ingot")));
	TestTrue(TEXT("nothing to mend"), Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty());
	Menu.SetStack(FMcAnvilMenu::RightSlot, FMcItemStack(TEXT("minecraft:dirt")));
	TestTrue(TEXT("dirt does nothing"), Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty());

	// A book of Sharpness III onto a sword: Sharpness's anvil cost (1), halved for a book but at least 1, times 3.
	FMcItemStack Book(TEXT("minecraft:enchanted_book"));
	const TSharedRef<FJsonObject> Stored = MakeShared<FJsonObject>();
	Stored->SetNumberField(TEXT("minecraft:sharpness"), 3);
	Book.Set(TEXT("minecraft:stored_enchantments"), MakeShared<FJsonValueObject>(Stored));
	Menu.SetStack(FMcAnvilMenu::RightSlot, Book);
	TestEqual(TEXT("sharpness III on"), Menu.GetStack(FMcAnvilMenu::ResultSlot).GetEnchantmentLevel(TEXT("minecraft:sharpness")), 3);
	TestEqual(TEXT("costs 3"), Menu.GetCost(), 3);

	// The same level on both makes one higher.
	FMcItemStack Sharp(TEXT("minecraft:iron_sword"));
	TMap<FName, int32> Three;
	Three.Add(TEXT("minecraft:sharpness"), 3);
	Sharp.SetEnchantments(Three);
	Menu.SetStack(FMcAnvilMenu::LeftSlot, Sharp);
	TestEqual(TEXT("III and III make IV"), Menu.GetStack(FMcAnvilMenu::ResultSlot).GetEnchantmentLevel(TEXT("minecraft:sharpness")), 4);

	// Smite clashes with Sharpness: left off, a level for trying; with nothing else applied there's no result.
	FMcItemStack SmiteBook(TEXT("minecraft:enchanted_book"));
	const TSharedRef<FJsonObject> Smite = MakeShared<FJsonObject>();
	Smite->SetNumberField(TEXT("minecraft:smite"), 1);
	SmiteBook.Set(TEXT("minecraft:stored_enchantments"), MakeShared<FJsonValueObject>(Smite));
	Menu.SetStack(FMcAnvilMenu::RightSlot, SmiteBook);
	TestTrue(TEXT("smite can't go on"), Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty());

	// A heavy prior-work penalty makes it too expensive in survival, but not in creative.
	FMcItemStack Old = Sharp;
	Old.Set(TEXT("minecraft:repair_cost"), MakeShared<FJsonValueNumber>(39));
	Menu.SetStack(FMcAnvilMenu::LeftSlot, Old);
	Menu.SetStack(FMcAnvilMenu::RightSlot, Book);
	TestTrue(TEXT("too expensive"), Menu.IsTooExpensive());
	TestTrue(TEXT("no result"), Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty());
	Menu.SetPlayerLevels(0, true);
	TestFalse(TEXT("creative can"), Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty());

	// Too few levels: the result shows but can't be taken.
	Menu.SetPlayerLevels(1, false);
	Menu.SetStack(FMcAnvilMenu::LeftSlot, Sharp);
	Menu.Click(FMcAnvilMenu::ResultSlot, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("can't take it"), Menu.GetCarried().IsEmpty());
	TestFalse(TEXT("it stays"), Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty());

	Menu.Close(Dropped);
	TestEqual(TEXT("the sword goes back"), Inventory->CountItem(TEXT("minecraft:iron_sword")), 1);
	TestEqual(TEXT("and the book"), Inventory->CountItem(TEXT("minecraft:enchanted_book")), 1);
	return true;
}

#endif
