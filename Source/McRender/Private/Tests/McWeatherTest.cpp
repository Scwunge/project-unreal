#include "McWeatherComponent.h"
#include "McLightningComponent.h"
#include "McCloudComponent.h"
#include "McBiomeRegistry.h"
#include "McDimensionType.h"
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

	// The clouds drift west 0.03 blocks a tick, repeating with the picture; cells wrap round it either way.
	TestEqual(TEXT("no drift at the start"), McClouds::GetDrift(0.0, 256), 0.0);
	TestEqual(TEXT("a day's drift"), McClouds::GetDrift(24000.0, 256), 720.0, 1e-6);
	TestEqual(TEXT("back round after the picture"), McClouds::GetDrift(3072.0 / 0.03, 256), 0.0, 1e-6);
	TestTrue(TEXT("rows sit a third of a cell past z 0 (cloudph, 3.9 +- 0.3)"), FMath::Abs(McClouds::ZOffset - 3.9) < 0.3);
	TestEqual(TEXT("a cell past the edge"), McClouds::WrapCell(FIntPoint(256, 3), 256, 256), FIntPoint(0, 3));
	TestEqual(TEXT("a cell before the corner"), McClouds::WrapCell(FIntPoint(-1, -257), 256, 256), FIntPoint(255, 255));

	// The Overworld's clouds and music from its dimension type; the Nether has neither clouds.
	FMcDimensionType Overworld, Nether;
	if (FMcDimensionType::Load(TEXT("minecraft:overworld"), Overworld) && FMcDimensionType::Load(TEXT("minecraft:the_nether"), Nether))
	{
		TestTrue(TEXT("Overworld clouds at 192.33"), Overworld.CloudHeight.IsSet() && FMath::IsNearlyEqual(*Overworld.CloudHeight, 192.33f, 0.01f));
		TestTrue(TEXT("white, a fifth see-through"), Overworld.CloudColor.Equals(FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), 0.01f));
		TestFalse(TEXT("no clouds in the Nether"), Nether.CloudHeight.IsSet());
		const FMcDimensionType::FMusic* Game = Overworld.Music.Find(TEXT("default"));
		TestTrue(TEXT("Overworld music"), Game && Game->Sound == FName(TEXT("music.game")) && Game->MinDelay == 12000 && Game->MaxDelay == 24000);
		TestTrue(TEXT("creative music"), Overworld.Music.Contains(TEXT("creative")));
	}
	else
	{
		AddInfo(TEXT("No dimension types in cache/: the cloud data checks were skipped."));
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
