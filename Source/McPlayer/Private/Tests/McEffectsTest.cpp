#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "McEffects.h"
#include "McBlockBreak.h"
#include "McContainer.h"
#include "McSurvival.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEffectsTest, "McClient.McPlayer.Effects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcEffectsTest::RunTest(const FString& Parameters)
{
	FName Id;
	TestTrue(TEXT("a name with its namespace"), McEffects::Parse(TEXT("minecraft:speed"), Id) && Id == TEXT("speed"));
	TestFalse(TEXT("no such effect"), McEffects::Parse(TEXT("flying"), Id));
	TestEqual(TEXT("level numerals"), McEffects::GetLevelText(2) + McEffects::GetLevelText(0), FString(TEXT("III")));

	// A stronger effect replaces; an equal one lasting longer does too; a weaker or shorter one does not.
	FMcPlayerStats Stats;
	TestTrue(TEXT("given"), McEffects::Add(Stats, TEXT("speed"), 1, 100));
	TestFalse(TEXT("a weaker one is refused"), McEffects::Add(Stats, TEXT("speed"), 0, 500));
	TestFalse(TEXT("a shorter one is refused"), McEffects::Add(Stats, TEXT("speed"), 1, 50));
	TestTrue(TEXT("a longer one lasts"), McEffects::Add(Stats, TEXT("speed"), 1, 200));
	TestEqual(TEXT("one entry"), Stats.Effects.Num(), 1);
	TestEqual(TEXT("the longer time"), McEffects::Find(Stats, TEXT("speed"))->Duration, 200);
	TestNearlyEqual(TEXT("speed II is +40%"), McEffects::GetSpeedFactor(Stats), 1.4f, 0.001f);
	McEffects::Add(Stats, TEXT("slowness"), 0, 100);
	TestNearlyEqual(TEXT("slowness takes 15% off"), McEffects::GetSpeedFactor(Stats), 1.4f * 0.85f, 0.001f);
	TestEqual(TEXT("clearing one"), McEffects::Remove(Stats, TEXT("slowness")), 1);
	TestEqual(TEXT("clearing none of it"), McEffects::Remove(Stats, TEXT("slowness")), 0);
	TestEqual(TEXT("clearing all"), McEffects::Remove(Stats, NAME_None), 1);
	TestEqual(TEXT("nothing left"), Stats.Effects.Num(), 0);

	// Effects run out.
	McEffects::Add(Stats, TEXT("haste"), 0, 3);
	for (int32 Tick = 0; Tick < 2; ++Tick)
	{
		McEffects::Tick(Stats);
	}
	TestNotNull(TEXT("still there"), McEffects::Find(Stats, TEXT("haste")));
	McEffects::Tick(Stats);
	TestNull(TEXT("gone when the time is up"), McEffects::Find(Stats, TEXT("haste")));

	// What the rest of the game reads.
	McEffects::Add(Stats, TEXT("haste"), 1, 100);
	McEffects::Add(Stats, TEXT("mining_fatigue"), 0, 100);
	TestNearlyEqual(TEXT("haste II and fatigue I"), McEffects::GetDigFactor(Stats), 1.4f * 0.3f, 0.001f);
	McEffects::Add(Stats, TEXT("jump_boost"), 1, 100);
	TestNearlyEqual(TEXT("jump boost"), McEffects::GetJumpBonus(Stats), 0.2f, 0.001f);
	McEffects::Add(Stats, TEXT("strength"), 0, 100);
	McEffects::Add(Stats, TEXT("weakness"), 0, 100);
	TestNearlyEqual(TEXT("strength +3, weakness -4"), McEffects::GetAttackBonus(Stats), -1.0f, 0.001f);

	// Resistance takes a fifth a level off a hit (but not off the void); fire resistance turns fire away.
	FMcPlayerStats Hit;
	McEffects::Add(Hit, TEXT("resistance"), 1, 100);
	McSurvival::Hurt(Hit, 10.0f, McSurvival::EDamage::Mob);
	TestNearlyEqual(TEXT("resistance II takes 40% off"), Hit.Health, 14.0f, 0.001f);
	McSurvival::Hurt(Hit, 4.0f, McSurvival::EDamage::Void);
	TestNearlyEqual(TEXT("not off the void"), Hit.Health, 10.0f, 0.001f);
	FMcPlayerStats Fireproof;
	McEffects::Add(Fireproof, TEXT("fire_resistance"), 0, 100);
	McSurvival::Hurt(Fireproof, 4.0f, McSurvival::EDamage::Lava);
	TestNearlyEqual(TEXT("lava does nothing"), Fireproof.Health, 20.0f, 0.001f);
	Fireproof.OnFireTicks = 100;
	McSurvival::Tick(Fireproof, McSurvival::FEnvironment(), McSurvival::EDifficulty::Normal);
	TestEqual(TEXT("and the burning stops"), Fireproof.OnFireTicks, 0);

	// Instant effects act at once and are never kept.
	FMcPlayerStats Instant;
	Instant.Health = 5.0f;
	McEffects::Add(Instant, TEXT("instant_health"), 0, 1);
	TestNearlyEqual(TEXT("instant health heals 4"), Instant.Health, 9.0f, 0.001f);
	McEffects::Add(Instant, TEXT("instant_damage"), 0, 1);
	TestNearlyEqual(TEXT("instant damage does 6, past armour"), Instant.Health, 3.0f, 0.001f);
	TestEqual(TEXT("none kept"), Instant.Effects.Num(), 0);

	// Regeneration: 1 health every 50 ticks at level I, every 25 at II.
	FMcPlayerStats Regen;
	Regen.Health = 5.0f;
	McEffects::Add(Regen, TEXT("regeneration"), 0, 1000);
	for (int32 Tick = 0; Tick < 100; ++Tick)
	{
		McEffects::Tick(Regen);
	}
	TestNearlyEqual(TEXT("two health in 100 ticks"), Regen.Health, 7.0f, 0.001f);

	// Poison hurts but never kills; wither does.
	FMcPlayerStats Poisoned;
	Poisoned.Health = 3.0f;
	McEffects::Add(Poisoned, TEXT("poison"), 0, 1000);
	for (int32 Tick = 0; Tick < 400; ++Tick)
	{
		McEffects::Tick(Poisoned);
	}
	TestNearlyEqual(TEXT("poison stops at half a heart"), Poisoned.Health, 1.0f, 0.001f);
	FMcPlayerStats Withered;
	Withered.Health = 3.0f;
	McEffects::Add(Withered, TEXT("wither"), 0, 1000);
	for (int32 Tick = 0; Tick < 400 && Withered.Health > 0.0f; ++Tick)
	{
		McEffects::Tick(Withered);
	}
	TestTrue(TEXT("wither kills"), Withered.Health <= 0.0f);

	// Health boost raises the most health, and absorption adds hearts that are spent first.
	FMcPlayerStats Boosted;
	McEffects::Add(Boosted, TEXT("health_boost"), 1, 4);
	McEffects::Tick(Boosted);
	TestNearlyEqual(TEXT("health boost II is 8 more"), Boosted.MaxHealth, 28.0f, 0.001f);
	for (int32 Tick = 0; Tick < 4; ++Tick)
	{
		McEffects::Tick(Boosted);
	}
	TestNearlyEqual(TEXT("back to 20 when it ends"), Boosted.MaxHealth, 20.0f, 0.001f);
	FMcPlayerStats Absorbed;
	McEffects::Add(Absorbed, TEXT("absorption"), 0, 100);
	TestNearlyEqual(TEXT("absorption I is 4"), Absorbed.Absorption, 4.0f, 0.001f);
	McEffects::Remove(Absorbed, TEXT("absorption"));
	TestNearlyEqual(TEXT("and goes with it"), Absorbed.Absorption, 0.0f, 0.001f);

	// Hunger costs exhaustion; saturation feeds.
	FMcPlayerStats Hungry;
	McEffects::Add(Hungry, TEXT("hunger"), 0, 100);
	McEffects::Tick(Hungry);
	TestTrue(TEXT("hunger works exhaustion"), Hungry.Exhaustion > 0.0f);
	FMcPlayerStats Fed;
	Fed.Food = 5;
	Fed.Saturation = 0.0f;
	McEffects::Add(Fed, TEXT("saturation"), 0, 3);
	McEffects::Tick(Fed);
	TestEqual(TEXT("saturation feeds"), Fed.Food, 6);

	// Haste speeds digging.
	TestNearlyEqual(TEXT("dig speed is scaled"), McBlockBreak::GetDigSpeed(FMcItemStack(), 0, true, false, 2.0f), 2.0f, 0.001f);

	// The effects are saved with the stats.
	FMcPlayerStats Kept;
	McEffects::Add(Kept, TEXT("speed"), 2, 600);
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Kept.Serialize(Writer);
	FMcPlayerStats Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("reads back"), Loaded.Serialize(Reader));
	TestEqual(TEXT("one effect"), Loaded.Effects.Num(), 1);
	TestEqual(TEXT("its level and time"), Loaded.Effects.Num() == 1 ? Loaded.Effects[0].Amplifier * 1000 + Loaded.Effects[0].Duration : -1, 2600);
	return true;
}

#endif
