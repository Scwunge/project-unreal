#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McItemStack.h"
#include "McLoot.h"
#include "McMob.h"
#include "McRandom.h"
#include "McVillager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRaidTest, "McClient.McServer.Raids",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcRaidTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto MakeFloor = [&](FMcBlockWorld& World)
	{
		for (int32 ChunkX = 0; ChunkX < 8; ++ChunkX)
		{
			for (int32 ChunkZ = 0; ChunkZ < 8; ++ChunkZ)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(ChunkX, ChunkZ)));
			}
		}
		for (int32 X = 0; X < 128; ++X)
		{
			for (int32 Z = 0; Z < 128; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
			}
		}
	};
	const auto CountOf = [](const TArray<TPair<FName, int32>>& Wave, const TCHAR* Kind)
	{
		for (const TPair<FName, int32>& Each : Wave)
		{
			if (Each.Key == FName(Kind))
			{
				return Each.Value;
			}
		}
		return 0;
	};
	{
		TestEqual(TEXT("easy has three waves"), FMcEntityHost::GetWaveCount(1, 1), 3);
		TestEqual(TEXT("normal five"), FMcEntityHost::GetWaveCount(2, 1), 5);
		TestEqual(TEXT("hard seven"), FMcEntityHost::GetWaveCount(3, 1), 7);
		TestEqual(TEXT("a second level of omen adds a wave"), FMcEntityHost::GetWaveCount(2, 2), 6);
		FRandomStream Random(7);
		TArray<TPair<FName, int32>> Wave;
		FMcEntityHost::RollWave(2, 1, Random, Wave);
		TestTrue(TEXT("the first normal wave is four or five pillagers"), CountOf(Wave, TEXT("minecraft:pillager")) >= 4 && CountOf(Wave, TEXT("minecraft:pillager")) <= 5);
		FMcEntityHost::RollWave(2, 4, Random, Wave);
		TestEqual(TEXT("the fourth has three witches"), CountOf(Wave, TEXT("minecraft:witch")), 3);
		FMcEntityHost::RollWave(2, 5, Random, Wave);
		TestEqual(TEXT("the fifth an evoker"), CountOf(Wave, TEXT("minecraft:evoker")), 1);
	}
	{
		// Only a captain drops the ominous bottle, with an amplifier of 0 to 4 on it.
		FMcRandom Rng(5);
		TArray<FMcItemStack> Plain;
		McLoot::RollEntity(TEXT("minecraft:entities/pillager"), false, Rng, Plain);
		TestTrue(TEXT("a pillager drops no bottle"), Plain.IsEmpty());
		TArray<FMcItemStack> Led;
		McLoot::RollEntity(TEXT("minecraft:entities/pillager"), true, Rng, Led);
		if (TestEqual(TEXT("a captain drops one thing"), Led.Num(), 1))
		{
			TestEqual(TEXT("an ominous bottle"), Led[0].Item, FName(TEXT("minecraft:ominous_bottle")));
			const TSharedPtr<FJsonValue> Amplifier = Led[0].Get(TEXT("minecraft:ominous_bottle_amplifier"));
			TestTrue(TEXT("with an amplifier"), Amplifier.IsValid() && Amplifier->AsNumber() >= 0.0 && Amplifier->AsNumber() <= 4.0);
		}
	}
	{
		// A patrol is a captain and some pillagers a way off.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		TestTrue(TEXT("a patrol comes"), Host.SpawnPatrol(FVector(64.0, 1.0, 64.0)));
		int32 Pillagers = 0;
		int32 Captains = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (Entity->GetType() == FName(TEXT("minecraft:pillager")))
			{
				++Pillagers;
				Captains += static_cast<FMcMob&>(*Entity).bCaptain ? 1 : 0;
			}
		}
		TestTrue(TEXT("with several pillagers"), Pillagers >= 3);
		TestEqual(TEXT("and one captain"), Captains, 1);
	}
	{
		// A raid on a village: the bar fills, waves come one after another as the last is killed, and the last one dead wins.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		FMcMob* Villager = Host.SpawnMob(FMcVillager::StaticType(), FVector(64.5, 1.0, 64.5));
		if (!Villager)
		{
			return false;
		}
		Villager->bFrozen = true;
		FVector Middle;
		TestTrue(TEXT("a villager makes a village"), Host.FindVillage(FVector(70.0, 1.0, 70.0), Middle));
		TestFalse(TEXT("which is not far off"), Host.FindVillage(FVector(0.0, 1.0, 0.0), Middle));
		int32 WonLevel = 0;
		Host.OnRaidWon = [&WonLevel](const FVector&, int32 Level) { WonLevel = Level; };
		const int32 Id = Host.StartRaid(FVector(64.5, 1.0, 64.5), 1, 0);
		TestTrue(TEXT("the raid starts"), Id != 0);
		TestEqual(TEXT("not twice on one village"), Host.StartRaid(FVector(66.5, 1.0, 66.5), 1, 0), 0);
		for (int32 Tick = 0; Tick < 150; ++Tick)
		{
			Host.Tick(TArray<FVector>(), 6000.0);
		}
		FString Title;
		float Fraction = 0.0f;
		TestTrue(TEXT("the bar shows"), Host.GetRaidBar(FVector(70.0, 1.0, 70.0), Title, Fraction));
		TestTrue(TEXT("half full while it waits"), Fraction > 0.3f && Fraction < 0.7f);
		TestFalse(TEXT("but not a long way off"), Host.GetRaidBar(FVector(-100.0, 1.0, -100.0), Title, Fraction));
		int32 Waves = 0;
		int32 Captains = 0;
		for (int32 Tick = 0; Tick < 4000 && WonLevel == 0; ++Tick)
		{
			Host.Tick(TArray<FVector>(), 6000.0);
			const FMcEntityHost::FRaid* Raid = Host.FindRaid(Id);
			if (!Raid || Raid->Status != FMcEntityHost::FRaid::EStatus::Active || Raid->Raiders.IsEmpty())
			{
				continue;
			}
			Waves = Raid->Wave;
			for (const int32 RaiderId : Raid->Raiders)
			{
				FMcMob* Raider = static_cast<FMcMob*>(Host.GetManager().Find(RaiderId));
				if (Raider)
				{
					Captains += Raider->bCaptain ? 1 : 0;
					Raider->Damage(1000.0f, nullptr);
				}
			}
		}
		TestEqual(TEXT("five waves on normal"), Waves, 5);
		TestEqual(TEXT("a captain in each"), Captains, 5);
		TestEqual(TEXT("the win is announced at omen level one"), WonLevel, 1);
		TestTrue(TEXT("the village lives"), !Villager->bRemoved && Villager->Health > 0.0f);
		Host.GetRaidBar(FVector(64.0, 1.0, 64.0), Title, Fraction);
		TestEqual(TEXT("the bar says victory"), Title, FString(TEXT("Raid - Victory")));
	}
	{
		// A raid with no villagers left is lost, and a bell makes the raiders glow.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		FMcMob* Villager = Host.SpawnMob(FMcVillager::StaticType(), FVector(64.5, 1.0, 64.5));
		FMcMob* Near = Host.SpawnMob(TEXT("minecraft:vindicator"), FVector(70.5, 1.0, 64.5));
		FMcMob* Far = Host.SpawnMob(TEXT("minecraft:vindicator"), FVector(10.5, 1.0, 10.5));
		FMcMob* Pig = Host.SpawnMob(TEXT("minecraft:pig"), FVector(66.5, 1.0, 64.5));
		if (!Villager || !Near || !Far || !Pig)
		{
			return false;
		}
		TestEqual(TEXT("the bell reaches one raider"), Host.RingBell(FVector(64.5, 1.0, 64.5)), 1);
		TestTrue(TEXT("the near raider glows"), Near->GlowTicks > 0);
		TestEqual(TEXT("the far one does not"), Far->GlowTicks, 0);
		TestEqual(TEXT("nor the pig"), Pig->GlowTicks, 0);
		const int32 Id = Host.StartRaid(FVector(64.5, 1.0, 64.5), 1, 0);
		Villager->bRemoved = true;
		Host.Tick(TArray<FVector>(), 6000.0);
		const FMcEntityHost::FRaid* Raid = Host.FindRaid(Id);
		TestTrue(TEXT("no villagers is a defeat"), Raid && Raid->Status == FMcEntityHost::FRaid::EStatus::Defeat);
	}
	return true;
}

#endif
