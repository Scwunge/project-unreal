#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McNoiseGenerator.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Development aid, not a check: lists where the surface biomes are in a seeded world (nearest to the origin first)
// and how high the ground is there, so a camera for a side-by-side shot can be placed in each kind of terrain.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorldSurveyTest, "McClient.McGen.WorldSurvey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWorldSurveyTest::RunTest(const FString& Parameters)
{
	FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const FMcNoiseGenerator Generator(Settings, 4242424242ull, TEXT("minecraft:overworld"));
	if (!TestTrue(TEXT("generator is valid"), Generator.IsValid()))
	{
		return false;
	}
	struct FSpot { int32 X, Z, Y; int64 Distance; };
	TMap<FName, TArray<FSpot>> Spots;
	TMap<FName, int32> Counts;
	for (int32 Z = -2400; Z <= 2400; Z += 64)
	{
		for (int32 X = -2400; X <= 2400; X += 64)
		{
			const int32 Y = Generator.FindSurfaceY(X, Z);
			const FName Name = Registry.GetName(Generator.GetBiome(X, Y, Z));
			Counts.FindOrAdd(Name)++;
			TArray<FSpot>& List = Spots.FindOrAdd(Name);
			List.Add({ X, Z, Y, int64(X) * X + int64(Z) * Z });
		}
	}
	Counts.ValueSort([](int32 A, int32 B) { return A > B; });
	for (const TPair<FName, int32>& Entry : Counts)
	{
		TArray<FSpot>& List = Spots[Entry.Key];
		List.Sort([](const FSpot& A, const FSpot& B) { return A.Distance < B.Distance; });
		FString Line = FString::Printf(TEXT("%s x%d:"), *Entry.Key.ToString().RightChop(10), Entry.Value);
		for (int32 Index = 0; Index < FMath::Min(3, List.Num()); ++Index)
		{
			Line += FString::Printf(TEXT(" (%d,%d y%d)"), List[Index].X, List[Index].Z, List[Index].Y);
		}
		AddInfo(Line);
	}
	return true;
}

// The biome mix at y 64 over a 48 by 48 grid 256 apart (the same grid the vanilla reference run samples), summed over
// several seeds: one seed has too few climate features to say what a world's share of each biome is.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBiomeMixTest, "McClient.McGen.BiomeMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBiomeMixTest::RunTest(const FString& Parameters)
{
	FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), Registry.EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	constexpr int32 Seeds = 8;
	TMap<FName, int32> Counts;
	int32 Total = 0;
	for (int32 SeedIndex = 0; SeedIndex < Seeds; ++SeedIndex)
	{
		const FMcNoiseGenerator Generator(Settings, SeedIndex == 0 ? 4242424242ull : 1000ull + SeedIndex * 7919ull, TEXT("minecraft:overworld"));
		for (int32 Z = 0; Z < 48; ++Z)
		{
			for (int32 X = 0; X < 48; ++X)
			{
				Counts.FindOrAdd(Registry.GetName(Generator.GetBiome(-6144 + X * 256, 64, -6144 + Z * 256)))++;
				++Total;
			}
		}
	}
	Counts.ValueSort([](int32 A, int32 B) { return A > B; });
	FString Line;
	for (const TPair<FName, int32>& Entry : Counts)
	{
		Line += FString::Printf(TEXT("%s=%.2f "), *Entry.Key.ToString().RightChop(10), 100.0 * Entry.Value / Total);
	}
	AddInfo(FString::Printf(TEXT("MIX %d seeds: %s"), Seeds, *Line));
	// The six climate values (temperature, humidity, continentalness, erosion, depth, weirdness) over the same grid for
	// the first seed, as histograms in steps of 0.2 from -1.2 to 1.2 (the first and last bins take the tails).
	{
		const FMcNoiseGenerator Generator(Settings, 4242424242ull, TEXT("minecraft:overworld"));
		int32 Bins[6][12] = {};
		for (int32 Z = 0; Z < 48; ++Z)
		{
			for (int32 X = 0; X < 48; ++X)
			{
				const FMcClimate Climate = Generator.SampleClimate(-6144 + X * 256, 64, -6144 + Z * 256);
				for (int32 Index = 0; Index < 6; ++Index)
				{
					Bins[Index][FMath::Clamp(FMath::FloorToInt((Climate.Values[Index] + 1.2) / 0.2), 0, 11)]++;
				}
			}
		}
		for (int32 Index = 0; Index < 6; ++Index)
		{
			FString Row;
			for (int32 Bin = 0; Bin < 12; ++Bin)
			{
				Row += FString::Printf(TEXT("%d "), Bins[Index][Bin]);
			}
			AddInfo(FString::Printf(TEXT("CLIMATE %d: %s"), Index, *Row));
		}
	}
	return true;
}

#endif
