#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McVillager.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Nine chunks with a stone floor at y 0, so a villager's feet stand at y 1.
	void MakeVillagerWorld(FMcBlockWorld& World, FMcStateId Stone)
	{
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		for (int32 X = -16; X <= 31; ++X)
		{
			for (int32 Z = -16; Z <= 31; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Stone);
			}
		}
	}

	FMcVillager* AddVillager(FMcEntityManager& Manager, const FVector& Pos)
	{
		FMcVillager* Villager = new FMcVillager(*FMcMobType::Find(FMcVillager::StaticType()));
		Villager->Body.Position = Pos;
		Manager.Add(TUniquePtr<FMcEntity>(Villager));
		return Villager;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVillagerTest, "McClient.McEntity.Villager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcVillagerTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Air = State(TEXT("minecraft:air"));

	{
		// The biome gives the look; the job site block gives the profession.
		TestEqual(TEXT("desert villagers"), McVillager::GetVariantOfBiome(TEXT("minecraft:desert")), FName(TEXT("desert")));
		TestEqual(TEXT("a bamboo jungle is a jungle"), McVillager::GetVariantOfBiome(TEXT("minecraft:bamboo_jungle")), FName(TEXT("jungle")));
		TestEqual(TEXT("snowy taiga is snow, not taiga"), McVillager::GetVariantOfBiome(TEXT("minecraft:snowy_taiga")), FName(TEXT("snow")));
		TestEqual(TEXT("mangrove swamp"), McVillager::GetVariantOfBiome(TEXT("minecraft:mangrove_swamp")), FName(TEXT("swamp")));
		TestEqual(TEXT("old growth pine taiga"), McVillager::GetVariantOfBiome(TEXT("minecraft:old_growth_pine_taiga")), FName(TEXT("taiga")));
		TestEqual(TEXT("anywhere else is plains"), McVillager::GetVariantOfBiome(TEXT("minecraft:forest")), FName(TEXT("plains")));
		TestEqual(TEXT("a composter is a farmer's"), McVillager::GetProfessionOfJobSite(TEXT("minecraft:composter")), FName(TEXT("minecraft:farmer")));
		TestEqual(TEXT("so is a lectern a librarian's"), McVillager::GetProfessionOfJobSite(TEXT("minecraft:lectern")), FName(TEXT("minecraft:librarian")));
		TestEqual(TEXT("a lava cauldron is a leatherworker's"), McVillager::GetProfessionOfJobSite(TEXT("minecraft:lava_cauldron")), FName(TEXT("minecraft:leatherworker")));
		TestTrue(TEXT("stone is nobody's"), McVillager::GetProfessionOfJobSite(TEXT("minecraft:stone")).IsNone());
		TestEqual(TEXT("thirteen professions"), McVillager::GetProfessions().Num(), 13);
		for (const FName& Profession : McVillager::GetProfessions())
		{
			TestEqual(*FString::Printf(TEXT("%s works at a block that gives it back"), *Profession.ToString()),
				McVillager::GetProfessionOfJobSite(McVillager::GetJobSite(Profession)), Profession);
			TestTrue(*FString::Printf(TEXT("%s's job site is a block"), *Profession.ToString()), Registry.FindDefaultState(McVillager::GetJobSite(Profession)).IsSet());
		}
	}
	{
		// Levels by experience: 0, 10, 70, 150, 250.
		TestEqual(TEXT("0 xp is novice"), McVillager::GetLevelOfXp(0), 1);
		TestEqual(TEXT("9 xp is still novice"), McVillager::GetLevelOfXp(9), 1);
		TestEqual(TEXT("10 xp is apprentice"), McVillager::GetLevelOfXp(10), 2);
		TestEqual(TEXT("70 xp is journeyman"), McVillager::GetLevelOfXp(70), 3);
		TestEqual(TEXT("150 xp is expert"), McVillager::GetLevelOfXp(150), 4);
		TestEqual(TEXT("250 xp is master"), McVillager::GetLevelOfXp(250), 5);
		TestEqual(TEXT("and no more after master"), McVillager::GetLevelOfXp(5000), 5);
		TestEqual(TEXT("level names"), McVillager::GetLevelName(3), FString(TEXT("Journeyman")));
		TestEqual(TEXT("the xp a level starts at"), McVillager::GetXpForLevel(4), 150);
	}
	{
		// A jobless villager takes the profession of the nearest job site, which then is its own; a second villager
		// next to it takes the other; with none left, it stays jobless; and a lost job site takes the job back.
		FMcBlockWorld World{ FMcDimensionType() };
		MakeVillagerWorld(World, Stone);
		World.SetBlock(FIntVector(5, 1, 0), State(TEXT("minecraft:composter")));
		World.SetBlock(FIntVector(0, 1, 6), State(TEXT("minecraft:lectern")));
		FMcEntityManager Manager(World);
		FMcVillager* First = AddVillager(Manager, FVector(1.5, 1.0, 0.5));
		TestTrue(TEXT("a new villager is jobless"), First->GetProfession() == FMcVillager::NoProfession() && !First->HasProfession());
		TestEqual(TEXT("a new villager is a novice"), First->GetLevel(), 1);
		for (int32 Tick = 0; Tick < 25; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("it takes the nearest job site's profession"), First->GetProfession(), FName(TEXT("minecraft:farmer")));
		TestTrue(TEXT("and claims that block"), First->GetJobSite().IsSet() && First->GetJobSite().GetValue() == FIntVector(5, 1, 0));
		FMcVillager* Second = AddVillager(Manager, FVector(1.5, 1.0, 1.5));
		for (int32 Tick = 0; Tick < 25; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("the next villager can't have the claimed composter, so it takes the lectern"), Second->GetProfession(), FName(TEXT("minecraft:librarian")));
		FMcVillager* Third = AddVillager(Manager, FVector(1.5, 1.0, 2.5));
		for (int32 Tick = 0; Tick < 25; ++Tick)
		{
			Manager.Tick();
		}
		TestFalse(TEXT("a third finds nothing free"), Third->HasProfession());
		World.SetBlock(FIntVector(5, 1, 0), Air);
		for (int32 Tick = 0; Tick < 25; ++Tick)
		{
			Manager.Tick();
		}
		TestFalse(TEXT("a farmer whose composter is gone, before any trade, is jobless again"), First->HasProfession());
		TestTrue(TEXT("and has no job site"), !First->GetJobSite().IsSet());
		TestTrue(TEXT("the librarian, whose lectern stays, keeps its job"), Second->HasProfession());
		// A new job site draws the jobless (the first, and the third that never found one).
		World.SetBlock(FIntVector(6, 1, 1), State(TEXT("minecraft:loom")));
		for (int32 Tick = 0; Tick < 25; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a new loom finds a shepherd"), First->GetProfession() == FName(TEXT("minecraft:shepherd")) || Third->GetProfession() == FName(TEXT("minecraft:shepherd")));
	}
	{
		// A villager that has earned experience keeps its profession and level when its job site goes, and looks only for
		// a site of its own kind.
		FMcBlockWorld World{ FMcDimensionType() };
		MakeVillagerWorld(World, Stone);
		World.SetBlock(FIntVector(4, 1, 0), State(TEXT("minecraft:composter")));
		FMcEntityManager Manager(World);
		FMcVillager* Farmer = AddVillager(Manager, FVector(1.5, 1.0, 0.5));
		Farmer->SetProfession(FName(TEXT("minecraft:farmer")));
		Farmer->ClaimJobSite(FIntVector(4, 1, 0));
		Farmer->SetLevelAndXp(2, 12);
		World.SetBlock(FIntVector(4, 1, 0), Air);
		World.SetBlock(FIntVector(0, 1, 5), State(TEXT("minecraft:lectern")));
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("an experienced farmer stays a farmer"), Farmer->GetProfession(), FName(TEXT("minecraft:farmer")));
		TestEqual(TEXT("at its level"), Farmer->GetLevel(), 2);
		TestFalse(TEXT("and won't take a lectern"), Farmer->GetJobSite().IsSet());
		World.SetBlock(FIntVector(0, 1, 7), State(TEXT("minecraft:composter")));
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("but takes a new composter"), Farmer->GetJobSite().IsSet() && Farmer->GetJobSite().GetValue() == FIntVector(0, 1, 7));
	}
	{
		// A baby is half the size, takes no profession and grows up in 24000 ticks.
		FMcBlockWorld World{ FMcDimensionType() };
		MakeVillagerWorld(World, Stone);
		World.SetBlock(FIntVector(4, 1, 0), State(TEXT("minecraft:composter")));
		FMcEntityManager Manager(World);
		FMcVillager* Baby = AddVillager(Manager, FVector(1.5, 1.0, 0.5));
		Baby->SetBaby(true);
		TestTrue(TEXT("a baby is half as wide"), FMath::IsNearlyEqual(Baby->Body.Width, 0.3f));
		TestTrue(TEXT("and half as tall"), FMath::IsNearlyEqual(Baby->Body.Height, 0.975f));
		for (int32 Tick = 0; Tick < 100; ++Tick)
		{
			Manager.Tick();
		}
		TestFalse(TEXT("a baby takes no job"), Baby->HasProfession());
		Baby->SetGrowthTicksLeft(5);
		for (int32 Tick = 0; Tick < 10; ++Tick)
		{
			Manager.Tick();
		}
		TestFalse(TEXT("it grows up"), Baby->IsBaby());
		TestTrue(TEXT("to the full size"), FMath::IsNearlyEqual(Baby->Body.Height, 1.95f));
		for (int32 Tick = 0; Tick < 40; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("and then takes the composter's job"), Baby->HasProfession());
	}
	{
		// A save keeps the look, the job, the level and the age.
		FMcBlockWorld World{ FMcDimensionType() };
		FMcVillager Villager(*FMcMobType::Find(FMcVillager::StaticType()));
		Villager.SetVariant(TEXT("snow"));
		Villager.SetProfession(FName(TEXT("minecraft:cleric")));
		Villager.ClaimJobSite(FIntVector(3, 64, -9));
		Villager.SetLevelAndXp(3, 80);
		Villager.Health = 13.0f;
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Villager.Serialize(Writer);
		FMcVillager Loaded(*FMcMobType::Find(FMcVillager::StaticType()));
		FMemoryReader Reader(Bytes);
		TestTrue(TEXT("the villager reads back"), Loaded.Serialize(Reader));
		TestEqual(TEXT("its biome type"), Loaded.GetVariant(), FName(TEXT("snow")));
		TestEqual(TEXT("its profession"), Loaded.GetProfession(), FName(TEXT("minecraft:cleric")));
		TestTrue(TEXT("its job site"), Loaded.GetJobSite().IsSet() && Loaded.GetJobSite().GetValue() == FIntVector(3, 64, -9));
		TestEqual(TEXT("its level"), Loaded.GetLevel(), 3);
		TestEqual(TEXT("its experience"), Loaded.GetXp(), 80);
		TestEqual(TEXT("its health"), Loaded.Health, 13.0f);
		Villager.SetBaby(true);
		Villager.SetGrowthTicksLeft(777);
		Bytes.Reset();
		FMemoryWriter BabyWriter(Bytes);
		Villager.Serialize(BabyWriter);
		FMcVillager LoadedBaby(*FMcMobType::Find(FMcVillager::StaticType()));
		FMemoryReader BabyReader(Bytes);
		TestTrue(TEXT("a baby reads back"), LoadedBaby.Serialize(BabyReader));
		TestTrue(TEXT("still a baby"), LoadedBaby.IsBaby());
		TestEqual(TEXT("with its time left"), LoadedBaby.GetGrowthTicksLeft(), 777);
		TestTrue(TEXT("at its size"), FMath::IsNearlyEqual(LoadedBaby.Body.Width, 0.3f));
		TArray<uint8> Garbage = { 1, 2, 3 };
		FMemoryReader GarbageReader(Garbage);
		FMcVillager Bad(*FMcMobType::Find(FMcVillager::StaticType()));
		TestFalse(TEXT("damaged bytes are refused"), Bad.Serialize(GarbageReader));
	}
	return true;
}

#endif
