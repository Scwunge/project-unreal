// The game rules that change what happens to a player: fall, fire and drowning damage, and natural regeneration.

#include "McGameRules.h"
#include "McPlayerData.h"
#include "McSurvival.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGameRulesSurvivalTest, "McClient.McPlayer.GameRulesSurvival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGameRulesSurvivalTest::RunTest(const FString& Parameters)
{
	using McSurvival::EDamage;
	using McSurvival::EDifficulty;

	FMcGameRules Rules;
	Rules.bFallDamage = false;
	Rules.bFireDamage = false;
	Rules.bDrowningDamage = false;
	TestFalse(TEXT("falls are off"), McSurvival::IsDamageEnabled(EDamage::Fall, Rules));
	TestFalse(TEXT("drowning is off"), McSurvival::IsDamageEnabled(EDamage::Drown, Rules));
	TestFalse(TEXT("fire is off"), McSurvival::IsDamageEnabled(EDamage::InFire, Rules));
	TestFalse(TEXT("burning is off"), McSurvival::IsDamageEnabled(EDamage::OnFire, Rules));
	TestFalse(TEXT("lava is off"), McSurvival::IsDamageEnabled(EDamage::Lava, Rules));
	TestTrue(TEXT("a mob's blow still hurts"), McSurvival::IsDamageEnabled(EDamage::Mob, Rules));
	TestTrue(TEXT("and starving"), McSurvival::IsDamageEnabled(EDamage::Starve, Rules));

	// Hurt with the rules does nothing for the kinds that are off, and the usual for the others.
	FMcPlayerStats Fall;
	TestEqual(TEXT("a fall does nothing"), McSurvival::Hurt(Fall, 5.0f, EDamage::Fall, nullptr, &Rules), 0.0f);
	TestEqual(TEXT("health is whole"), Fall.Health, Fall.MaxHealth);
	FMcPlayerStats Bitten;
	TestTrue(TEXT("a bite hurts"), McSurvival::Hurt(Bitten, 3.0f, EDamage::Mob, nullptr, &Rules) > 0.0f);
	FMcPlayerStats Fallen;
	TestTrue(TEXT("a fall hurts with no rules"), McSurvival::Hurt(Fallen, 3.0f, EDamage::Fall) > 0.0f);

	// Breath runs out and nothing happens with drowning_damage off.
	McSurvival::FEnvironment Underwater;
	Underwater.bHeadInWater = true;
	FMcPlayerStats Diver;
	FMcPlayerStats Drowner;
	for (int32 Tick = 0; Tick < 600; ++Tick)
	{
		McSurvival::Tick(Diver, Underwater, EDifficulty::Normal, nullptr, &Rules);
		McSurvival::Tick(Drowner, Underwater, EDifficulty::Normal);
	}
	TestEqual(TEXT("no one drowns"), Diver.Health, Diver.MaxHealth);
	TestTrue(TEXT("with the rule on they do"), Drowner.Health < Drowner.MaxHealth);

	// Burning does nothing with fire_damage off.
	McSurvival::FEnvironment InFire;
	InFire.bInFire = true;
	FMcPlayerStats Torch;
	FMcPlayerStats Burnt;
	for (int32 Tick = 0; Tick < 200; ++Tick)
	{
		McSurvival::Tick(Torch, InFire, EDifficulty::Normal, nullptr, &Rules);
		McSurvival::Tick(Burnt, InFire, EDifficulty::Normal);
	}
	TestEqual(TEXT("fire doesn't hurt"), Torch.Health, Torch.MaxHealth);
	TestTrue(TEXT("with the rule on it does"), Burnt.Health < Burnt.MaxHealth);

	// Natural regeneration: well fed and hurt, health comes back unless the rule is off. Hunger still runs.
	FMcGameRules NoRegen;
	NoRegen.bNaturalRegeneration = false;
	const McSurvival::FEnvironment Calm;
	FMcPlayerStats Fed;
	FMcPlayerStats Still;
	FMcPlayerStats Peaceful;
	Fed.Health = Still.Health = Peaceful.Health = 10.0f;
	for (int32 Tick = 0; Tick < 400; ++Tick)
	{
		McSurvival::Tick(Fed, Calm, EDifficulty::Normal);
		McSurvival::Tick(Still, Calm, EDifficulty::Normal, nullptr, &NoRegen);
		McSurvival::Tick(Peaceful, Calm, EDifficulty::Peaceful, nullptr, &NoRegen);
	}
	TestTrue(TEXT("health comes back by default"), Fed.Health > 10.0f);
	TestEqual(TEXT("it doesn't with natural_health_regeneration off"), Still.Health, 10.0f);
	TestEqual(TEXT("nor on peaceful"), Peaceful.Health, 10.0f);
	return true;
}

#endif
