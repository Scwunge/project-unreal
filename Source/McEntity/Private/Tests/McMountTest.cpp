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
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

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
	for (int32 Z = -2; Z <= 8; ++Z) { for (int32 X = -2; X <= 2; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
	for (int32 X = -30; X < 30; ++X) { for (int32 Z = -30; Z < 130; ++Z) { World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0)); } }
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
	// Rearing (vehicle_log34: a standing jump set the rider back 0.315, 0.504, 0.6174, 0.6854 then 0.7 along the facing, and forward again by 0.119, 0.1915,
	// 0.2108, 0.1366, 0.0421 after the landing; 0.0675 = 0.15 x 0.45 higher on the first tick).
	for (const double Yaw : { 0.0, 90.0 })
	{
		FMcMob* Rearer = MakeHorse(*HorseType, 80.5 + Yaw / 30.0);
		Rearer->bTamed = true;
		FMcMountState& Rearing = McMount::Ensure(*Rearer);
		Rearing.Speed = 0.225f;
		Rearing.JumpStrength = 0.7f;
		Rearing.bSaddled = true;
		Rearing.RiderId = 40;
		for (int32 Tick = 0; Tick < 5; ++Tick) { Manager.Tick(); }
		const double Facing = FMath::DegreesToRadians(Yaw);
		double MostBack = 0.0;
		double FirstBack = 0.0;
		double FirstRise = 0.0;
		for (int32 Tick = 0; Tick < 90; ++Tick)
		{
			McMount::SetRiderInput(*Rearer, 0.0f, 0.0f, static_cast<float>(Yaw), Tick < 10);
			Manager.Tick();
			const FVector Offset = McMount::GetSeat(*Rearer) - Rearer->Body.Position - FVector(0.0, 0.84375, 0.0);
			const double Back = Offset.X * FMath::Sin(Facing) - Offset.Z * FMath::Cos(Facing);
			if (FirstBack == 0.0 && Back > 0.0)
			{
				FirstBack = Back;
				FirstRise = Offset.Y;
			}
			MostBack = FMath::Max(MostBack, Back);
		}
		TestNearlyEqual(*FString::Printf(TEXT("facing %.0f the first rear tick sets the rider back 0.315"), Yaw), FirstBack, 0.315, 1e-3);
		TestNearlyEqual(TEXT("and 0.0675 up"), FirstRise, 0.0675, 1e-3);
		TestNearlyEqual(TEXT("at the most 0.7 back"), MostBack, 0.7, 1e-3);
		TestNearlyEqual(TEXT("and back in place after the landing"), (McMount::GetSeat(*Rearer) - Rearer->Body.Position).Size(), 0.84375, 1e-3);
	}
	TestEqual(TEXT("hold 10 is a full charge"), McMount::JumpFraction(10), 1.0f);
	TestEqual(TEXT("hold 1 is 0.4"), McMount::JumpFraction(1), 0.4f);

	// Feeding a wild horse (vehicle_log40/41): temper after each food, in the logged order.
	{
		FMcMob* Fed = MakeHorse(*HorseType, 96.5);
		const TCHAR* Foods[] = {TEXT("minecraft:hay_block"), TEXT("minecraft:golden_apple"), TEXT("minecraft:sugar"), TEXT("minecraft:hay_block"), TEXT("minecraft:golden_carrot"), TEXT("minecraft:wheat"), TEXT("minecraft:apple")};
		const int32 Temper[] = {0, 10, 13, 13, 18, 21, 24};
		for (int32 I = 0; I < 7; I++)
		{
			FMcItemStack Food{FName(Foods[I]), 16};
			McMount::Use(Manager, *Fed, Food, 1, false, false);
			TestEqual(*FString::Printf(TEXT("temper after %s"), Foods[I]), Fed->Mount->Temper, Temper[I]);
		}
	}

	// Saddle, armour, chest, food and a seat.
	FMcMob* Wild = MakeHorse(*HorseType, 90.5);
	FMcItemStack Saddle(FName(TEXT("minecraft:saddle")), 1);
	TestNearlyEqual(TEXT("the rider sits 0.84375 above a horse's feet (vehicle_log30)"), McMount::GetSeat(*Wild).Y - Wild->Body.Position.Y, 0.84375, 1e-6);
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
	TestEqual(TEXT("the window holds the saddle, armour and 15 slots"), McMount::GetInventory(*Donkey->Mount)->Num(), 17);
	FMcItemStack Sneak;
	TestEqual(TEXT("sneaking on a tame one opens its window"), McMount::Use(Manager, *Donkey, Sneak, 1, false, true), McMount::EUse::Inventory);
	FMcItemStack Chest2(FName(TEXT("minecraft:chest")), 1);
	FMcMob* Horse3 = MakeHorse(*HorseType, 97.5);
	Horse3->bTamed = true;
	TestEqual(TEXT("but not on a horse (it is a ride)"), McMount::Use(Manager, *Horse3, Chest2, 1, false, false), McMount::EUse::Mounted);
	TestEqual(TEXT("a donkey is slower and jumps lower"), McMount::Ensure(*Donkey).JumpStrength, 0.5f);

	// A camel walks at its attribute, 0.1943 a tick, and dashes: 0.8 for the weakest charge (hold 1), then 0.4368, 0.3975.
	if (const FMcMobType* CamelType = FMcMobType::Find(FName(TEXT("minecraft:camel"))))
	{
		FMcMob* Camel = MakeHorse(*CamelType, 60.5);
		FMcMountState& CamelState = McMount::Ensure(*Camel);
		CamelState.bSaddled = true;
		CamelState.RiderId = 6;
		for (int32 Tick = 0; Tick < 5; ++Tick) { Manager.Tick(); }
		double CamelLast = Camel->Body.Position.Z;
		for (int32 Tick = 0; Tick < 40; ++Tick)
		{
			McMount::SetRiderInput(*Camel, 1.0f, 0.0f, 0.0f, false);
			Manager.Tick();
			Step = Camel->Body.Position.Z - CamelLast;
			CamelLast = Camel->Body.Position.Z;
		}
		TestTrue(*FString::Printf(TEXT("a camel walks 0.1943, got %f"), Step), FMath::Abs(Step - 0.1943) < 0.005);
		for (int32 Tick = 0; Tick < 40; ++Tick) { McMount::SetRiderInput(*Camel, 0.0f, 0.0f, 0.0f, false); Manager.Tick(); }
		CamelLast = Camel->Body.Position.Z;
		McMount::SetRiderInput(*Camel, 0.0f, 0.0f, 0.0f, true);
		Manager.Tick();
		McMount::SetRiderInput(*Camel, 0.0f, 0.0f, 0.0f, false);
		Manager.Tick();
		TestTrue(*FString::Printf(TEXT("the weakest dash steps 0.8, got %f"), Camel->Body.Position.Z - CamelLast), FMath::Abs((Camel->Body.Position.Z - CamelLast) - 0.8) < 0.03);
		TestEqual(TEXT("and the next must wait 55 ticks"), CamelState.DashCooldown, 55);
	}

	// A camel lies down and gets up by itself (after 400 ticks, 1 in 190 to sit, 1 in 39 to stand), and a rider makes it stand.
	if (const FMcMobType* SitType = FMcMobType::Find(FName(TEXT("minecraft:camel"))))
	{
		FMcMob* Sitter = MakeHorse(*SitType, 120.5);
		FMcMountState& SitState = McMount::Ensure(*Sitter);
		for (int32 Tick = 0; Tick < 399; ++Tick) { Manager.Tick(); }
		TestFalse(TEXT("a camel does not sit before 400 ticks"), SitState.bSitting);
		bool bSat = false;
		for (int32 Tick = 0; Tick < 6000 && !bSat; ++Tick) { Manager.Tick(); bSat = SitState.bSitting; }
		TestTrue(TEXT("it sits down in the end"), bSat);
		SitState.RiderId = 6;
		Manager.Tick();
		TestFalse(TEXT("a rider makes it stand"), SitState.bSitting);
	}

	// A camel takes a second rider; when the first gets off the second steers.
	if (const FMcMobType* TwoType = FMcMobType::Find(FName(TEXT("minecraft:camel"))))
	{
		FMcMob* Two = MakeHorse(*TwoType, 75.5);
		FMcMountState& TwoState = McMount::Ensure(*Two);
		TwoState.bSaddled = true;
		FMcItemStack Nothing;
		TestEqual(TEXT("first rider"), McMount::Use(Manager, *Two, Nothing, 11, false, false), McMount::EUse::Mounted);
		TestEqual(TEXT("second rider"), McMount::Use(Manager, *Two, Nothing, 12, false, false), McMount::EUse::Mounted);
		TestEqual(TEXT("no third"), McMount::Use(Manager, *Two, Nothing, 13, false, false), McMount::EUse::None);
		McMount::ClearPlayer(*Two, 11);
		TestEqual(TEXT("the second steers now"), TwoState.RiderId, 12u);
		// Seats (vehicle_log39, camel at yaw 28.125: the player 0.500 ahead of its centre, a villager put on behind it 0.700 behind).
		Two->Yaw = 0.0f;
		TestNearlyEqual(TEXT("the first rider sits 0.5 ahead of the camel's centre"), McMount::GetSeat(*Two).Z - Two->Body.Position.Z, 0.5, 1e-6);
		TwoState.PassengerId = 99;
		TestNearlyEqual(TEXT("a passenger sits 0.7 behind it"), McMount::GetSeatFor(*Two, 99).Z - Two->Body.Position.Z, -0.7, 1e-6);
	}

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

	// Armour (vehicle_log35, a horse of 30 health hit by 10 and by 4 in each armour): health left after the 10 hit and the 4 hit.
	struct FArmorRow { const TCHAR* Item; float After10; float After4; };
	const FArmorRow Rows[] = {
		{TEXT("minecraft:leather_horse_armor"), 20.24f, 26.16f}, {TEXT("minecraft:copper_horse_armor"), 20.32f, 26.32f},
		{TEXT("minecraft:iron_horse_armor"), 20.40f, 26.48f}, {TEXT("minecraft:golden_horse_armor"), 20.80f, 26.80f},
		{TEXT("minecraft:diamond_horse_armor"), 22.80f, 27.50f}, {TEXT("minecraft:netherite_horse_armor"), 26.15f, 28.81f},
	};
	for (const FArmorRow& Row : Rows)
	{
		FMcMountState Armored;
		Armored.Armor = FName(Row.Item);
		TestTrue(*FString::Printf(TEXT("%s on a 10 hit leaves %.2f (got %.2f)"), Row.Item, Row.After10, 30.0f - McMount::ReduceDamage(Armored, 10.0f)), FMath::Abs(30.0f - McMount::ReduceDamage(Armored, 10.0f) - Row.After10) < 0.01f);
		TestTrue(*FString::Printf(TEXT("%s on a 4 hit leaves %.2f (got %.2f)"), Row.Item, Row.After4, 30.0f - McMount::ReduceDamage(Armored, 4.0f)), FMath::Abs(30.0f - McMount::ReduceDamage(Armored, 4.0f) - Row.After4) < 0.01f);
	}

	// Saved and loaded again: the saddle, the armour and the taming stay.
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Wild->Serialize(Writer);
		FMcMob Loaded(*HorseType);
		FMemoryReader Reader(Bytes);
		TestTrue(TEXT("it loads"), Loaded.Serialize(Reader));
		TestTrue(TEXT("tame, saddled, armoured"), Loaded.bTamed && Loaded.Mount.IsValid() && Loaded.Mount->bSaddled && Loaded.Mount->Armor == FName(TEXT("minecraft:iron_horse_armor")));
	}

	// Rolled at spawn (vehicle_log43, 120 bare summons each): whole max health 15 to 30 (horse mean 22.8, donkey 22.4, mule 23.2), kept by a save.
	{
		const FMcMobType* RollType = FMcMobType::Find(TEXT("minecraft:donkey"));
		double Sum = 0.0;
		bool bWhole = true;
		bool bRange = true;
		for (int32 I = 0; I < 400; I++)
		{
			FMcMob Rolled(*RollType);
			const float Max = McMount::MaxHealthOf(Rolled);
			bWhole = bWhole && Max == FMath::FloorToFloat(Max) && Rolled.Health == Max;
			bRange = bRange && Max >= 15.0f && Max <= 30.0f;
			Sum += Max;
		}
		TestTrue(TEXT("whole numbers, health at the max"), bWhole);
		TestTrue(TEXT("15 to 30"), bRange);
		TestTrue(TEXT("mean about 22.5"), FMath::Abs(Sum / 400.0 - 22.5) < 0.8);
		FMcMob Original(*HorseType);
		Original.Health = 10.0f;
		const float Max = McMount::MaxHealthOf(Original);
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Original.Serialize(Writer);
		FMcMob Back(*HorseType);
		FMemoryReader Reader(Bytes);
		TestTrue(TEXT("loads"), Back.Serialize(Reader));
		TestEqual(TEXT("the roll and the hurt health stay"), McMount::MaxHealthOf(Back), Max);
		TestEqual(TEXT("health"), Back.Health, 10.0f);
		// A pig has no roll: the type's.
		FMcMob Pig(*FMcMobType::Find(TEXT("minecraft:pig")));
		TestEqual(TEXT("a pig keeps the type's max health"), McMount::MaxHealthOf(Pig), 10.0f);
	}
	return true;
}
