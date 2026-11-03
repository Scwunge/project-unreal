#include "Misc/AutomationTest.h"
#include "McMapData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMapCenterTest, "Mc.Map.Center", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcMapCenterTest::RunTest(const FString&)
{
	// Scale 0: cells of 128 starting at -64; the map made at 0 is centred on 0, at 64 on 128.
	TestEqual(TEXT("0"), FMcMapData::CenterOf(0, 0), 0);
	TestEqual(TEXT("63"), FMcMapData::CenterOf(63, 0), 0);
	TestEqual(TEXT("64"), FMcMapData::CenterOf(64, 0), 128);
	TestEqual(TEXT("-65"), FMcMapData::CenterOf(-65, 0), -128);
	TestEqual(TEXT("scale 1"), FMcMapData::CenterOf(3040, 1), 3136);
	TestEqual(TEXT("scale 2"), FMcMapData::CenterOf(3040, 2), 3264);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMapMarkerTest, "Mc.Map.Marker", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcMapMarkerTest::RunTest(const FString&)
{
	FMcMapData Map;
	bool bOff = false;
	TestEqual(TEXT("centre"), (int32)Map.MarkerByte(0.0, 0, bOff), 0);
	TestEqual(TEXT("10 east"), (int32)Map.MarkerByte(10.0, 0, bOff), 20);
	TestFalse(TEXT("on"), bOff);
	Map.MarkerByte(80.0, 0, bOff);
	TestTrue(TEXT("off"), bOff);
	TestEqual(TEXT("yaw -180"), (int32)FMcMapData::MarkerRotation(-180.0), 8);
	TestEqual(TEXT("yaw -157.5"), (int32)FMcMapData::MarkerRotation(-157.5), 9);
	TestEqual(TEXT("yaw 0"), (int32)FMcMapData::MarkerRotation(0.0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMapShadeTest, "Mc.Map.Shade", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcMapShadeTest::RunTest(const FString&)
{
	TestEqual(TEXT("up"), FMcMapData::LandBrightness(1.0), 2);
	TestEqual(TEXT("flat"), FMcMapData::LandBrightness(0.0), 1);
	TestEqual(TEXT("down"), FMcMapData::LandBrightness(-1.0), 0);
	TestEqual(TEXT("shallow"), FMcMapData::WaterBrightness(2, 1), 2);
	TestEqual(TEXT("mid"), FMcMapData::WaterBrightness(5, 0), 1);
	TestEqual(TEXT("deep"), FMcMapData::WaterBrightness(12, 1), 0);
	return true;
}
