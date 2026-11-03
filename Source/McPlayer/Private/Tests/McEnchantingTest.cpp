#include "McEnchanting.h"
#include "McEnchantMenu.h"
#include "McItemRegistry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEnchantingTest, "McClient.McPlayer.Enchanting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcEnchantingTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const FName Sword(TEXT("minecraft:diamond_sword"));
	const FName Book(TEXT("minecraft:book"));
	const FName LapisLazuli(TEXT("minecraft:lapis_lazuli"));

	// Bookshelves: a full ring of 16 positions on two levels counts at most 15; a blocked gap hides the shelf behind it.
	TestEqual(TEXT("a full ring counts 15"), McEnchanting::CountBookshelves([](const FIntVector&) { return true; }, [](const FIntVector&) { return true; }), 15);
	TestEqual(TEXT("nothing clear counts none"), McEnchanting::CountBookshelves([](const FIntVector&) { return true; }, [](const FIntVector&) { return false; }), 0);
	const int32 One = McEnchanting::CountBookshelves(
		[](const FIntVector& At) { return At == FIntVector(2, 0, 0); },
		[](const FIntVector& At) { return At == FIntVector(1, 0, 0); });
	TestEqual(TEXT("one shelf with its gap clear"), One, 1);

	TestTrue(TEXT("the table has enchantments"), McEnchanting::GetTableOptions().Num() > 10);

	// Offers: the same for the same seed; 15 shelves put the bottom at 30; nothing for a stick.
	const McEnchanting::FOffers Full = McEnchanting::GetOffers(Sword, 15, 1234);
	const McEnchanting::FOffers Again = McEnchanting::GetOffers(Sword, 15, 1234);
	TestEqual(TEXT("15 shelves: the bottom option costs 30"), Full.Costs[2], 30);
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		TestEqual(TEXT("same seed, same cost"), Full.Costs[Slot], Again.Costs[Slot]);
		TestTrue(TEXT("same seed, same clue"), Full.Clues[Slot] == Again.Clues[Slot]);
		TestTrue(TEXT("each option has a clue"), !Full.Clues[Slot].Name.IsNone());
	}
	TestTrue(TEXT("costs go up"), Full.Costs[0] <= Full.Costs[1] && Full.Costs[1] <= Full.Costs[2]);
	const McEnchanting::FOffers None = McEnchanting::GetOffers(TEXT("minecraft:stick"), 15, 1234);
	TestEqual(TEXT("a stick gets nothing"), None.Costs[0] + None.Costs[1] + None.Costs[2], 0);
	const McEnchanting::FOffers Bare = McEnchanting::GetOffers(Sword, 0, 99);
	TestTrue(TEXT("no shelves: at most 8"), Bare.Costs[2] <= 8);

	// The clue is among what the option gives, and the same option gives the same.
	const TArray<McEnchanting::FEnchant> Given = McEnchanting::GetTableEnchantments(Sword, 1234, 2, Full.Costs[2]);
	TestTrue(TEXT("the clue is given"), Given.Contains(Full.Clues[2]));
	TestTrue(TEXT("repeatable"), Given == McEnchanting::GetTableEnchantments(Sword, 1234, 2, Full.Costs[2]));
	for (const McEnchanting::FEnchant& Enchant : Given)
	{
		TestTrue(TEXT("a sword can take what it's given"), FMcItemRegistry::Get().CanEnchant(Sword, Enchant.Name));
	}

	// The menu: lapis only in its slot; enchanting takes Slot + 1 lapis and gives the levels to spend.
	const TSharedPtr<FMcContainer> Inventory = MakeShared<FMcContainer>(36);
	FMcEnchantMenu Menu(Inventory, 15, 1234);
	FMcItemStack Item;
	Item.Item = Sword;
	Item.Count = 1;
	Menu.SetStack(FMcEnchantMenu::ItemSlot, Item);
	TestEqual(TEXT("the menu offers what GetOffers does"), Menu.GetOffers().Costs[2], 30);
	TestFalse(TEXT("no lapis, no enchanting"), Menu.CanEnchant(0, 100, false));
	TestTrue(TEXT("creative needs no lapis"), Menu.CanEnchant(0, 0, true));
	FMcItemStack Fuel;
	Fuel.Item = LapisLazuli;
	Fuel.Count = 5;
	Menu.SetStack(FMcEnchantMenu::LapisSlot, Fuel);
	TestFalse(TEXT("too few levels"), Menu.CanEnchant(2, 29, false));
	TestTrue(TEXT("enough levels"), Menu.CanEnchant(2, 30, false));
	int32 Spent = 0;
	TestTrue(TEXT("enchants"), Menu.Enchant(2, 30, false, Spent));
	TestEqual(TEXT("three levels"), Spent, 3);
	TestEqual(TEXT("three lapis used"), Menu.GetStack(FMcEnchantMenu::LapisSlot).Count, 2);
	TestTrue(TEXT("the sword is enchanted"), Menu.GetStack(FMcEnchantMenu::ItemSlot).IsEnchanted());
	TestEqual(TEXT("with what was offered"), Menu.GetStack(FMcEnchantMenu::ItemSlot).GetEnchantments().Num(), Given.Num());
	TestEqual(TEXT("an enchanted sword gets no more offers"), Menu.GetOffers().Costs[2], 0);

	// A book becomes an enchanted book that stores them.
	FMcEnchantMenu BookMenu(Inventory, 15, 77);
	FMcItemStack Plain;
	Plain.Item = Book;
	Plain.Count = 1;
	BookMenu.SetStack(FMcEnchantMenu::ItemSlot, Plain);
	TestTrue(TEXT("a book has offers"), BookMenu.GetOffers().Costs[0] > 0);
	TestTrue(TEXT("a book enchants"), BookMenu.Enchant(0, 0, true, Spent));
	const FMcItemStack& Done = BookMenu.GetStack(FMcEnchantMenu::ItemSlot);
	TestEqual(TEXT("it becomes an enchanted book"), Done.Item, FName(TEXT("minecraft:enchanted_book")));
	TestTrue(TEXT("storing at least one"), McEnchanting::GetEnchantments(Done).Num() >= 1);
	TestEqual(TEXT("creative spends no levels"), Spent, 0);
	return true;
}

#endif
