#include "McTotem.h"
#include "McEffects.h"
#include "McItemStack.h"
#include "McPlayerData.h"
#include "McSurvival.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTotemTest, "McClient.McPlayer.Totem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcTotemTest::RunTest(const FString& Parameters)
{
	const FName Totem(TEXT("minecraft:totem_of_undying"));
	using McSurvival::EDamage;

	// A killing blow with a totem in the off hand: it is used up, and the player lives on with its effects.
	{
		FMcPlayerStats Stats;
		McEffects::Add(Stats, TEXT("poison"), 0, 200);
		McSurvival::Hurt(Stats, 30.0f, EDamage::Mob);
		TestTrue(TEXT("the blow would kill"), Stats.Health <= 0.0f);
		TestTrue(TEXT("a bite can be cheated"), McTotem::CanSave(Stats.LastHurtKind));
		FMcItemStack Main(FName(TEXT("minecraft:stone")), 5);
		FMcItemStack Off(Totem, 1);
		TestTrue(TEXT("the totem saves"), McTotem::TryUse(Stats, Main, Off));
		TestTrue(TEXT("used up"), Off.IsEmpty());
		TestEqual(TEXT("the main hand is untouched"), Main.Count, 5);
		TestEqual(TEXT("one health left"), Stats.Health, 1.0f);
		TestNull(TEXT("the poison is gone"), McEffects::Find(Stats, TEXT("poison")));
		const FMcStatusEffect* Regeneration = McEffects::Find(Stats, TEXT("regeneration"));
		TestTrue(TEXT("Regeneration II for 45 s"), Regeneration && Regeneration->Amplifier == 1 && Regeneration->Duration == 900);
		const FMcStatusEffect* Fire = McEffects::Find(Stats, TEXT("fire_resistance"));
		TestTrue(TEXT("Fire Resistance for 40 s"), Fire && Fire->Amplifier == 0 && Fire->Duration == 800);
		const FMcStatusEffect* Absorption = McEffects::Find(Stats, TEXT("absorption"));
		TestTrue(TEXT("Absorption II for 5 s"), Absorption && Absorption->Amplifier == 1 && Absorption->Duration == 100);
	}

	// The main hand's goes first; a stack of none, or no totem at all, saves nobody.
	{
		FMcPlayerStats Stats;
		Stats.Health = 0.0f;
		FMcItemStack Main(Totem, 1);
		FMcItemStack Off(Totem, 1);
		TestTrue(TEXT("saved from the main hand"), McTotem::TryUse(Stats, Main, Off) && Main.IsEmpty() && Off.Count == 1);

		FMcPlayerStats Doomed;
		Doomed.Health = 0.0f;
		FMcItemStack Sword(FName(TEXT("minecraft:iron_sword")), 1);
		FMcItemStack Empty;
		TestFalse(TEXT("no totem, no saving"), McTotem::TryUse(Doomed, Sword, Empty));
		TestEqual(TEXT("still dead"), Doomed.Health, 0.0f);
	}

	// The void gets past it.
	{
		FMcPlayerStats Stats;
		McSurvival::Hurt(Stats, 40.0f, EDamage::Void);
		TestFalse(TEXT("the void can't be cheated"), McTotem::CanSave(Stats.LastHurtKind));
		TestTrue(TEXT("a fall can"), McTotem::CanSave(static_cast<uint8>(EDamage::Fall)));
	}

	// The flight across the screen, as measured from vanilla: small in the middle, edge on 2 ticks later, then just over
	// half the screen high from 4 ticks until it goes.
	{
		TestFalse(TEXT("not before"), McTotem::GetFlight(-1.0f).bVisible);
		TestFalse(TEXT("gone after"), McTotem::GetFlight(static_cast<float>(McTotem::AnimationTicks)).bVisible);
		const McTotem::FFlight Start = McTotem::GetFlight(0.0f);
		const McTotem::FFlight Turning = McTotem::GetFlight(2.0f);
		const McTotem::FFlight Full = McTotem::GetFlight(4.0f);
		const McTotem::FFlight Late = McTotem::GetFlight(McTotem::AnimationTicks - 1.0f);
		TestTrue(TEXT("starts small in the middle"), Start.bVisible && Start.Size < 0.15f && FMath::Abs(Start.Centre.Y - 0.56) < 0.05);
		TestTrue(TEXT("edge on as it grows"), FMath::Abs(FMath::Cos(Turning.Turn)) < 0.5f && Turning.Size > 0.25f && Turning.Size < 0.5f);
		TestTrue(TEXT("just over half the screen"), Full.Size > 0.5f && Full.Size < 0.6f);
		TestTrue(TEXT("hangs there to the end"), Late.bVisible && FMath::IsNearlyEqual(Late.Size, Full.Size) && Late.Centre.Equals(Full.Centre));
	}
	return true;
}

#endif
