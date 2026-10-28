#include "McGameRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGameRulesTest, "McClient.McCore.GameRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGameRulesTest::RunTest(const FString& Parameters)
{
	// A level with no rules has the game's defaults.
	const FMcGameRules Defaults = FMcGameRules::FromLevel(TMap<FString, FString>(), 2);
	TestEqual(TEXT("difficulty"), Defaults.Difficulty, 2);
	TestFalse(TEXT("normal isn't peaceful"), Defaults.IsPeaceful());
	TestTrue(TEXT("mobs spawn"), Defaults.bDoMobSpawning);
	TestTrue(TEXT("mobs drop loot"), Defaults.bDoMobLoot);
	TestTrue(TEXT("blocks drop"), Defaults.bDoTileDrops);
	TestFalse(TEXT("the inventory isn't kept"), Defaults.bKeepInventory);
	TestTrue(TEXT("health comes back"), Defaults.bNaturalRegeneration);
	TestTrue(TEXT("falls, fire and drowning hurt"), Defaults.bFallDamage && Defaults.bFireDamage && Defaults.bDrowningDamage);
	TestTrue(TEXT("the weather changes"), Defaults.bDoWeatherCycle);
	TestEqual(TEXT("random ticks"), Defaults.RandomTickSpeed, 3);

	TMap<FString, FString> Rules;
	Rules.Add(TEXT("doMobSpawning"), TEXT("false"));
	Rules.Add(TEXT("keepInventory"), TEXT("TRUE"));
	Rules.Add(TEXT("randomTickSpeed"), TEXT("20"));
	Rules.Add(TEXT("fallDamage"), TEXT("false"));
	const FMcGameRules Set = FMcGameRules::FromLevel(Rules, 0);
	TestTrue(TEXT("peaceful"), Set.IsPeaceful());
	TestFalse(TEXT("doMobSpawning off"), Set.bDoMobSpawning);
	TestTrue(TEXT("keepInventory is read ignoring case"), Set.bKeepInventory);
	TestEqual(TEXT("randomTickSpeed"), Set.RandomTickSpeed, 20);
	TestFalse(TEXT("fallDamage off"), Set.bFallDamage);
	TestTrue(TEXT("the others keep their defaults"), Set.bDoMobLoot && Set.bFireDamage);

	// Text that isn't a switch or a number is the default; a negative tick speed is none; the difficulty is clamped.
	Rules.Add(TEXT("doTileDrops"), TEXT("maybe"));
	Rules.Add(TEXT("randomTickSpeed"), TEXT("-4"));
	const FMcGameRules Odd = FMcGameRules::FromLevel(Rules, 9);
	TestTrue(TEXT("a bad switch is the default"), Odd.bDoTileDrops);
	TestEqual(TEXT("a negative speed is 0"), Odd.RandomTickSpeed, 0);
	TestEqual(TEXT("difficulty is clamped"), Odd.Difficulty, 3);
	TestEqual(TEXT("an absent int is the default"), FMcGameRules::GetInt(TMap<FString, FString>(), TEXT("x"), 7), 7);
	TestTrue(TEXT("an absent bool is the default"), FMcGameRules::GetBool(TMap<FString, FString>(), TEXT("x"), true));
	return true;
}

#endif
