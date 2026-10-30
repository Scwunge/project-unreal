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
	TestEqual(TEXT("a good one never goes below one"), McTrading::GetCost(Priced, 100), 1);
	Priced.Uses = 16;
	Priced.MaxUses = 16;
	McTrading::Restock(Priced);
	TestEqual(TEXT("a trade used up has demand 16"), Priced.Demand, 16);
	TestEqual(TEXT("which raises the price by a base times demand times the multiplier"), McTrading::GetCost(Priced, 0), 36);
	TestEqual(TEXT("and a good reputation takes some off"), McTrading::GetCost(Priced, 5), 31);
	Priced.Demand = 0;
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

	// What a villager thinks of a player is kept, and runs between its limits.
	Villager.AddReputation(TEXT("Alex"), 30);
	Villager.AddReputation(TEXT("Alex"), -5);
	Villager.AddReputation(TEXT("Steve"), -500);
	TestEqual(TEXT("opinion adds up"), Villager.GetReputation(TEXT("Alex")), 25);
	TestEqual(TEXT("and has a floor"), Villager.GetReputation(TEXT("Steve")), -200);
	TestEqual(TEXT("a stranger is neutral"), Villager.GetReputation(TEXT("Nobody")), 0);
	Bytes.Reset();
	{
		FMemoryWriter Writer(Bytes);
		Villager.Serialize(Writer);
	}
	FMcVillager Remembered(*Type);
	FMemoryReader RememberedReader(Bytes);
	TestTrue(TEXT("it loads again"), Remembered.Serialize(RememberedReader));
	TestEqual(TEXT("with the opinion"), Remembered.GetReputation(TEXT("Alex")), 25);

	// A zombie villager keeps the villager, and needs weakness and a golden apple to be cured.
	const FMcMobType* ZombieType = FMcMobType::Find(FMcZombieVillager::StaticType());
	if (TestNotNull(TEXT("zombie villager type"), ZombieType))
	{
		FMcZombieVillager Zombie(*ZombieType);
		Zombie.Become(Villager);
		TestEqual(TEXT("it keeps the profession"), Zombie.GetProfession(), FName(TEXT("minecraft:farmer")));
		TestFalse(TEXT("no cure without weakness"), Zombie.StartCure(TEXT("Alex"), 10));
		Zombie.Weaken();
		TestTrue(TEXT("weakened, the apple starts it"), Zombie.StartCure(TEXT("Alex"), 3));
		TestFalse(TEXT("only once"), Zombie.StartCure(TEXT("Alex"), 3));
		TestFalse(TEXT("not done yet"), Zombie.IsCured());
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			Zombie.AdvanceCure();
		}
		TestTrue(TEXT("done after the ticks"), Zombie.IsCured());
		TArray<uint8> ZombieBytes;
		{
			FMemoryWriter Writer(ZombieBytes);
			Zombie.Serialize(Writer);
		}
		FMcZombieVillager ZombieLoaded(*ZombieType);
		FMemoryReader ZombieReader(ZombieBytes);
		TestTrue(TEXT("it loads"), ZombieLoaded.Serialize(ZombieReader));
		TestTrue(TEXT("still curing"), ZombieLoaded.IsCured());
		FMcVillager Cured(*Type);
		FMemoryReaderView VillagerReader(ZombieLoaded.GetVillagerData());
		TestTrue(TEXT("the villager comes back"), Cured.Serialize(VillagerReader));
		TestEqual(TEXT("with its trades"), Cured.GetOffers().Num(), Villager.GetOffers().Num());
		TestEqual(TEXT("and its opinions"), Cured.GetReputation(TEXT("Alex")), 25);
	}

	// The wandering trader sells nine trades from its three sets, and keeps them in a save.
	const FMcMobType* TraderType = FMcMobType::Find(FMcWanderingTrader::StaticType());
	if (TestNotNull(TEXT("trader type"), TraderType))
	{
		FMcWanderingTrader Trader(*TraderType);
		Trader.Id = 5;
		TestTrue(TEXT("it has trades"), Trader.GetOffers().Num() > 0);
		Trader.GetOffers()[0].Uses = 1;
		TArray<uint8> TraderBytes;
		{
			FMemoryWriter Writer(TraderBytes);
			Trader.Serialize(Writer);
		}
		FMcWanderingTrader TraderLoaded(*TraderType);
		FMemoryReader TraderReader(TraderBytes);
		TestTrue(TEXT("it loads"), TraderLoaded.Serialize(TraderReader));
		TestEqual(TEXT("with the trades"), TraderLoaded.GetOffers().Num(), Trader.GetOffers().Num());
		TestEqual(TEXT("and their use"), TraderLoaded.GetOffers()[0].Uses, 1);
	}
	return true;
}

#endif
