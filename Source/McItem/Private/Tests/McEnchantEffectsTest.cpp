#include "McEnchantEffects.h"
#include "McItemRegistry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEnchantEffectsTest, "McClient.McItem.EnchantEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FMcItemStack Enchanted(const TCHAR* Item, const TCHAR* Enchantment, int32 Level)
	{
		FMcItemStack Stack{ FName(Item) };
		TMap<FName, int32> Map;
		Map.Add(FName(Enchantment), Level);
		Stack.SetEnchantments(Map);
		return Stack;
	}
}

bool FMcEnchantEffectsTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	using namespace McEnchantEffects;

	// Level-based numbers, from the enchantment files (sharpness: 1 + 0.5 per level above the first).
	const FMcItemStack Sharp = Enchanted(TEXT("minecraft:diamond_sword"), TEXT("minecraft:sharpness"), 5);
	FContext Zombie;
	Zombie.VictimType = TEXT("minecraft:zombie");
	TestEqual(TEXT("sharpness V adds 3"), SumAdd(Sharp, TEXT("minecraft:damage"), Zombie), 3.0f);
	const FMcItemStack Smite = Enchanted(TEXT("minecraft:diamond_sword"), TEXT("minecraft:smite"), 5);
	TestEqual(TEXT("smite V adds 12.5 against the undead"), SumAdd(Smite, TEXT("minecraft:damage"), Zombie), 12.5f);
	FContext Cow;
	Cow.VictimType = TEXT("minecraft:cow");
	TestEqual(TEXT("smite adds nothing against a cow"), SumAdd(Smite, TEXT("minecraft:damage"), Cow), 0.0f);
	FContext Spider;
	Spider.VictimType = TEXT("minecraft:spider");
	const FMcItemStack Bane = Enchanted(TEXT("minecraft:diamond_sword"), TEXT("minecraft:bane_of_arthropods"), 3);
	TestEqual(TEXT("bane III adds 7.5 against a spider"), SumAdd(Bane, TEXT("minecraft:damage"), Spider), 7.5f);
	const FMcItemStack Knock = Enchanted(TEXT("minecraft:diamond_sword"), TEXT("minecraft:knockback"), 2);
	TestEqual(TEXT("knockback II adds 2"), SumAdd(Knock, TEXT("minecraft:knockback"), Cow), 2.0f);

	// Protection: 1 a level for anything that doesn't get past invulnerability; Feather Falling 3 a level, falls only.
	FContext Generic;
	Generic.DamageTags = GetDamageTags(TEXT("minecraft:generic"));
	FContext Fall;
	Fall.DamageTags = GetDamageTags(TEXT("minecraft:fall"));
	FContext Void;
	Void.DamageTags = GetDamageTags(TEXT("minecraft:out_of_world"));
	TestTrue(TEXT("a fall is_fall"), Fall.DamageTags.Contains(TEXT("minecraft:is_fall")));
	const FMcItemStack Protection = Enchanted(TEXT("minecraft:diamond_chestplate"), TEXT("minecraft:protection"), 4);
	TestEqual(TEXT("protection IV is 4"), SumAdd(Protection, TEXT("minecraft:damage_protection"), Generic), 4.0f);
	TestEqual(TEXT("protection is nothing against the void"), SumAdd(Protection, TEXT("minecraft:damage_protection"), Void), 0.0f);
	const FMcItemStack Feather = Enchanted(TEXT("minecraft:diamond_boots"), TEXT("minecraft:feather_falling"), 4);
	TestEqual(TEXT("feather falling IV is 12 against a fall"), SumAdd(Feather, TEXT("minecraft:damage_protection"), Fall), 12.0f);
	TestEqual(TEXT("feather falling is nothing against a blow"), SumAdd(Feather, TEXT("minecraft:damage_protection"), Generic), 0.0f);

	// Mending doubles what an orb mends.
	TestEqual(TEXT("mending's factor"), GetFactor(Enchanted(TEXT("minecraft:iron_pickaxe"), TEXT("minecraft:mending"), 1), TEXT("minecraft:repair_with_xp"), Generic), 2.0f);

	// Unbreaking: a tool is saved from half the wear at level 1 and three quarters at III; armour a fifth and a quarter.
	FRandomStream Random(1234);
	const auto Worn = [&](const FMcItemStack& Stack)
	{
		int32 Total = 0;
		for (int32 Point = 0; Point < 20000; ++Point)
		{
			Total += GetItemDamage(Stack, 1, Random);
		}
		return Total / 20000.0;
	};
	TestTrue(TEXT("a plain tool takes it all"), FMath::IsNearlyEqual(Worn(FMcItemStack(TEXT("minecraft:iron_pickaxe"))), 1.0, 1e-9));
	TestTrue(TEXT("unbreaking I on a tool: half"), FMath::IsNearlyEqual(Worn(Enchanted(TEXT("minecraft:iron_pickaxe"), TEXT("minecraft:unbreaking"), 1)), 0.5, 0.02));
	TestTrue(TEXT("unbreaking III on a tool: a quarter"), FMath::IsNearlyEqual(Worn(Enchanted(TEXT("minecraft:iron_pickaxe"), TEXT("minecraft:unbreaking"), 3)), 0.25, 0.02));
	TestTrue(TEXT("unbreaking I on armour: four fifths"), FMath::IsNearlyEqual(Worn(Enchanted(TEXT("minecraft:iron_chestplate"), TEXT("minecraft:unbreaking"), 1)), 0.8, 0.02));
	TestTrue(TEXT("unbreaking III on armour: 0.6 + 0.4 / 4"), FMath::IsNearlyEqual(Worn(Enchanted(TEXT("minecraft:iron_chestplate"), TEXT("minecraft:unbreaking"), 3)), 0.6 + 0.4 / 4.0, 0.02));
	return true;
}

#endif
