#include "McBed.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBedTest, "McClient.McPlayer.Bed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBedTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };

	// Night runs from 12542 to 23459 of each day; a thunderstorm makes any time dark enough.
	TestFalse(TEXT("noon"), McBed::CanSleepAt(6000, false));
	TestFalse(TEXT("dusk before 12542"), McBed::CanSleepAt(12541, false));
	TestTrue(TEXT("night starts"), McBed::CanSleepAt(12542, false));
	TestTrue(TEXT("midnight on the third day"), McBed::CanSleepAt(2 * 24000 + 18000, false));
	TestTrue(TEXT("night ends"), McBed::CanSleepAt(23459, false));
	TestFalse(TEXT("morning"), McBed::CanSleepAt(23460, false));
	TestTrue(TEXT("storm at noon"), McBed::CanSleepAt(6000, true));
	TestEqual(TEXT("morning after midnight"), McBed::GetMorning(18000), static_cast<int64>(24000));
	TestEqual(TEXT("morning after the second night"), McBed::GetMorning(24000 + 13000), static_cast<int64>(48000));

	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	for (int32 X = 0; X < 12; ++X)
	{
		for (int32 Z = 0; Z < 12; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Stone);
		}
	}
	// A bed with its foot at (5, 1, 5), facing east: its head is at (6, 1, 5).
	World.SetBlock(FIntVector(5, 1, 5), State(TEXT("minecraft:red_bed[facing=east,part=foot]")));
	World.SetBlock(FIntVector(6, 1, 5), State(TEXT("minecraft:red_bed[facing=east,part=head]")));

	TestTrue(TEXT("from the foot"), McBed::GetHead(World, FIntVector(5, 1, 5)) == FIntVector(6, 1, 5));
	TestTrue(TEXT("from the head"), McBed::GetHead(World, FIntVector(6, 1, 5)) == FIntVector(6, 1, 5));
	TestFalse(TEXT("stone is no bed"), McBed::GetHead(World, FIntVector(5, 0, 5)).IsSet());
	TestTrue(TEXT("the pillow is to the east, so the player looks east"), McBed::GetSleepYaw(World, FIntVector(6, 1, 5)).Get(-1.0f) == 270.0f);

	const FVector Lying = McBed::GetSleepPosition(FIntVector(6, 1, 5));
	TestTrue(TEXT("lies on the mattress"), FMath::IsNearlyEqual(Lying.Y, 1.5625) && FMath::IsNearlyEqual(Lying.X, 6.5));

	// Standing up (or respawning): on the floor beside the bed, never in it.
	const TOptional<FVector> Stand = McBed::FindStandPosition(World, FIntVector(5, 1, 5));
	if (TestTrue(TEXT("there is room beside the bed"), Stand.IsSet()))
	{
		const FIntVector Cell(FMath::FloorToInt32(Stand->X), FMath::FloorToInt32(Stand->Y), FMath::FloorToInt32(Stand->Z));
		TestEqual(TEXT("on the floor"), Cell.Y, 1);
		TestTrue(TEXT("not in the bed"), Cell != FIntVector(5, 1, 5) && Cell != FIntVector(6, 1, 5));
		TestTrue(TEXT("next to it"), FMath::Abs(Cell.X - 6) <= 1 && FMath::Abs(Cell.Z - 5) <= 1);
	}

	// Walled in on every side, with a ceiling: nowhere to stand.
	for (int32 X = 3; X <= 8; ++X)
	{
		for (int32 Z = 3; Z <= 7; ++Z)
		{
			for (int32 Y = 1; Y <= 3; ++Y)
			{
				const bool bBed = Y == 1 && Z == 5 && (X == 5 || X == 6);
				if (!bBed)
				{
					World.SetBlock(FIntVector(X, Y, Z), Stone);
				}
			}
		}
	}
	TestFalse(TEXT("a bed boxed in has no spot"), McBed::FindStandPosition(World, FIntVector(5, 1, 5)).IsSet());
	return true;
}

#endif
