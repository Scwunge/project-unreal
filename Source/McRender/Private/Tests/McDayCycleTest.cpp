#include "McDayCycle.h"
#include "McSkyComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool Near(double A, double B, double Tolerance = 1e-3)
	{
		return FMath::Abs(A - B) <= Tolerance;
	}

	// Distance between two angles, going the short way round.
	double AngleGap(double A, double B)
	{
		return FMath::Abs(FMath::UnwindDegrees(static_cast<float>(A - B)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDayCycleAnglesTest, "McClient.McRender.DayCycleAngles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcDayCycleAnglesTest::RunTest(const FString& Parameters)
{
	FMcDayCycle& Cycle = FMcDayCycle::Get();
	if (!TestTrue(TEXT("timeline loads"), Cycle.EnsureLoaded()))
	{
		return false;
	}

	// The sun is overhead at noon and underfoot at midnight, and the moon opposite it.
	TestTrue(TEXT("noon sun overhead"), AngleGap(Cycle.GetSunAngle(6000.0), 0.0) < 0.01);
	TestTrue(TEXT("midnight sun underfoot"), AngleGap(Cycle.GetSunAngle(18000.0), 180.0) < 0.01);
	TestTrue(TEXT("next noon too"), AngleGap(Cycle.GetSunAngle(30000.0), 0.0) < 0.01);
	TestTrue(TEXT("midnight moon overhead"), AngleGap(Cycle.GetMoonAngle(18000.0), 0.0) < 0.01);
	TestTrue(TEXT("noon moon underfoot"), AngleGap(Cycle.GetMoonAngle(6000.0), 180.0) < 0.01);

	// It sets in the west around tick 12000 and rises in the east around 0 (23000 to 1000 either side of it).
	const double Sunset = Cycle.GetSunAngle(12000.0);
	TestTrue(TEXT("sunset is on the western horizon"), Sunset > 70.0 && Sunset < 110.0);
	const double Sunrise = Cycle.GetSunAngle(0.0);
	TestTrue(TEXT("sunrise is on the eastern horizon"), Sunrise > 250.0 && Sunrise < 290.0);
	TestTrue(TEXT("sunset and sunrise mirror each other"), Near(Sunset + Sunrise, 360.0, 0.01));

	// Always moving the same way, and a day later it's back.
	double Previous = Cycle.GetSunAngle(6000.0);
	for (int32 Tick = 6100; Tick < 30000; Tick += 100)
	{
		const double Angle = Cycle.GetSunAngle(Tick);
		TestTrue(FString::Printf(TEXT("sun angle rises at tick %d"), Tick), Angle > Previous);
		Previous = Angle;
	}
	TestTrue(TEXT("a day later"), AngleGap(Cycle.GetSunAngle(1234.0), Cycle.GetSunAngle(1234.0 + 24000.0)) < 0.01);
	TestTrue(TEXT("stars turn with the sun"), AngleGap(Cycle.GetStarAngle(9000.0), Cycle.GetSunAngle(9000.0)) < 0.01);

	// It crawls near noon and races at the horizon: the angle covered in the 1000 ticks around the sunset is more
	// than in the 1000 ticks around noon.
	const double AroundNoon = Cycle.GetSunAngle(6500.0) - Cycle.GetSunAngle(5500.0 + 24000.0) + 360.0;
	const double AroundSunset = Cycle.GetSunAngle(12500.0) - Cycle.GetSunAngle(11500.0);
	TestTrue(TEXT("the sun is quicker at the horizon"), AroundSunset > AroundNoon);

	// Directions: up at noon, west at sunset, east at sunrise, down at midnight (x east, y up).
	TestTrue(TEXT("up at noon"), FMcDayCycle::AngleToDirection(0.0f).Equals(FVector(0.0, 1.0, 0.0), 1e-5));
	TestTrue(TEXT("west at sunset"), FMcDayCycle::AngleToDirection(90.0f).Equals(FVector(-1.0, 0.0, 0.0), 1e-5));
	TestTrue(TEXT("east at sunrise"), FMcDayCycle::AngleToDirection(270.0f).Equals(FVector(1.0, 0.0, 0.0), 1e-5));
	TestTrue(TEXT("down at midnight"), FMcDayCycle::AngleToDirection(180.0f).Equals(FVector(0.0, -1.0, 0.0), 1e-5));

	// The easing curve.
	TestTrue(TEXT("ease start"), Near(FMcDayCycle::CubicBezier(0.362, 0.241, 0.638, 0.759, 0.0), 0.0));
	TestTrue(TEXT("ease end"), Near(FMcDayCycle::CubicBezier(0.362, 0.241, 0.638, 0.759, 1.0), 1.0));
	TestTrue(TEXT("ease middle"), Near(FMcDayCycle::CubicBezier(0.362, 0.241, 0.638, 0.759, 0.5), 0.5));
	TestTrue(TEXT("a straight line is the identity"), Near(FMcDayCycle::CubicBezier(0.0, 0.0, 1.0, 1.0, 0.3), 0.3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDayCycleMoonTest, "McClient.McRender.DayCycleMoon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcDayCycleMoonTest::RunTest(const FString& Parameters)
{
	// The day count mod 8, from a full moon on the first night.
	TestEqual(TEXT("day 0"), FMcDayCycle::GetMoonPhase(0), 0);
	TestEqual(TEXT("end of day 0"), FMcDayCycle::GetMoonPhase(23999), 0);
	TestEqual(TEXT("day 1"), FMcDayCycle::GetMoonPhase(24000), 1);
	TestEqual(TEXT("day 4"), FMcDayCycle::GetMoonPhase(24000 * 4 + 6000), 4);
	TestEqual(TEXT("day 7"), FMcDayCycle::GetMoonPhase(24000 * 7 + 5), 7);
	TestEqual(TEXT("day 8 is a full moon again"), FMcDayCycle::GetMoonPhase(24000 * 8), 0);
	TestEqual(TEXT("day 100"), FMcDayCycle::GetMoonPhase(24000 * 100), 4);
	TestEqual(TEXT("before the start"), FMcDayCycle::GetMoonPhase(-1), 7);

	// The phases' names are the jar's texture names.
	const TCHAR* const Names[] = { TEXT("full_moon"), TEXT("waning_gibbous"), TEXT("third_quarter"), TEXT("waning_crescent"),
		TEXT("new_moon"), TEXT("waxing_crescent"), TEXT("first_quarter"), TEXT("waxing_gibbous") };
	for (int32 Phase = 0; Phase < 8; ++Phase)
	{
		TestEqual(FString::Printf(TEXT("name of phase %d"), Phase), FMcDayCycle::GetMoonPhaseName(Phase), FName(Names[Phase]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDayCycleLightTest, "McClient.McRender.DayCycleLight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcDayCycleLightTest::RunTest(const FString& Parameters)
{
	FMcDayCycle& Cycle = FMcDayCycle::Get();
	if (!TestTrue(TEXT("timeline loads"), Cycle.EnsureLoaded()))
	{
		return false;
	}

	// Sky light: none taken away by day, 11 of the 15 levels at midnight, and the change comes over dusk and dawn.
	TestEqual(TEXT("no darkening at noon"), Cycle.GetSkyDarken(6000.0), 0);
	TestEqual(TEXT("no darkening at tick 1000"), Cycle.GetSkyDarken(1000.0), 0);
	TestEqual(TEXT("11 levels at midnight"), Cycle.GetSkyDarken(18000.0), 11);
	TestEqual(TEXT("11 levels from 13670 on"), Cycle.GetSkyDarken(13670.0), 11);
	TestEqual(TEXT("11 levels until 22330"), Cycle.GetSkyDarken(22330.0), 11);
	const int32 Dusk = Cycle.GetSkyDarken(12500.0);
	TestTrue(TEXT("dusk is between"), Dusk > 0 && Dusk < 11);
	const int32 Dawn = Cycle.GetSkyDarken(23000.0);
	TestTrue(TEXT("dawn is between"), Dawn > 0 && Dawn < 11);
	TestTrue(TEXT("darker after sunset than before"), Cycle.GetSkyDarken(13000.0) > Cycle.GetSkyDarken(12200.0));
	TestTrue(TEXT("the light level is a multiplier"), Near(Cycle.GetSkyLightLevel(6000.0), 1.0) && Near(Cycle.GetSkyLightLevel(18000.0), 0.2667, 1e-3));
	TestTrue(TEXT("the brightness factor"), Near(Cycle.GetSkyLightFactor(6000.0), 1.0) && Near(Cycle.GetSkyLightFactor(18000.0), 0.24, 1e-3));

	// Colours: the plains' sky and fog by day, black and a dark navy at night.
	const FLinearColor Sky = Cycle.GetSkyColor(6000.0);
	TestTrue(TEXT("noon sky"), Near(Sky.R, 0x78 / 255.0, 1e-3) && Near(Sky.G, 0xa7 / 255.0, 1e-3) && Near(Sky.B, 1.0, 1e-3));
	const FLinearColor Fog = Cycle.GetFogColor(6000.0);
	TestTrue(TEXT("noon fog"), Near(Fog.R, 0xc0 / 255.0, 1e-3) && Near(Fog.G, 0xd8 / 255.0, 1e-3) && Near(Fog.B, 1.0, 1e-3));
	const FLinearColor NightSky = Cycle.GetSkyColor(18000.0);
	TestTrue(TEXT("midnight sky is black"), Near(NightSky.R, 0.0) && Near(NightSky.G, 0.0) && Near(NightSky.B, 0.0));
	const FLinearColor NightFog = Cycle.GetFogColor(18000.0);
	TestTrue(TEXT("midnight fog"), Near(NightFog.R, 0xc0 / 255.0 * 0x0f / 255.0, 1e-3) && Near(NightFog.B, 0x16 / 255.0, 1e-3));
	const FLinearColor DuskSky = Cycle.GetSkyColor(12500.0);
	TestTrue(TEXT("dusk sky is darker than noon, brighter than midnight"), DuskSky.B < Sky.B && DuskSky.B > NightSky.B);

	// The glow at the horizon: nothing at noon or midnight, strong at sunset and sunrise, orange.
	TestTrue(TEXT("no glow at noon"), Cycle.GetSunriseColor(6000.0).A < 0.001f);
	TestTrue(TEXT("no glow at midnight"), Cycle.GetSunriseColor(18000.0).A < 0.001f);
	const FLinearColor Sunset = Cycle.GetSunriseColor(12732.0);
	TestTrue(TEXT("a strong orange sunset"), Sunset.A > 0.95f && Sunset.R > Sunset.G && Sunset.G > Sunset.B);
	const FLinearColor Sunrise = Cycle.GetSunriseColor(23488.0);
	TestTrue(TEXT("a sunrise too"), Sunrise.A > 0.85f);

	// Stars come out at night.
	TestTrue(TEXT("no stars at noon"), Cycle.GetStarBrightness(6000.0) < 0.001f);
	TestTrue(TEXT("stars at midnight"), Near(Cycle.GetStarBrightness(18000.0), 0.5, 1e-3));
	TestTrue(TEXT("stars brighten through dusk"), Cycle.GetStarBrightness(12000.0) < Cycle.GetStarBrightness(12800.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSkyWeatherTest, "McClient.McRender.SkyWeather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcSkyWeatherTest::RunTest(const FString& Parameters)
{
	// The plains' day sky, #78a7ff.
	const FLinearColor Sky(0x78 / 255.0f, 0xa7 / 255.0f, 1.0f, 1.0f);
	TestTrue(TEXT("clear leaves it be"), McSky::ApplyWeather(Sky, 0.0f, 0.0f).Equals(Sky, 1e-5f));
	const FLinearColor Rainy = McSky::ApplyWeather(Sky, 1.0f, 0.0f);
	TestTrue(TEXT("rain darkens"), Rainy.B < Sky.B && Rainy.G < Sky.G);
	TestTrue(TEXT("rain greys: the blue is mostly gone"), Rainy.B - Rainy.R < (Sky.B - Sky.R) * 0.3f);
	const FLinearColor Stormy = McSky::ApplyWeather(Sky, 1.0f, 1.0f);
	TestTrue(TEXT("thunder darker still"), Stormy.G < Rainy.G * 0.6f);

	// Daylight 1 is level 15; rain makes it 12 or so, the night's 0.24 stays.
	TestEqual(TEXT("clear daylight"), McSky::ApplyWeatherToLight(1.0f, 0.0f, 0.0f), 1.0f);
	const float RainLight = McSky::ApplyWeatherToLight(1.0f, 1.0f, 0.0f);
	TestTrue(TEXT("rain dims the day"), RainLight < 0.8f && RainLight > 0.7f);
	TestTrue(TEXT("thunder dims it more"), McSky::ApplyWeatherToLight(1.0f, 1.0f, 1.0f) < RainLight);
	TestEqual(TEXT("night is unchanged"), McSky::ApplyWeatherToLight(0.24f, 1.0f, 1.0f), 0.24f);
	return true;
}

#endif
