#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McVillager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Nine chunks with a stone floor at y 0, so a villager's feet stand at y 1.
	void MakeRoutineWorld(FMcBlockWorld& World, FMcStateId Stone)
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

	FMcVillager* AddRoutineVillager(FMcEntityManager& Manager, const FVector& Pos)
	{
		FMcVillager* Villager = new FMcVillager(*FMcMobType::Find(FMcVillager::StaticType()));
		Villager->Body.Position = Pos;
		Manager.Add(TUniquePtr<FMcEntity>(Villager));
		return Villager;
	}

	void Run(FMcEntityManager& Manager, int32 Ticks)
	{
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			Manager.Tick();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVillagerRoutineTest, "McClient.McEntity.VillagerRoutine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcVillagerRoutineTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	using McVillager::EActivity;

	{
		// The hours of the day.
		TestTrue(TEXT("dawn is free time"), McVillager::GetActivityAt(500, false) == EActivity::Idle);
		TestTrue(TEXT("work starts at 2000"), McVillager::GetActivityAt(2000, false) == EActivity::Work);
		TestTrue(TEXT("and runs to 9000"), McVillager::GetActivityAt(8999, false) == EActivity::Work);
		TestTrue(TEXT("then the bell"), McVillager::GetActivityAt(9000, false) == EActivity::Meet);
		TestTrue(TEXT("free time again at 11000"), McVillager::GetActivityAt(11500, false) == EActivity::Idle);
		TestTrue(TEXT("bed from 12000"), McVillager::GetActivityAt(12000, false) == EActivity::Rest);
		TestTrue(TEXT("through the night"), McVillager::GetActivityAt(23999, false) == EActivity::Rest);
		TestTrue(TEXT("an alarm beats the hour"), McVillager::GetActivityAt(13000, true) == EActivity::Alarm);
		TestTrue(TEXT("a bed is a bed in any colour"), McVillager::IsBed(TEXT("minecraft:red_bed")) && !McVillager::IsBed(TEXT("minecraft:bedrock")));
		TestTrue(TEXT("a bell is a bell"), McVillager::IsBell(TEXT("minecraft:bell")) && !McVillager::IsBell(TEXT("minecraft:stone")));
	}
	{
		// At work hours a villager goes to its job site and stays; when the work is over it is free to wander again.
		FMcBlockWorld World{ FMcDimensionType() };
		MakeRoutineWorld(World, Stone);
		World.SetBlock(FIntVector(10, 1, 0), State(TEXT("minecraft:composter")));
		FMcEntityManager Manager(World);
		Manager.TimeOfDay = 3000;
		FMcVillager* Farmer = AddRoutineVillager(Manager, FVector(0.5, 1.0, 0.5));
		Run(Manager, 500);
		TestEqual(TEXT("it took the composter's job"), Farmer->GetProfession(), FName(TEXT("minecraft:farmer")));
		TestTrue(TEXT("it is working"), Farmer->GetActivity() == EActivity::Work);
		TestTrue(TEXT("at the job site"), FVector::Dist2D(Farmer->Body.Position, FVector(10.5, 1.0, 0.5)) < 3.0);
		TestTrue(TEXT("and stays there"), Farmer->bStayPut);
		const FVector Place = Farmer->Body.Position;
		Run(Manager, 200);
		TestTrue(TEXT("it does not wander off"), FVector::Dist(Farmer->Body.Position, Place) < 0.5);
		Manager.TimeOfDay = 10000;
		Run(Manager, 60);
		TestTrue(TEXT("no bell in reach, so nothing keeps it"), Farmer->GetActivity() == EActivity::Meet && !Farmer->bStayPut);
		Manager.TimeOfDay = 11500;
		Run(Manager, 60);
		TestTrue(TEXT("free time"), Farmer->GetActivity() == EActivity::Idle && !Farmer->bStayPut);
	}
	{
		// At night each villager takes a bed of its own and sleeps in it; one bed has one sleeper.
		FMcBlockWorld World{ FMcDimensionType() };
		MakeRoutineWorld(World, Stone);
		World.SetBlock(FIntVector(8, 1, 0), State(TEXT("minecraft:red_bed")));
		FMcEntityManager Manager(World);
		Manager.TimeOfDay = 13000;
		FMcVillager* First = AddRoutineVillager(Manager, FVector(0.5, 1.0, 0.5));
		FMcVillager* Second = AddRoutineVillager(Manager, FVector(0.5, 1.0, 2.5));
		Run(Manager, 500);
		const bool bFirstHas = First->GetBed().IsSet();
		const bool bSecondHas = Second->GetBed().IsSet();
		TestTrue(TEXT("one of them has the bed"), bFirstHas != bSecondHas);
		FMcVillager* Sleeper = bFirstHas ? First : Second;
		TestTrue(TEXT("and sleeps in it"), Sleeper->IsSleeping() && Sleeper->bStayPut);
		TestFalse(TEXT("the other one doesn't"), (bFirstHas ? Second : First)->IsSleeping());
		Manager.TimeOfDay = 2500;
		Run(Manager, 60);
		TestFalse(TEXT("it gets up in the morning"), Sleeper->IsSleeping());
		TestTrue(TEXT("but keeps its bed"), Sleeper->GetBed().IsSet());
		World.SetBlock(FIntVector(8, 1, 0), Registry.ParseState(TEXT("minecraft:air")).Get(0));
		Manager.TimeOfDay = 13000;
		Run(Manager, 60);
		TestFalse(TEXT("a bed that is gone is let go"), Sleeper->GetBed().IsSet());
	}
	{
		// An alarm calls every villager to the bell, whatever the hour.
		FMcBlockWorld World{ FMcDimensionType() };
		MakeRoutineWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.TimeOfDay = 3000;
		FMcVillager* Villager = AddRoutineVillager(Manager, FVector(0.5, 1.0, 0.5));
		Manager.SoundAlarm(FVector(9.5, 1.0, 0.5), McVillager::AlarmTicks);
		TestTrue(TEXT("the alarm is sounding"), Manager.IsAlarmSounding());
		Run(Manager, 400);
		TestTrue(TEXT("the villager answers it"), Villager->GetActivity() == EActivity::Alarm);
		TestTrue(TEXT("at the bell"), FVector::Dist2D(Villager->Body.Position, FVector(9.5, 1.0, 0.5)) < 3.0);
		Run(Manager, McVillager::AlarmTicks);
		TestFalse(TEXT("the alarm runs out"), Manager.IsAlarmSounding());
	}
	return true;
}

#endif
