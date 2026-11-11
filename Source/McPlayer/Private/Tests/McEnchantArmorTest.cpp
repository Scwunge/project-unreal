#include "McContainer.h"
#include "McItemRegistry.h"
#include "McSurvival.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// What the vanilla client lost to a 10 point hit wearing diamond armour with enchantments (ench_log2.txt, a /damage of 10 each,
// health read every tick): the health left is 20 minus these.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEnchantArmorTest, "McClient.McPlayer.EnchantArmor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FMcItemStack Piece(const TCHAR* Kind, const TCHAR* Enchantment, int32 Level)
	{
		FMcItemStack Stack(FName(*FString::Printf(TEXT("minecraft:diamond_%s"), Kind)));
		if (Enchantment)
		{
			TMap<FName, int32> Map;
			Map.Add(FName(Enchantment), Level);
			Stack.SetEnchantments(Map);
		}
		return Stack;
	}

	float Lost(FMcContainer& Worn, McSurvival::EDamage Kind)
	{
		FMcPlayerStats Stats;
		McSurvival::Hurt(Stats, 10.0f, Kind, &Worn);
		return 20.0f - Stats.Health;
	}
}

bool FMcEnchantArmorTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto Check = [this](const TCHAR* Name, float Got, float Wanted) { TestTrue(*FString::Printf(TEXT("%s: %.3f, vanilla %.3f"), Name, Got, Wanted), FMath::IsNearlyEqual(Got, Wanted, 0.02f)); };
	{
		FMcContainer Worn(4);
		Check(TEXT("bare, a blow"), Lost(Worn, McSurvival::EDamage::Mob), 10.0f);
	}
	{
		FMcContainer Worn(4);
		Worn.Set(1, Piece(TEXT("chestplate"), nullptr, 0));
		Check(TEXT("a diamond chestplate"), Lost(Worn, McSurvival::EDamage::Mob), 8.4f);
	}
	{
		FMcContainer Worn(4);
		Worn.Set(1, Piece(TEXT("chestplate"), TEXT("minecraft:protection"), 4));
		Check(TEXT("protection IV on a chestplate"), Lost(Worn, McSurvival::EDamage::Mob), 7.06f);
	}
	{
		FMcContainer Worn(4);
		Worn.Set(1, Piece(TEXT("chestplate"), TEXT("minecraft:protection"), 1));
		Check(TEXT("protection I on a chestplate"), Lost(Worn, McSurvival::EDamage::Mob), 8.06f);
	}
	{
		FMcContainer Worn(4);
		Worn.Set(0, Piece(TEXT("helmet"), TEXT("minecraft:protection"), 4));
		Worn.Set(1, Piece(TEXT("chestplate"), TEXT("minecraft:protection"), 4));
		Worn.Set(2, Piece(TEXT("leggings"), TEXT("minecraft:protection"), 4));
		Worn.Set(3, Piece(TEXT("boots"), TEXT("minecraft:protection"), 4));
		Check(TEXT("a full protection IV set, a blow"), Lost(Worn, McSurvival::EDamage::Mob), 1.08f);
		FMcContainer Again(4);
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			Again.Set(Slot, Worn.Get(Slot));
		}
		Check(TEXT("a full protection IV set, generic damage (armour passed)"), Lost(Again, McSurvival::EDamage::Generic), 3.6f);
		FMcContainer Magic(4);
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			Magic.Set(Slot, Worn.Get(Slot));
		}
		Check(TEXT("a full protection IV set, magic"), Lost(Magic, McSurvival::EDamage::Magic), 3.6f);
		FMcContainer Void(4);
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			Void.Set(Slot, Worn.Get(Slot));
		}
		Check(TEXT("a full protection IV set, the void"), Lost(Void, McSurvival::EDamage::Void), 10.0f);
	}
	{
		FMcContainer Worn(4);
		Worn.Set(3, Piece(TEXT("boots"), nullptr, 0));
		Check(TEXT("plain boots, a fall"), Lost(Worn, McSurvival::EDamage::Fall), 10.0f);
		FMcContainer Feather(4);
		Feather.Set(3, Piece(TEXT("boots"), TEXT("minecraft:feather_falling"), 4));
		Check(TEXT("feather falling IV, a fall"), Lost(Feather, McSurvival::EDamage::Fall), 5.2f);
		FMcContainer Blow(4);
		Blow.Set(3, Piece(TEXT("boots"), TEXT("minecraft:feather_falling"), 4));
		Check(TEXT("feather falling IV, a blow: only the armour"), Lost(Blow, McSurvival::EDamage::Mob), 9.76f);
	}
	{
		FMcContainer Worn(4);
		Worn.Set(1, Piece(TEXT("chestplate"), TEXT("minecraft:blast_protection"), 4));
		Check(TEXT("blast protection IV, an explosion"), Lost(Worn, McSurvival::EDamage::Explosion), 5.71f);
	}
	return true;
}

#endif
