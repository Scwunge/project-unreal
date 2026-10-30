#include "McCache.h"
#include "McItemRegistry.h"
#include "McRandom.h"
#include "McTrading.h"
#include "McVillager.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTradingTest, "McClient.McEntity.Trading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcTradingTest::RunTest(const FString& Parameters)
{
	if (!IFileManager::Get().FileExists(*McCache::Data(TEXT("trade_set/farmer/level_1.json"))) || !FMcItemRegistry::Get().EnsureLoaded())
	{
		AddInfo(TEXT("No cache/: skipped"));
		return true;
	}
	// A farmer's first set is two of the five trades of the tag, each wanting or giving what the data says.
	FMcRandom Random(5);
	TArray<McTrading::FOffer> Offers;
	TestTrue(TEXT("farmer level 1 exists"), McTrading::RollLevel(TEXT("minecraft:farmer"), 1, TEXT("minecraft:plains"), Random, Offers));
	TestEqual(TEXT("two trades"), Offers.Num(), 2);
	for (const McTrading::FOffer& Offer : Offers)
	{
		const bool bBuys = Offer.Gives.Item == TEXT("minecraft:emerald");
		TestTrue(TEXT("one side is emeralds"), bBuys || Offer.Wants.Item == TEXT("minecraft:emerald"));
		TestTrue(TEXT("a trade can be made"), Offer.MaxUses >= 12 && Offer.Xp >= 1);
	}
	TestTrue(TEXT("the two differ"), Offers[0].Wants.Item != Offers[1].Wants.Item || Offers[0].Gives.Item != Offers[1].Gives.Item);

	// A librarian's book is enchanted, and the price follows reputation.
	TArray<McTrading::FOffer> Library;
	for (int32 Seed = 0; Seed < 20 && Library.IsEmpty(); ++Seed)
	{
		FMcRandom Local(Seed);
		TArray<McTrading::FOffer> Try;
		McTrading::RollLevel(TEXT("minecraft:librarian"), 1, TEXT("minecraft:plains"), Local, Try);
		for (McTrading::FOffer& Offer : Try)
		{
			if (Offer.Gives.Item == TEXT("minecraft:enchanted_book"))
			{
				Library.Add(Offer);
			}
		}
	}
	if (TestEqual(TEXT("a librarian sells an enchanted book"), Library.Num(), 1))
	{
		TestTrue(TEXT("with an enchantment on it"), Library[0].Gives.Has(TEXT("minecraft:stored_enchantments")) || Library[0].Gives.Has(TEXT("minecraft:enchantments")));
	}
	McTrading::FOffer Priced;
	Priced.Wants = FMcItemStack(TEXT("minecraft:wheat"), 20);
	Priced.Gives = FMcItemStack(TEXT("minecraft:emerald"), 1);
	Priced.Discount = 0.05f;
	TestEqual(TEXT("no reputation, no discount"), McTrading::GetCost(Priced, 0), 20);
	TestEqual(TEXT("a good one halves nothing below one"), McTrading::GetCost(Priced, 100), 1);
	TestTrue(TEXT("a bad one costs more"), McTrading::GetCost(Priced, -4) > 20);

	// A villager makes its trades from its profession, levels up with the experience of the trades and keeps them in a save.
	const FMcMobType* Type = FMcMobType::Find(FMcVillager::StaticType());
	if (!TestNotNull(TEXT("villager type"), Type))
	{
		return false;
	}
	FMcVillager Villager(*Type);
	Villager.SetProfession(TEXT("minecraft:farmer"));
	TestEqual(TEXT("two at first"), Villager.GetOffers().Num(), 2);
	Villager.GetOffers()[0].Uses = 3;
	Villager.AddTradeXp(10);
	TestEqual(TEXT("apprentice"), Villager.GetLevel(), 2);
	TestTrue(TEXT("more trades at the new level"), Villager.GetOffers().Num() > 2);
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		Villager.Serialize(Writer);
	}
	FMcVillager Loaded(*Type);
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("it loads"), Loaded.Serialize(Reader));
	TestEqual(TEXT("with its trades"), Loaded.GetOffers().Num(), Villager.GetOffers().Num());
	TestEqual(TEXT("and how often they were made"), Loaded.GetOffers()[0].Uses, 3);
	Villager.Restock();
	TestEqual(TEXT("restocked"), Villager.GetOffers()[0].Uses, 0);
	return true;
}

#endif
