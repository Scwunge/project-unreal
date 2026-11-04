// Ridden horses against vehicle_log24.txt (walking: first step 0.98 x speed, terminal 2.159 x speed) and vehicle_log25.txt (jump charge: a hold
// of 1, 2, 3, 4, 5, 6, 8, 15 and 30 ticks peaked 0.623, 0.743, 0.876, 1.025, 1.174, 1.341, 1.696, 1.977 and 1.915 blocks with the jump strength 0.7),
// and the taming, saddle, armour and chest rules.

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McMount.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMountTest, "McClient.McEntity.Mount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcMountTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const FMcMobType* HorseType = FMcMobType::Find(FName(TEXT("minecraft:horse")));
	const FMcMobType* DonkeyType = FMcMobType::Find(FName(TEXT("minecraft:donkey")));
	if (!TestNotNull(TEXT("horse type"), HorseType) || !TestNotNull(TEXT("donkey type"), DonkeyType))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -2; Z <= 6; ++Z) { for (int32 X = -2; X <= 2; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
	for (int32 X = -30; X < 30; ++X) { for (int32 Z = -30; Z < 100; ++Z) { World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0)); } }
	FMcEntityManager Manager(World);
	const auto MakeHorse = [&](const FMcMobType& Type, double Z)
	{
		FMcMob* Horse = new FMcMob(Type);
		Horse->Body.Position = FVector(0.5, 1.0, Z);
		Horse->bFrozen = false;
		Manager.Add(TUniquePtr<FMcEntity>(Horse));
		return Horse;
	};

	// Walking: the player's physics with the speed attribute for the input.
	FMcMob* Horse = MakeHorse(*HorseType, 0.5);
	Horse->bTamed = true;
	FMcMountState& State = McMount::Ensure(*Horse);
	State.Speed = 0.225f;
	State.JumpStrength = 0.7f;
	State.bSaddled = true;
	State.RiderId = 7;
	for (int32 Tick = 0; Tick < 3; ++Tick) { Manager.Tick(); }
	Horse->Body.Velocity = FVector::ZeroVector;
	const double StartZ = Horse->Body.Position.Z;
	McMount::SetRiderInput(*Horse, 1.0f, 0.0f, 0.0f, false);
	Manager.Tick();
	TestTrue(*FString::Printf(TEXT("first step 0.98 x speed = 0.2205, got %f"), Horse->Body.Position.Z - StartZ), FMath::Abs((Horse->Body.Position.Z - StartZ) - 0.2205) < 0.005);
	double Last = Horse->Body.Position.Z;
	double Step = 0.0;
	for (int32 Tick = 0; Tick < 40; ++Tick)
	{
		McMount::SetRiderInput(*Horse, 1.0f, 0.0f, 0.0f, false);
		Manager.Tick();
		Step = Horse->Body.Position.Z - Last;
		Last = Horse->Body.Position.Z;
	}
	TestTrue(*FString::Printf(TEXT("terminal 2.159 x speed = 0.4858, got %f"), Step), FMath::Abs(Step - 0.4858) < 0.01);

	// Jump charge: hold the key H ticks, let go; the peak is the rise of a jump at the fraction's strength.
	const int32 Holds[] = { 1, 2, 3, 4, 5, 6, 8, 15, 30 };
	const double Peaks[] = { 0.623, 0.743, 0.876, 1.025, 1.174, 1.341, 1.696, 1.977, 1.915 };
	for (int32 Row = 0; Row < 9; ++Row)
	{
		FMcMob* Jumper = MakeHorse(*HorseType, 20.5 + Row * 6.0);
		Jumper->bTamed = true;
		FMcMountState& Jumping = McMount::Ensure(*Jumper);
		Jumping.Speed = 0.225f;
		Jumping.JumpStrength = 0.7f;
		Jumping.bSaddled = true;
		Jumping.RiderId = 8 + Row;
		for (int32 Tick = 0; Tick < 5; ++Tick) { Manager.Tick(); }
		const double Ground = Jumper->Body.Position.Y;
		double Peak = 0.0;
		for (int32 Tick = 0; Tick < Holds[Row] + 60; ++Tick)
		{
			McMount::SetRiderInput(*Jumper, 0.0f, 0.0f, 0.0f, Tick < Holds[Row]);
			Manager.Tick();
			Peak = FMath::Max(Peak, Jumper->Body.Position.Y - Ground);
		}
		TestTrue(*FString::Printf(TEXT("a hold of %d ticks peaks %.3f above the floor (reference %.3f)"), Holds[Row], Peak, Peaks[Row]), FMath::Abs(Peak - Peaks[Row]) < 0.02);
	}
	TestEqual(TEXT("hold 10 is a full charge"), McMount::JumpFraction(10), 1.0f);
	TestEqual(TEXT("hold 1 is 0.4"), McMount::JumpFraction(1), 0.4f);

	// Saddle, armour, chest, food and a seat.
	FMcMob* Wild = MakeHorse(*HorseType, 90.5);
	FMcItemStack Saddle(FName(TEXT("minecraft:saddle")), 1);
	TestEqual(TEXT("a wild horse takes no saddle"), McMount::Use(Manager, *Wild, Saddle, 1, false, false), McMount::EUse::Mounted);
	McMount::ClearRider(*Wild);
	TestEqual(TEXT("and the saddle stays in the hand"), Saddle.Count, 1);
	Wild->bTamed = true;
	TestEqual(TEXT("a tame one does"), McMount::Use(Manager, *Wild, Saddle, 1, false, false), McMount::EUse::Saddled);
	TestTrue(TEXT("the saddle is used up"), Saddle.IsEmpty());
	FMcItemStack Armor(FName(TEXT("minecraft:iron_horse_armor")), 1);
	TestEqual(TEXT("armour goes on a horse"), McMount::Use(Manager, *Wild, Armor, 1, false, false), McMount::EUse::Armored);
	FMcMob* Donkey = MakeHorse(*DonkeyType, 94.5);
	Donkey->bTamed = true;
	FMcItemStack Chest(FName(TEXT("minecraft:chest")), 1);
	TestEqual(TEXT("a chest goes on a donkey"), McMount::Use(Manager, *Donkey, Chest, 1, false, false), McMount::EUse::Chested);
	FMcItemStack Chest2(FName(TEXT("minecraft:chest")), 1);
	FMcMob* Horse3 = MakeHorse(*HorseType, 97.5);
	Horse3->bTamed = true;
	TestEqual(TEXT("but not on a horse (it is a ride)"), McMount::Use(Manager, *Horse3, Chest2, 1, false, false), McMount::EUse::Mounted);
	TestEqual(TEXT("a donkey is slower and jumps lower"), McMount::Ensure(*Donkey).JumpStrength, 0.5f);

	// A pig with a carrot on a stick (vehicle_log26): 0.1239 a tick; a boost of 746 ticks adds 1.15 x sin(pi t/746): 0.1632 after 64 ticks.
	if (const FMcMobType* PigType = FMcMobType::Find(FName(TEXT("minecraft:pig"))))
	{
		FMcMob* Pig = MakeHorse(*PigType, 40.5);
		FMcMountState& PigState = McMount::Ensure(*Pig);
		PigState.bSaddled = true;
		PigState.RiderId = 5;
		const FName Stick(TEXT("minecraft:carrot_on_a_stick"));
		for (int32 Tick = 0; Tick < 5; ++Tick) { Manager.Tick(); }
		double PigLast = Pig->Body.Position.Z;
		for (int32 Tick = 0; Tick < 30; ++Tick)
		{
			McMount::SetRiderInput(*Pig, 0.0f, 0.0f, 0.0f, false, Stick);
			Manager.Tick();
			Step = Pig->Body.Position.Z - PigLast;
			PigLast = Pig->Body.Position.Z;
		}
		TestTrue(*FString::Printf(TEXT("a pig on a stick walks 0.1239, got %f"), Step), FMath::Abs(Step - 0.1239) < 0.004);
		TestTrue(TEXT("the boost starts"), McMount::Boost(*Pig, 746));
		for (int32 Tick = 0; Tick < 64; ++Tick)
		{
			McMount::SetRiderInput(*Pig, 0.0f, 0.0f, 0.0f, false, Stick);
			Manager.Tick();
			Step = Pig->Body.Position.Z - PigLast;
			PigLast = Pig->Body.Position.Z;
		}
		TestTrue(*FString::Printf(TEXT("64 ticks into the boost the step is about 0.162, got %f"), Step), FMath::Abs(Step - 0.1625) < 0.01);
		McMount::SetRiderInput(*Pig, 0.0f, 0.0f, 0.0f, false, FName(TEXT("minecraft:stick")));
		Manager.Tick();
		const double Held = Pig->Body.Position.Z;
		Manager.Tick();
		TestTrue(TEXT("without the stick in hand it stops"), Pig->Body.Position.Z - Held < 0.05);
	}
	return true;
}
