// The overworld animals added after the pig, cow, sheep and chicken: wolf, rabbit, fox, horse, goat, llama, turtle,
// frog and bee. Each spawns with the health the wiki gives it and drops only what its loot table lists; the wolf and
// the bee fight back when hit, and the bee stays in the air.

#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McItemRegistry.h"
#include "McMob.h"
#include "McSound.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FAnimal
	{
		const TCHAR* Name;
		float Health;
		// What the loot table can give (empty when it gives nothing here).
		TArray<FName> Drops;
	};

	void Fill(FMcBlockWorld& World, FMcBlockRegistry& Registry)
	{
		World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
		for (int32 X = 0; X < 16; ++X)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcAnimalsTest, "McClient.McServer.Animals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcAnimalsTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const TArray<FAnimal> Animals = {
		{ TEXT("minecraft:wolf"), 8.0f, {} },
		{ TEXT("minecraft:rabbit"), 3.0f, { TEXT("minecraft:rabbit_hide"), TEXT("minecraft:rabbit"), TEXT("minecraft:rabbit_foot") } },
		{ TEXT("minecraft:fox"), 10.0f, {} },
		{ TEXT("minecraft:horse"), 22.5f, { TEXT("minecraft:leather") } },
		{ TEXT("minecraft:goat"), 10.0f, {} },
		{ TEXT("minecraft:llama"), 22.5f, { TEXT("minecraft:leather") } },
		{ TEXT("minecraft:turtle"), 30.0f, { TEXT("minecraft:seagrass"), TEXT("minecraft:bowl") } },
		{ TEXT("minecraft:frog"), 10.0f, {} },
		{ TEXT("minecraft:bee"), 10.0f, {} },
	};

	for (const FAnimal& Animal : Animals)
	{
		const FName Kind(Animal.Name);
		const FMcMobType* Type = FMcMobType::Find(Kind);
		if (!TestNotNull(*FString::Printf(TEXT("%s is a mob type"), Animal.Name), Type))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s has the wiki's health"), Animal.Name), FMath::IsNearlyEqual(Type->MaxHealth, Animal.Health));
		TestFalse(*FString::Printf(TEXT("%s is not counted as hostile"), Animal.Name), Type->IsHostile() && Kind != FName(TEXT("minecraft:wolf")) && Kind != FName(TEXT("minecraft:bee")));
		TestFalse(*FString::Printf(TEXT("%s is not named as an animal that has no sounds"), Animal.Name), McSound::GetMobSoundName(Kind, McSound::EMobSound::Hurt).IsNone());

		FMcBlockWorld World{FMcDimensionType()};
		Fill(World, Registry);
		FMcEntityHost Host(World, 1);
		FMcMob* Mob = Host.SpawnMob(Kind, FVector(8.5, 1.0, 8.5));
		if (!TestNotNull(*FString::Printf(TEXT("%s spawns"), Animal.Name), Mob))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s spawns at full health"), Animal.Name), FMath::IsNearlyEqual(Mob->Health, Animal.Health));

		// Killed, it drops nothing but what its table lists (tables with a chance of nothing may give nothing).
		Mob->Damage(1000.0f, nullptr);
		TestTrue(*FString::Printf(TEXT("%s dies"), Animal.Name), Mob->bRemoved);
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (Entity->GetType() == FMcItemEntity::StaticType())
			{
				const FName Item = static_cast<const FMcItemEntity&>(*Entity).Stack.Item;
				TestTrue(*FString::Printf(TEXT("%s dropped %s, which its table lists"), Animal.Name, *Item.ToString()), Animal.Drops.Contains(Item));
			}
		}
	}

	// A wolf leaves a player alone until hit, then goes for them and bites.
	{
		FMcBlockWorld World{FMcDimensionType()};
		Fill(World, Registry);
		FMcEntityHost Host(World, 2);
		int32 Bites = 0;
		Host.OnPlayerHit = [&Bites](const FVector&, float Damage) { Bites += Damage > 0.0f ? 1 : 0; };
		FMcMob* Wolf = Host.SpawnMob(TEXT("minecraft:wolf"), FVector(8.5, 1.0, 8.5));
		if (TestNotNull(TEXT("the wolf spawns"), Wolf))
		{
			const TArray<FVector> Player = { FVector(10.5, 1.0, 8.5) };
			for (int32 Tick = 0; Tick < 100; ++Tick)
			{
				Host.Tick(Player, 6000.0);
			}
			TestEqual(TEXT("an unhurt wolf doesn't bite"), Bites, 0);
			Wolf->Damage(1.0f, &Player[0]);
			for (int32 Tick = 0; Tick < 200; ++Tick)
			{
				Host.Tick(Player, 6000.0);
			}
			TestTrue(TEXT("a hurt wolf bites"), Bites > 0);
		}
	}

	// A bee stays in the air instead of falling to the ground.
	{
		FMcBlockWorld World{FMcDimensionType()};
		Fill(World, Registry);
		FMcEntityHost Host(World, 3);
		FMcMob* Bee = Host.SpawnMob(TEXT("minecraft:bee"), FVector(8.5, 8.0, 8.5));
		if (TestNotNull(TEXT("the bee spawns"), Bee))
		{
			for (int32 Tick = 0; Tick < 100; ++Tick)
			{
				Host.Tick({}, 6000.0);
			}
			TestTrue(*FString::Printf(TEXT("the bee is still flying at y %.1f"), Bee->Body.Position.Y), Bee->Body.Position.Y > 2.0);
		}
	}
	return true;
}

#endif
