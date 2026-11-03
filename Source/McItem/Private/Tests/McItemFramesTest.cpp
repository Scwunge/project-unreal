#include "McItemFrames.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcItemFramesTest, "McClient.McItem.Frames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcItemFramesTest::RunTest(const FString& Parameters)
{
	// Frames: half-frame thresholds, wrapping at the end.
	TestEqual(TEXT("0 is frame 0"), McItemFrames::FrameOf(0.0, 64), 0);
	TestEqual(TEXT("just under half a frame"), McItemFrames::FrameOf(0.49 / 64.0, 64), 0);
	TestEqual(TEXT("half a frame"), McItemFrames::FrameOf(0.5 / 64.0, 64), 1);
	TestEqual(TEXT("last half frame wraps"), McItemFrames::FrameOf(63.6 / 64.0, 64), 0);

	// The compass: dead ahead is 0 (or 1), a quarter turn either side is 0.25 off.
	const FVector2D Origin(0.0, 0.0);
	const double Ahead = McItemFrames::CompassValue(Origin, 0.0, FVector2D(0.0, 10.0));
	TestTrue(TEXT("south target facing south is dead ahead"), FMath::Min(Ahead, 1.0 - Ahead) < 1.0e-9);
	const double Left = McItemFrames::CompassValue(Origin, 0.0, FVector2D(10.0, 0.0));
	const double Right = McItemFrames::CompassValue(Origin, 0.0, FVector2D(-10.0, 0.0));
	TestEqual(TEXT("east is a quarter off"), Left, 0.75, 1.0e-9);
	TestEqual(TEXT("west is the other quarter"), Right, 0.25, 1.0e-9);
	const double Turned = McItemFrames::CompassValue(Origin, 90.0, FVector2D(-10.0, 0.0));
	TestTrue(TEXT("turning to face the target"), FMath::Min(Turned, 1.0 - Turned) < 1.0e-9);

	// Resolve: the needle starts on the target, and other items pass through.
	McItemFrames::FContext Context;
	Context.Position = Origin;
	Context.Yaw = 0.0;
	Context.Spawn = FVector2D(0.0, 10.0);
	Context.DayTime = 6000.0;
	Context.GameTicks = 100;
	McItemFrames::SetContext(Context);
	TestEqual(TEXT("compass ahead"), McItemFrames::Resolve(TEXT("minecraft:compass")), FName(TEXT("minecraft:compass_16")));
	TestEqual(TEXT("clock at noon"), McItemFrames::Resolve(TEXT("minecraft:clock")), FName(TEXT("minecraft:clock_16")));
	TestEqual(TEXT("other items"), McItemFrames::Resolve(TEXT("minecraft:stick")), FName(TEXT("minecraft:stick")));
	return true;
}

#endif
