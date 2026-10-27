#include "McWeatherComponent.h"
#include "McLightningComponent.h"
#include "McBiomeRegistry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWeatherRenderTest, "McClient.McRender.Weather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcWeatherRenderTest::RunTest(const FString& Parameters)
{
	// A column's sheet: ten blocks below and above the camera on open ground far below.
	FInt32Interval Span = McWeather::GetColumnSpan(70, 40);
	TestEqual(TEXT("from ten below"), Span.Min, 60);
	TestEqual(TEXT("to ten above"), Span.Max, 80);
	// Standing on the ground: it stops at the ground.
	Span = McWeather::GetColumnSpan(70, 64);
	TestEqual(TEXT("down to the ground"), Span.Min, 65);
	TestEqual(TEXT("still ten above"), Span.Max, 80);
	// Under a roof high above: nothing to draw.
	Span = McWeather::GetColumnSpan(70, 90);
	TestTrue(TEXT("a roof high above hides it"), Span.Min >= Span.Max);

	TestEqual(TEXT("full at the camera"), McWeather::GetFade(0.0f), 1.0f);
	TestEqual(TEXT("a half at the edge"), McWeather::GetFade(100.0f), 0.5f);
	TestEqual(TEXT("none beyond"), McWeather::GetFade(101.0f), 0.0f);

	// A bolt: three strands, the main one of eight segments reaching the strike from 128 blocks up, the forks hanging
	// off it in the air; the same seed, the same bolt.
	{
		const TArray<McLightning::FSegment> Bolt = McLightning::BuildBolt(5);
		int32 Main = 0;
		TSet<int32> Strands;
		for (const McLightning::FSegment& Segment : Bolt)
		{
			Strands.Add(Segment.Strand);
			Main += Segment.Strand == 0 ? 1 : 0;
			TestTrue(TEXT("each segment goes up"), Segment.Top.Y > Segment.Bottom.Y);
			if (Segment.Strand != 0)
			{
				TestTrue(TEXT("forks end above the ground"), Segment.Bottom.Y > 0.0);
			}
		}
		TestEqual(TEXT("three strands"), Strands.Num(), 3);
		TestEqual(TEXT("eight in the main"), Main, McLightning::SegmentsPerStrand);
		TestTrue(TEXT("it reaches the strike"), Bolt[0].Bottom.Equals(FVector::ZeroVector));
		TestEqual(TEXT("from 128 blocks up"), Bolt[McLightning::SegmentsPerStrand - 1].Top.Y, 128.0);
		TestEqual(TEXT("the same seed, the same bolt"), McLightning::BuildBolt(5).Num(), Bolt.Num());
		TestTrue(TEXT("and the same shape"), McLightning::BuildBolt(5)[3].Top.Equals(Bolt[3].Top));
	}

	// The biomes' climates, from the cache when it's there.
	FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	if (!Biomes.EnsureLoaded())
	{
		AddInfo(TEXT("No biomes in cache/: the climate checks were skipped."));
		return true;
	}
	using EPrecipitation = FMcBiomeRegistry::EPrecipitation;
	const FMcBiomeId Plains = Biomes.Find(TEXT("minecraft:plains")).Get(0);
	TestEqual(TEXT("plains: 0.8"), Biomes.GetTemperature(Plains, 64), 0.8f);
	TestTrue(TEXT("rain on the plains"), Biomes.GetPrecipitation(Plains, 64) == EPrecipitation::Rain);
	if (const TOptional<FMcBiomeId> Desert = Biomes.Find(TEXT("minecraft:desert")))
	{
		TestTrue(TEXT("nothing in the desert"), Biomes.GetPrecipitation(*Desert, 64) == EPrecipitation::None);
	}
	if (const TOptional<FMcBiomeId> Snowy = Biomes.Find(TEXT("minecraft:snowy_plains")))
	{
		TestTrue(TEXT("snow on the snowy plains"), Biomes.GetPrecipitation(*Snowy, 64) == EPrecipitation::Snow);
	}
	// Higher is colder: at 0.8, snow begins 520 blocks above y 80.
	TestTrue(TEXT("plains are colder high up"), Biomes.GetTemperature(Plains, 280) < 0.6f);
	return true;
}

#endif
