#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McEntityManager.h"
#include "McItemStack.h"
#include "McLoot.h"
#include "McMob.h"
#include "McRandom.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// What the 23 new mob types drop when they die, against the vanilla loot tables in cache/client/data/minecraft/loot_table/entities (item names read from
// the table files on 2026-11-05; the amounts and chances are the tables' own and are run by McLoot). Each mob is killed on the host and the items that land on
// the ground must be among the table's items; a mob whose table has no item (ocelot, tadpole, axolotl, armadillo, allay, happy ghast, sulfur cube) drops none.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcNewMobDropsTest, "McClient.McServer.NewMobDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcNewMobDropsTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	struct FRow
	{
		const TCHAR* Kind;
		TArray<FName> Items;
		// Items every kill (almost) gives at least one of: the first pool has no condition.
		bool bAlways;
	};
	const TArray<FRow> Rows = {
		{ TEXT("minecraft:mooshroom"), { FName("minecraft:beef"), FName("minecraft:leather") }, false },
		{ TEXT("minecraft:ocelot"), {}, false },
		{ TEXT("minecraft:polar_bear"), { FName("minecraft:cod"), FName("minecraft:salmon") }, false },
		{ TEXT("minecraft:panda"), { FName("minecraft:bamboo") }, false },
		{ TEXT("minecraft:parrot"), { FName("minecraft:feather") }, false },
		{ TEXT("minecraft:squid"), { FName("minecraft:ink_sac") }, true },
		{ TEXT("minecraft:glow_squid"), { FName("minecraft:glow_ink_sac") }, true },
		{ TEXT("minecraft:cod"), { FName("minecraft:cod"), FName("minecraft:bone_meal") }, true },
		{ TEXT("minecraft:salmon"), { FName("minecraft:salmon"), FName("minecraft:bone_meal") }, true },
		{ TEXT("minecraft:pufferfish"), { FName("minecraft:pufferfish"), FName("minecraft:bone_meal") }, true },
		{ TEXT("minecraft:tadpole"), {}, false },
		{ TEXT("minecraft:axolotl"), {}, false },
		{ TEXT("minecraft:snow_golem"), { FName("minecraft:snowball") }, false },
		{ TEXT("minecraft:armadillo"), {}, false },
		{ TEXT("minecraft:zoglin"), { FName("minecraft:rotten_flesh") }, true },
		{ TEXT("minecraft:allay"), {}, false },
		{ TEXT("minecraft:breeze"), { FName("minecraft:breeze_rod") }, false },
		{ TEXT("minecraft:parched"), { FName("minecraft:arrow"), FName("minecraft:bone"), FName("minecraft:tipped_arrow") }, false },
		{ TEXT("minecraft:camel_husk"), { FName("minecraft:rotten_flesh") }, true },
		{ TEXT("minecraft:zombie_nautilus"), { FName("minecraft:rotten_flesh") }, false },
		{ TEXT("minecraft:happy_ghast"), {}, false },
		{ TEXT("minecraft:sulfur_cube"), {}, false },
	};
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 ChunkX = 0; ChunkX < 2; ++ChunkX)
	{
		for (int32 ChunkZ = 0; ChunkZ < 2; ++ChunkZ)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(ChunkX, ChunkZ)));
		}
	}
	for (int32 X = 0; X < 32; ++X)
	{
		for (int32 Z = 0; Z < 32; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
		}
	}
	FMcEntityHost Host(World, 5);
	for (const FRow& Row : Rows)
	{
		TMap<FName, int32> Seen;
		int32 KillsWithDrops = 0;
		constexpr int32 Kills = 60;
		for (int32 Index = 0; Index < Kills; ++Index)
		{
			FMcMob* Mob = Host.SpawnMob(Row.Kind, FVector(8.5 + (Index % 8), 1.0, 8.5 + (Index / 8)));
			if (!TestNotNull(*FString::Printf(TEXT("%s spawns"), Row.Kind), Mob))
			{
				break;
			}
			Mob->bFrozen = true;
			Mob->Damage(1000.0f, nullptr);
			Host.GetManager().Tick();
		}
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (!Entity->bRemoved && Entity->GetType() == FMcItemEntity::StaticType())
			{
				Seen.FindOrAdd(static_cast<const FMcItemEntity&>(*Entity).Stack.Item) += 1;
				KillsWithDrops += 1;
			}
		}
		for (const TPair<FName, int32>& Each : Seen)
		{
			TestTrue(*FString::Printf(TEXT("%s drops only table items (%s)"), Row.Kind, *Each.Key.ToString()), Row.Items.Contains(Each.Key));
		}
		if (Row.Items.IsEmpty())
		{
			TestEqual(*FString::Printf(TEXT("%s drops nothing"), Row.Kind), KillsWithDrops, 0);
		}
		else if (Row.bAlways)
		{
			TestTrue(*FString::Printf(TEXT("%s drops items (%d stacks in %d kills)"), Row.Kind, KillsWithDrops, Kills), KillsWithDrops >= Kills / 2);
		}
		else
		{
			// A drop with a chance or a condition: some of the table's items turn up in 60 kills.
			TestTrue(*FString::Printf(TEXT("%s drops something (%d stacks in %d kills)"), Row.Kind, KillsWithDrops, Kills), KillsWithDrops > 0);
		}
		UE_LOG(LogTemp, Display, TEXT("NewMobDrops %s: %d stacks in %d kills"), Row.Kind, KillsWithDrops, Kills);
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (Entity->GetType() == FMcItemEntity::StaticType())
			{
				Entity->bRemoved = true;
			}
		}
	}
	return true;
}

#endif
