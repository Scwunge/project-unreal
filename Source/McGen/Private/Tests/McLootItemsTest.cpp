#include "HAL/FileManager.h"
#include "McBlockEntity.h"
#include "McCache.h"
#include "McItemRegistry.h"
#include "McLoot.h"
#include "McRandom.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLootItemsTest, "McClient.McGen.LootItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLootItemsTest::RunTest(const FString& Parameters)
{
	FMcItemRegistry& Items = FMcItemRegistry::Get();
	if (!TestTrue(TEXT("items load"), Items.EnsureLoaded()))
	{
		return false;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *McCache::Data(TEXT("loot_table/chests")), TEXT("*.json"), true, false);
	TestTrue(TEXT("there are chest tables"), Files.Num() > 40);

	int32 NumStacks = 0;
	int32 NumEnchanted = 0;
	int32 NumBooks = 0;
	int32 NumPotions = 0;
	int32 NumDamaged = 0;
	int32 NumNamed = 0;
	int32 NumBad = 0;
	for (const FString& File : Files)
	{
		FString Name = File;
		FPaths::MakePathRelativeTo(Name, *(McCache::Data(TEXT("loot_table")) + TEXT("/")));
		const FString Table = TEXT("minecraft:") + Name.Replace(TEXT("\\"), TEXT("/")).LeftChop(5);
		for (uint64 Seed = 1; Seed <= 60; ++Seed)
		{
			FMcBlockEntity Chest = FMcBlockEntity::MakeContainer(54);
			if (!McLoot::Fill(Table, Seed, Chest))
			{
				AddError(FString::Printf(TEXT("%s can't be filled"), *Table));
				break;
			}
			FMcBlockEntity Again = FMcBlockEntity::MakeContainer(54);
			McLoot::Fill(Table, Seed, Again);
			if (!(Chest == Again))
			{
				AddError(FString::Printf(TEXT("%s with seed %llu doesn't roll the same twice"), *Table, Seed));
			}
			for (const FMcItemStack& Stack : Chest.Slots)
			{
				if (Stack.IsEmpty())
				{
					continue;
				}
				++NumStacks;
				bool bBad = !Items.Exists(Stack.Item) || Stack.Count > Stack.GetMaxStackSize();
				if (Stack.IsDamaged())
				{
					++NumDamaged;
				}
				bBad |= Stack.GetDamage() > Stack.GetMaxDamage();
				if (Stack.Item == TEXT("minecraft:enchanted_book"))
				{
					++NumBooks;
					const TSharedPtr<FJsonValue> Stored = Stack.Get(TEXT("minecraft:stored_enchantments"));
					bBad |= !Stored.IsValid() || Stored->Type != EJson::Object || Stored->AsObject()->Values.IsEmpty();
					if (Stored.IsValid() && Stored->Type == EJson::Object)
					{
						for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Stored->AsObject()->Values)
						{
							const FMcEnchantmentDef* Def = Items.FindEnchantment(FName(*McCache::Namespaced(Entry.Key)));
							bBad |= !Def || Entry.Value->AsNumber() < 1 || Entry.Value->AsNumber() > Def->MaxLevel;
						}
					}
				}
				const TMap<FName, int32> Enchantments = Stack.GetEnchantments();
				NumEnchanted += Enchantments.Num() > 0 ? 1 : 0;
				for (const TPair<FName, int32>& Entry : Enchantments)
				{
					const FMcEnchantmentDef* Def = Items.FindEnchantment(Entry.Key);
					bBad |= !Def || Entry.Value < 1 || Entry.Value > Def->MaxLevel || !Items.CanEnchant(Stack.Item, Entry.Key);
					for (const TPair<FName, int32>& Other : Enchantments)
					{
						bBad |= Entry.Key != Other.Key && !Items.AreCompatible(Entry.Key, Other.Key);
					}
				}
				NumPotions += Stack.Has(TEXT("minecraft:potion_contents")) ? 1 : 0;
				NumNamed += Stack.Has(TEXT("minecraft:custom_name")) || Stack.Has(TEXT("minecraft:item_name")) ? 1 : 0;
				if (bBad && ++NumBad < 6)
				{
					AddError(FString::Printf(TEXT("%s (seed %llu) gave a bad stack: %s x%d"), *Table, Seed, *Stack.Item.ToString(), Stack.Count));
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d stacks: %d enchanted, %d books, %d with a potion, %d worn, %d named"), NumStacks, NumEnchanted, NumBooks, NumPotions, NumDamaged, NumNamed));
	TestEqual(TEXT("no stack broke the rules"), NumBad, 0);
	TestTrue(TEXT("tools come enchanted"), NumEnchanted > 20);
	TestTrue(TEXT("books come enchanted"), NumBooks > 20);
	TestTrue(TEXT("potions come with their kind"), NumPotions > 5);
	TestTrue(TEXT("some tools are worn"), NumDamaged > 5);

	return true;
}

#endif
