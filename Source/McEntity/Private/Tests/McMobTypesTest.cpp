#include "McMob.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// The mobs added after the first audit: health, speed, attack and follow range are the reference game's attributes (mobattrs1.txt, 2026-11-05), the box the wiki's.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMobTypesTest, "McClient.McEntity.MobTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcMobTypesTest::RunTest(const FString& Parameters)
{
	struct FRow { const TCHAR* Name; float Health; float Speed; float Attack; float Width; float Height; };
	const FRow Rows[] = {
		{ TEXT("minecraft:mooshroom"), 10.f, 0.2f, 0.f, 0.9f, 1.4f }, { TEXT("minecraft:ocelot"), 10.f, 0.3f, 0.f, 0.6f, 0.7f },
		{ TEXT("minecraft:armadillo"), 12.f, 0.14f, 0.f, 0.7f, 0.65f }, { TEXT("minecraft:polar_bear"), 30.f, 0.25f, 6.f, 1.4f, 1.4f },
		{ TEXT("minecraft:panda"), 20.f, 0.15f, 6.f, 1.3f, 1.25f }, { TEXT("minecraft:parrot"), 6.f, 0.4f, 0.f, 0.5f, 0.9f },
		{ TEXT("minecraft:allay"), 20.f, 0.1f, 0.f, 0.35f, 0.6f }, { TEXT("minecraft:happy_ghast"), 20.f, 0.05f, 0.f, 4.f, 4.f },
		{ TEXT("minecraft:squid"), 10.f, 0.7f, 0.f, 0.8f, 0.8f }, { TEXT("minecraft:glow_squid"), 10.f, 0.7f, 0.f, 0.8f, 0.8f },
		{ TEXT("minecraft:cod"), 3.f, 0.7f, 0.f, 0.5f, 0.3f }, { TEXT("minecraft:salmon"), 3.f, 0.7f, 0.f, 0.7f, 0.4f },
		{ TEXT("minecraft:tropical_fish"), 3.f, 0.7f, 0.f, 0.5f, 0.4f }, { TEXT("minecraft:pufferfish"), 3.f, 0.7f, 0.f, 0.7f, 0.7f },
		{ TEXT("minecraft:tadpole"), 6.f, 1.0f, 0.f, 0.4f, 0.3f }, { TEXT("minecraft:axolotl"), 14.f, 1.0f, 2.f, 0.75f, 0.42f },
		{ TEXT("minecraft:snow_golem"), 4.f, 0.2f, 0.f, 0.7f, 1.9f }, { TEXT("minecraft:zoglin"), 40.f, 0.3f, 6.f, 1.3965f, 1.4f },
		{ TEXT("minecraft:breeze"), 30.f, 0.63f, 3.f, 0.6f, 1.77f }, { TEXT("minecraft:camel_husk"), 32.f, 0.09f, 0.f, 1.7f, 2.375f },
		{ TEXT("minecraft:parched"), 16.f, 0.25f, 2.f, 0.6f, 1.99f }, { TEXT("minecraft:sulfur_cube"), 4.f, 0.3f, 0.f, 0.98f, 0.98f },
		{ TEXT("minecraft:zombie_nautilus"), 15.f, 1.1f, 3.f, 0.875f, 0.95f } };
	for (const FRow& Row : Rows)
	{
		const FMcMobType* Type = FMcMobType::Find(Row.Name);
		if (!TestNotNull(*FString::Printf(TEXT("%s exists"), Row.Name), Type))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s health"), Row.Name), Type->MaxHealth, Row.Health);
		TestEqual(*FString::Printf(TEXT("%s speed"), Row.Name), Type->Speed, Row.Speed, 1.0e-4f);
		TestEqual(*FString::Printf(TEXT("%s attack"), Row.Name), Type->AttackDamage, Row.Attack);
		TestEqual(*FString::Printf(TEXT("%s width"), Row.Name), Type->Width, Row.Width, 1.0e-4f);
		TestEqual(*FString::Printf(TEXT("%s height"), Row.Name), Type->Height, Row.Height, 1.0e-4f);
	}
	return true;
}

#endif
