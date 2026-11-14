// Pins how well the overworld's climate and biome stage matches the reference client, as "ours inside vanilla's own variation".
//
// Not seed-exact (see PLAN.md, "Known limitation: seed-exact noise"), so a world is compared with the reference through
// distributions: the reference's `log cbiomes` grid (64 by 64 points 256 blocks apart from -8192, y 64; five worlds, files
// Content/Tests/vanilla_cb_<n>.txt, "x z t h c e d w biome" in 1/10000) against the same grid of five seeds of ours. For
// each measure the reference's five worlds give a minimum and a maximum; ours counts as matching when the mean over our five
// seeds lies between them. The measures are the 5/25/50/75/95 percentiles of each of the six climate parameters, and the
// share of the grid held by every biome that holds at least 0.5% of it in some world.
// Measured 2026-11-13: 27 of 30 percentiles and 39 of 42 biome shares match; the floors below leave a little room for
// the mean of five seeds to move, and each is a failure if the match gets worse.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "McBiomeRegistry.h"
#include "McBiomeSource.h"
#include "McBlockRegistry.h"
#include "McNoiseGenerator.h"

namespace
{
	struct FGridPoint
	{
		double Values[6];
		FName Biome;
	};

	double Percentile(TArray<double>& Sorted, double Fraction)
	{
		return Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt32(Sorted.Num() * Fraction))];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVanillaMatchTest, "McClient.McGen.VanillaMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcVanillaMatchTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	FMcBiomeSource Source;
	if (!TestTrue(TEXT("biome source"), Source.Load(TEXT("minecraft:overworld"))))
	{
		return false;
	}

	TArray<TArray<FGridPoint>> Vanilla;
	for (int32 World = 1; World <= 5; ++World)
	{
		FString Text;
		if (!TestTrue(TEXT("reference grid"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / FString::Printf(TEXT("Tests/vanilla_cb_%d.txt"), World)))))
		{
			return false;
		}
		TArray<FString> Rows;
		Text.ParseIntoArrayLines(Rows);
		TArray<FGridPoint>& Points = Vanilla.AddDefaulted_GetRef();
		for (const FString& Row : Rows)
		{
			TArray<FString> Parts;
			Row.ParseIntoArrayWS(Parts);
			if (Parts.Num() < 9)
			{
				continue;
			}
			FGridPoint Point;
			for (int32 Index = 0; Index < 6; ++Index)
			{
				Point.Values[Index] = FCString::Atod(*Parts[2 + Index]) / 10000.0;
			}
			FString Name = Parts[8];
			Name.RemoveFromStart(TEXT("ResourceKey[minecraft:worldgen/biome / "));
			Name.RemoveFromEnd(TEXT("]"));
			Point.Biome = FName(*Name);
			Points.Add(Point);
		}
	}

	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
	TArray<TArray<FGridPoint>> Ours;
	for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
	{
		const FMcNoiseGenerator Generator(Settings, Seeds[SeedIndex], TEXT("minecraft:overworld"));
		TArray<FGridPoint>& Points = Ours.AddDefaulted_GetRef();
		for (int32 J = 0; J < 64; ++J)
		{
			for (int32 I = 0; I < 64; ++I)
			{
				const FMcClimate Climate = Generator.SampleClimate(-8192 + I * 256, 64, -8192 + J * 256);
				int32 Hint = INDEX_NONE;
				FGridPoint Point;
				for (int32 Index = 0; Index < 6; ++Index)
				{
					Point.Values[Index] = FMath::RoundToInt64(Climate.Values[Index] * 10000) / 10000.0;
				}
				Point.Biome = Registry.GetName(Source.Find(Climate, Hint));
				Points.Add(Point);
			}
		}
	}

	const auto Inside = [](const TArray<double>& VanillaValues, const TArray<double>& OurValues)
	{
		double Mean = 0.0;
		for (const double Value : OurValues)
		{
			Mean += Value / OurValues.Num();
		}
		return Mean >= FMath::Min(VanillaValues) - 1e-9 && Mean <= FMath::Max(VanillaValues) + 1e-9;
	};

	// The percentiles of each climate parameter.
	static const TCHAR* ParameterNames[6] = { TEXT("temperature"), TEXT("humidity"), TEXT("continentalness"), TEXT("erosion"), TEXT("depth"), TEXT("weirdness") };
	static const double Fractions[5] = { 0.05, 0.25, 0.5, 0.75, 0.95 };
	int32 PercentilesTotal = 0;
	int32 PercentilesInside = 0;
	for (int32 Parameter = 0; Parameter < 6; ++Parameter)
	{
		for (const double Fraction : Fractions)
		{
			TArray<double> VanillaValues;
			TArray<double> OurValues;
			for (TArray<FGridPoint>& Points : Vanilla)
			{
				TArray<double> Sorted;
				for (const FGridPoint& Point : Points) { Sorted.Add(Point.Values[Parameter]); }
				Sorted.Sort();
				VanillaValues.Add(Percentile(Sorted, Fraction));
			}
			for (TArray<FGridPoint>& Points : Ours)
			{
				TArray<double> Sorted;
				for (const FGridPoint& Point : Points) { Sorted.Add(Point.Values[Parameter]); }
				Sorted.Sort();
				OurValues.Add(Percentile(Sorted, Fraction));
			}
			++PercentilesTotal;
			const bool bInside = Inside(VanillaValues, OurValues);
			PercentilesInside += bInside ? 1 : 0;
			if (!bInside)
			{
				double Mean = 0.0;
				for (const double Value : OurValues) { Mean += Value / OurValues.Num(); }
				AddInfo(FString::Printf(TEXT("OUTSIDE %s p%d: reference %.3f to %.3f, ours %.3f"), ParameterNames[Parameter], FMath::RoundToInt32(Fraction * 100),
					FMath::Min(VanillaValues), FMath::Max(VanillaValues), Mean));
			}
		}
	}

	// The share of the grid per biome.
	TSet<FName> BiomeNames;
	for (const TArray<FGridPoint>& Points : Vanilla) { for (const FGridPoint& Point : Points) { BiomeNames.Add(Point.Biome); } }
	for (const TArray<FGridPoint>& Points : Ours) { for (const FGridPoint& Point : Points) { BiomeNames.Add(Point.Biome); } }
	int32 BiomesTotal = 0;
	int32 BiomesInside = 0;
	for (const FName Biome : BiomeNames)
	{
		TArray<double> VanillaValues;
		TArray<double> OurValues;
		for (const TArray<FGridPoint>& Points : Vanilla)
		{
			int32 Count = 0;
			for (const FGridPoint& Point : Points) { Count += Point.Biome == Biome ? 1 : 0; }
			VanillaValues.Add(static_cast<double>(Count) / Points.Num());
		}
		for (const TArray<FGridPoint>& Points : Ours)
		{
			int32 Count = 0;
			for (const FGridPoint& Point : Points) { Count += Point.Biome == Biome ? 1 : 0; }
			OurValues.Add(static_cast<double>(Count) / Points.Num());
		}
		if (FMath::Max(VanillaValues) < 0.005 && FMath::Max(OurValues) < 0.005)
		{
			continue;
		}
		++BiomesTotal;
		const bool bInside = Inside(VanillaValues, OurValues);
		BiomesInside += bInside ? 1 : 0;
		if (!bInside)
		{
			double Mean = 0.0;
			for (const double Value : OurValues) { Mean += Value / OurValues.Num(); }
			AddInfo(FString::Printf(TEXT("OUTSIDE biome %s share: reference %.4f to %.4f, ours %.4f"), *Biome.ToString(), FMath::Min(VanillaValues), FMath::Max(VanillaValues), Mean));
		}
	}

	AddInfo(FString::Printf(TEXT("MATCH climate percentiles %d of %d inside the reference's five-world range, biome shares %d of %d"), PercentilesInside, PercentilesTotal, BiomesInside, BiomesTotal));
	TestTrue(TEXT("at least 25 of 30 climate percentiles inside the reference range"), PercentilesInside >= 25);
	TestTrue(TEXT("at least 35 of 42 biome shares inside the reference range"), BiomesInside >= 35 && BiomesTotal >= 38);
	return true;
}
