#include "McBiomeRegistry.h"
#include "McBiomeSource.h"
#include "McBlockRegistry.h"
#include "McChunk.h"
#include "McNoiseGenerator.h"
#include "McRandom.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBiomeSourceTest, "McClient.McGen.BiomeSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBiomeSourceTest::RunTest(const FString& Parameters)
{
	FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	if (!TestTrue(TEXT("biome registry loads"), Registry.EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	TestTrue(TEXT("plains is a biome"), Registry.Find(TEXT("plains")).IsSet());

	FMcBiomeSource Source;
	if (!TestTrue(TEXT("overworld parameters load"), Source.Load(TEXT("minecraft:overworld"))))
	{
		return false;
	}
	TestTrue(TEXT("thousands of entries"), Source.NumEntries() > 5000);

	// The tree finds what checking every entry finds. Ties between overlapping entries can pick either.
	FMcRandom Random(11);
	int32 Differ = 0;
	int32 Hint = INDEX_NONE;
	constexpr int32 Samples = 3000;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		FMcClimate Climate;
		for (double& Value : Climate.Values)
		{
			Value = (Random.NextDouble() * 2.0 - 1.0) * 1.2;
		}
		Differ += Source.Find(Climate, Hint) != Source.FindSlow(Climate) ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("tree and full search agree (%d of %d differ)"), Differ, Samples), Differ <= Samples / 100);

	// Known spots in the climate space.
	FMcClimate DeepOcean;
	DeepOcean.Values[2] = -0.8;  // continentalness: deep ocean
	TestTrue(TEXT("low continentalness is ocean"), Registry.GetName(Source.FindSlow(DeepOcean)).ToString().Contains(TEXT("ocean")));
	FMcClimate Mushroom;
	Mushroom.Values[2] = -1.15;
	TestEqual(TEXT("very low continentalness is mushroom fields"), Registry.GetName(Source.FindSlow(Mushroom)), FName(TEXT("minecraft:mushroom_fields")));

	// A real world has many biomes at the surface.
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const FMcNoiseGenerator Generator(Settings, 12345, TEXT("minecraft:overworld"));
	TestTrue(TEXT("generator is valid"), Generator.IsValid());
	TMap<FName, int32> Counts;
	for (int32 Index = 0; Index < 900; ++Index)
	{
		const int32 X = (Index % 30) * 300 - 4500;
		const int32 Z = (Index / 30) * 300 - 4500;
		Counts.FindOrAdd(Registry.GetName(Generator.GetBiome(X, 64, Z)))++;
	}
	Counts.ValueSort([](int32 A, int32 B) { return A > B; });
	FString List;
	for (const TPair<FName, int32>& Entry : Counts)
	{
		List += FString::Printf(TEXT("%s %d, "), *Entry.Key.ToString().RightChop(10), Entry.Value);
	}
	AddInfo(List);
	TestTrue(FString::Printf(TEXT("many surface biomes (%d)"), Counts.Num()), Counts.Num() >= 15);

	// Generating a chunk fills its biomes and takes a sensible time.
	FMcChunk Chunk(FIntPoint(3, 5), -64, 24);
	const double Start = FPlatformTime::Seconds();
	Generator.Generate(Chunk);
	const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
	AddInfo(FString::Printf(TEXT("one chunk in %.1f ms"), Milliseconds));
	TestEqual(TEXT("chunk biome matches a direct lookup"), Chunk.GetBiome(4, 64, 8), Generator.GetBiome(3 * 16 + 4, 64, 5 * 16 + 8));
	return true;
}

#endif
