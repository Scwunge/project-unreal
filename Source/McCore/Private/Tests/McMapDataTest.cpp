#include "Misc/AutomationTest.h"
#include "McMapData.h"
#include "McBlockRegistry.h"

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
	TestEqual(TEXT("up"), FMcMapData::LandBrightness(1.0, 0, 0), 2);
	TestEqual(TEXT("flat"), FMcMapData::LandBrightness(0.0, 0, 0), 1);
	TestEqual(TEXT("down"), FMcMapData::LandBrightness(-1.0, 0, 1), 0);
	// Scale 1 dither (mapexp5): step -1 low/normal, +1 normal/high, 0 normal, 2 saturated.
	TestEqual(TEXT("s1 -1 p0"), FMcMapData::LandBrightness(-1.0, 1, 0), 0);
	TestEqual(TEXT("s1 -1 p1"), FMcMapData::LandBrightness(-1.0, 1, 1), 1);
	TestEqual(TEXT("s1 +1 p0"), FMcMapData::LandBrightness(1.0, 1, 0), 1);
	TestEqual(TEXT("s1 +1 p1"), FMcMapData::LandBrightness(1.0, 1, 1), 2);
	TestEqual(TEXT("s1 0"), FMcMapData::LandBrightness(0.0, 2, 1), 1);
	TestEqual(TEXT("s2 +2"), FMcMapData::LandBrightness(2.0, 2, 0), 2);
	TestEqual(TEXT("s2 -2"), FMcMapData::LandBrightness(-2.0, 2, 1), 0);
	TestEqual(TEXT("shallow"), FMcMapData::WaterBrightness(2, 1), 2);
	TestEqual(TEXT("mid"), FMcMapData::WaterBrightness(5, 0), 1);
	TestEqual(TEXT("deep"), FMcMapData::WaterBrightness(12, 1), 0);
	return true;
}

// The held map fills by columns (measured, ServerTickMaps): a column update touches that column alone, and going over every
// column gives what going over every row gives.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMapColumnTest, "Mc.Map.Column", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcMapColumnTest::RunTest(const FString&)
{
	// Any state that has a map colour.
	FMcBlockRegistry::Get().EnsureLoaded();
	FMcStateId Coloured = 0;
	for (FMcStateId Id = 1; Id < 3000 && Coloured == 0; ++Id)
	{
		Coloured = McMapColors::ColorOf(Id) != 0 ? Id : 0;
	}
	if (!TestTrue(TEXT("a coloured state exists"), Coloured != 0))
	{
		return true;
	}
	const auto Sample = [Coloured](int32 X, int32 Z)
	{
		FMcMapData::FColumn Column;
		Column.Top = Coloured;
		Column.Height = 64 + ((X + 2 * Z) & 3);
		return Column;
	};
	FMcMapData ByColumn;
	const int32 Changed = ByColumn.UpdateColumn(5, Sample);
	TestTrue(TEXT("column 5 changed"), Changed > 0);
	int32 Elsewhere = 0;
	for (int32 Z = 0; Z < FMcMapData::Size; ++Z)
	{
		for (int32 X = 0; X < FMcMapData::Size; ++X)
		{
			Elsewhere += (X != 5 && ByColumn.Colors[Z * FMcMapData::Size + X] != 0) ? 1 : 0;
		}
	}
	TestEqual(TEXT("no other column touched"), Elsewhere, 0);
	for (int32 X = 0; X < FMcMapData::Size; ++X)
	{
		ByColumn.UpdateColumn(X, Sample);
	}
	FMcMapData ByRow;
	for (int32 Z = 0; Z < FMcMapData::Size; ++Z)
	{
		ByRow.UpdateRow(Z, Sample);
	}
	TestTrue(TEXT("columns and rows give one picture"), ByColumn.Colors == ByRow.Colors);
	return true;
}
