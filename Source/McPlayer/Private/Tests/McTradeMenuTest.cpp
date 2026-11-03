#include "McTradeMenu.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTradeMenuTest, "McClient.McPlayer.TradeMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcTradeMenuTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FMcContainer> Inventory = MakeShared<FMcContainer>(36);
	McTrading::FOffer Offer;
	Offer.Wants = FMcItemStack(TEXT("minecraft:wheat"), 20);
	Offer.Gives = FMcItemStack(TEXT("minecraft:emerald"), 1);
	Offer.MaxUses = 2;
	Offer.Xp = 5;
	Offer.Discount = 0.05f;

	FMcTradeMenu Menu(Inventory);
	int32 Trades = 0;
	int32 TraderXp = 0;
	Menu.OnTrade = [&](int32 Index, int32 Xp)
	{
		Trades += 1;
		TraderXp += Xp;
	};
	Menu.SetTrader({ Offer }, 0, 1, 0, true);

	// Picking a trade with nothing to pay with gives nothing.
	Menu.ClickButton(0);
	TestTrue(TEXT("no result without the wheat"), Menu.GetStack(FMcTradeMenu::ResultSlot).IsEmpty());

	// With 50 wheat: the price goes into the slot, the emerald shows.
	Inventory->Set(0, FMcItemStack(TEXT("minecraft:wheat"), 50));
	Menu.ClickButton(0);
	TestEqual(TEXT("the price is taken from the inventory"), Menu.GetStack(FMcTradeMenu::WantsSlot).Count, 20);
	TestEqual(TEXT("the emerald is offered"), Menu.GetStack(FMcTradeMenu::ResultSlot).Item, FName(TEXT("minecraft:emerald")));

	// Taking the result makes the trade.
	TArray<FMcItemStack> Dropped;
	Menu.Click(FMcTradeMenu::ResultSlot, 0, EMcClickType::Pickup, Dropped);
	TestEqual(TEXT("the emerald is on the cursor"), Menu.GetCarried().Item, FName(TEXT("minecraft:emerald")));
	TestEqual(TEXT("the trade was told"), Trades, 1);
	TestEqual(TEXT("and its experience"), TraderXp, 5);
	TestEqual(TEXT("the offer's uses went up"), Menu.GetOffers()[0].Uses, 1);
	const int32 PlayerXp = Menu.TakeExperience();
	TestTrue(TEXT("the player earns 3 to 6 experience"), PlayerXp >= 3 && PlayerXp <= 6);
	TestEqual(TEXT("asked once"), Menu.TakeExperience(), 0);
	TestEqual(TEXT("the same trade is ready again"), Menu.GetStack(FMcTradeMenu::WantsSlot).Count, 20);

	// A second trade uses the last of the stock; the third is out of stock and offers nothing.
	Menu.SetCarried(FMcItemStack());
	Menu.Click(FMcTradeMenu::ResultSlot, 0, EMcClickType::Pickup, Dropped);
	TestEqual(TEXT("two trades"), Trades, 2);
	TestTrue(TEXT("out of stock offers nothing"), Menu.GetStack(FMcTradeMenu::ResultSlot).IsEmpty());

	// Closing gives back what was left in the slots.
	Menu.Close(Dropped);
	int32 Wheat = 0;
	for (int32 Slot = 0; Slot < 36; ++Slot)
	{
		if (Inventory->Get(Slot).Item == TEXT("minecraft:wheat"))
		{
			Wheat += Inventory->Get(Slot).Count;
		}
	}
	for (const FMcItemStack& Stack : Dropped)
	{
		Wheat += Stack.Item == TEXT("minecraft:wheat") ? Stack.Count : 0;
	}
	TestEqual(TEXT("10 wheat is left after two trades of 20"), Wheat, 10);

	// The window's state goes across to the client's menu as it is.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Menu.SerializeState(Writer);
	FMcTradeMenu Other(MakeShared<FMcContainer>(36));
	FMemoryReaderView Reader(Bytes);
	Other.SerializeState(Reader);
	TestFalse(TEXT("the state reads back"), Reader.IsError());
	TestEqual(TEXT("with its offers"), Other.GetOffers().Num(), 1);
	TestEqual(TEXT("and their uses"), Other.GetOffers()[0].Uses, 2);
	TestTrue(TEXT("out of stock"), Other.GetOffers()[0].IsOutOfStock());
	return true;
}

#endif
