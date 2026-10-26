#include "HAL/FileManager.h"
#include "McCache.h"
#include "McNoise.h"
#include "McRandom.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcNoiseTest, "McClient.McGen.Noise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	struct FStats
	{
		double Mean = 0.0;
		double StdDev = 0.0;
		double MaxAbs = 0.0;
	};

	template <typename FSampler>
	FStats Measure(FMcRandom& Points, double Spread, int32 Count, FSampler Sampler)
	{
		TArray<double> Values;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Values.Add(Sampler((Points.NextDouble() - 0.5) * Spread, (Points.NextDouble() - 0.5) * 256.0, (Points.NextDouble() - 0.5) * Spread));
		}
		FStats Stats;
		for (const double Value : Values)
		{
			Stats.Mean += Value / Count;
			Stats.MaxAbs = FMath::Max(Stats.MaxAbs, FMath::Abs(Value));
		}
		for (const double Value : Values)
		{
			Stats.StdDev += FMath::Square(Value - Stats.Mean) / Count;
		}
		Stats.StdDev = FMath::Sqrt(Stats.StdDev);
		return Stats;
	}
}

bool FMcNoiseTest::RunTest(const FString& Parameters)
{
	// The random stream is repeatable, bounded and forks into independent streams.
	{
		FMcRandom A(12345);
		FMcRandom B(12345);
		bool bSame = true;
		for (int32 Index = 0; Index < 100; ++Index)
		{
			bSame &= A.NextLong() == B.NextLong();
		}
		TestTrue(TEXT("same seed, same stream"), bSame);

		FMcRandom C(1);
		int32 Counts[6] = {};
		bool bInRange = true;
		for (int32 Index = 0; Index < 60000; ++Index)
		{
			const int32 Value = C.NextInt(6);
			bInRange &= Value >= 0 && Value < 6;
			Counts[FMath::Clamp(Value, 0, 5)]++;
		}
		TestTrue(TEXT("NextInt stays in range"), bInRange);
		for (const int32 Count : Counts)
		{
			TestTrue(TEXT("NextInt is uniform"), Count > 9500 && Count < 10500);
		}
		const double D = C.NextDouble();
		TestTrue(TEXT("NextDouble in [0, 1)"), D >= 0.0 && D < 1.0);

		const FMcRandom World(42);
		TestEqual(TEXT("forks by name repeat"), World.Fork(TEXT("minecraft:ridge")).Fork(1, 2, 3).NextLong(), FMcRandom(42).Fork(TEXT("minecraft:ridge")).Fork(1, 2, 3).NextLong());
		TestNotEqual(TEXT("different names, different streams"), World.Fork(TEXT("minecraft:ridge")).NextLong(), World.Fork(TEXT("minecraft:erosion")).NextLong());
		TestNotEqual(TEXT("different seeds, different streams"), World.Fork(TEXT("minecraft:ridge")).NextLong(), FMcRandom(43).Fork(TEXT("minecraft:ridge")).NextLong());
	}

	FMcRandom Points(7);

	// Improved noise is smooth, bounded and has the deviation the octave normalisation assumes.
	{
		FMcRandom Random(99);
		const FMcImprovedNoise Noise(Random);
		const FStats Stats = Measure(Points, 400.0, 50000, [&](double X, double Y, double Z) { return Noise.Sample(X, Y, Z); });
		TestTrue(TEXT("improved noise stays within -1..1"), Stats.MaxAbs <= 1.0);
		TestTrue(FString::Printf(TEXT("improved noise deviation %.3f is about %.2f"), Stats.StdDev, FMcImprovedNoise::StdDev), FMath::Abs(Stats.StdDev - FMcImprovedNoise::StdDev) < 0.02);
		const double Step = FMath::Abs(Noise.Sample(10.3, 4.2, 7.7) - Noise.Sample(10.3001, 4.2, 7.7));
		TestTrue(TEXT("improved noise is continuous"), Step < 0.001);
	}

	// Every noise in the data loads, and comes out with the amplitude it asks for.
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *McCache::Data(TEXT("worldgen/noise/*.json")), true, false);
	TestTrue(TEXT("the jar has noises"), Files.Num() > 50);
	for (const FString& File : Files)
	{
		const FString Id = TEXT("minecraft:") + FPaths::GetBaseFilename(File);
		FMcNoiseParams Params;
		if (!TestTrue(FString::Printf(TEXT("%s loads"), *Id), FMcNoiseParams::Load(Id, Params)))
		{
			continue;
		}
		FMcRandom Random = FMcRandom(5).Fork(Id);
		const FMcNoise Noise(Random, Params);
		// Spread wide enough to cover the lowest octave several times over.
		const double Spread = 8.0 / FMath::Pow(2.0, Params.BaseOctave);
		const FStats Stats = Measure(Points, Spread, 4000, [&](double X, double Y, double Z) { return Noise.Sample(X, Y, Z); });
		const double Expected = Params.BaseAmplitude / 3.0;
		TestTrue(FString::Printf(TEXT("%s deviation %.3f is near %.3f"), *Id, Stats.StdDev, Expected), Stats.StdDev > Expected * 0.6 && Stats.StdDev < Expected * 1.4);
		TestTrue(FString::Printf(TEXT("%s stays under its maximum"), *Id), Stats.MaxAbs <= Noise.GetMaxValue());
	}

	// The classic blended terrain noise has small, varied values.
	{
		FMcRandom Random(3);
		const FMcBlendedNoise Noise(Random, 0.25, 0.125, 80.0, 160.0, 8.0);
		const FStats Stats = Measure(Points, 2000.0, 4000, [&](double X, double Y, double Z) { return Noise.Sample(X, Y, Z); });
		AddInfo(FString::Printf(TEXT("blended noise deviation %.3f, max %.3f"), Stats.StdDev, Stats.MaxAbs));
		TestTrue(TEXT("blended noise varies"), Stats.StdDev > 0.02 && Stats.StdDev < 0.5);
	}
	return true;
}

#endif
