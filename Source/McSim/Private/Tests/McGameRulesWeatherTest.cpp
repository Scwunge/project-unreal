// The doWeatherCycle rule: with it off the weather stays as it is, and a command can still change it.

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWeatherCycleRuleTest, "McClient.McSim.WeatherCycleRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWeatherCycleRuleTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	const auto Run = [](FMcSimulation& Sim, int32 Ticks)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Sim.Tick();
		}
	};

	FMcSimulation Frozen(World);
	Frozen.SetWeatherCycle(false);
	Frozen.SetWeather(EMcWeather::Rain, 100);
	Run(Frozen, 500);
	TestTrue(TEXT("the rain goes on past its timer"), Frozen.GetWeather() == EMcWeather::Rain);
	Frozen.SetWeather(EMcWeather::Clear, 100);
	TestTrue(TEXT("a command still changes it"), Frozen.GetWeather() == EMcWeather::Clear);

	FMcSimulation Running(World);
	Running.SetWeather(EMcWeather::Rain, 100);
	Run(Running, 500);
	TestTrue(TEXT("with the cycle on the rain stops when its time is up"), Running.GetWeather() == EMcWeather::Clear);
	return true;
}

#endif
