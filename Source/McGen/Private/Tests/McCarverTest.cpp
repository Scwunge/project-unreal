#include "McCarver.h"
#include "McProviders.h"
#include "McRandom.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCarverTest, "McClient.McGen.Carver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	TSharedPtr<FJsonValue> ParseJson(const TCHAR* Text)
	{
		TSharedPtr<FJsonValue> Value;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value);
		return Value;
	}
}

bool FMcCarverTest::RunTest(const FString& Parameters)
{
	// Providers stay in their ranges, and the biased ones lean low.
	FMcRandom Random(5);
	FMcFloatProvider Trapezoid;
	TestTrue(TEXT("trapezoid parses"), Trapezoid.Parse(ParseJson(TEXT("{\"type\":\"minecraft:trapezoid\",\"min\":0,\"max\":6,\"plateau\":2}"))));
	FMcIntProvider Biased;
	TestTrue(TEXT("very_biased_to_bottom parses"), Biased.Parse(ParseJson(TEXT("{\"type\":\"minecraft:very_biased_to_bottom\",\"min_inclusive\":0,\"max_inclusive\":14}"))));
	FMcHeightProvider Uniform;
	TestTrue(TEXT("uniform height parses"), Uniform.Parse(ParseJson(TEXT("{\"type\":\"minecraft:uniform\",\"min_inclusive\":{\"above_bottom\":8},\"max_inclusive\":{\"absolute\":180}}"))));
	double TrapezoidSum = 0.0;
	double BiasedSum = 0.0;
	bool bInRange = true;
	for (int32 Index = 0; Index < 10000; ++Index)
	{
		const double T = Trapezoid.Sample(Random);
		const int32 B = Biased.Sample(Random);
		const int32 H = Uniform.Sample(Random, -64, 384);
		bInRange &= T >= 0.0 && T <= 6.0 && B >= 0 && B <= 14 && H >= -56 && H <= 180;
		TrapezoidSum += T;
		BiasedSum += B;
	}
	TestTrue(TEXT("samples stay in range"), bInRange);
	TestTrue(FString::Printf(TEXT("trapezoid centres on 3 (%.2f)"), TrapezoidSum / 10000), FMath::Abs(TrapezoidSum / 10000 - 3.0) < 0.1);
	TestTrue(FString::Printf(TEXT("very biased leans low (%.2f)"), BiasedSum / 10000), BiasedSum / 10000 < 3.0);

	// The overworld's carvers load and carve plausible amounts.
	for (const TCHAR* Id : { TEXT("minecraft:cave"), TEXT("minecraft:cave_extra_underground"), TEXT("minecraft:canyon") })
	{
		FMcCarver Carver;
		if (!TestTrue(FString::Printf(TEXT("%s loads"), Id), Carver.Load(Id, -64, 384)))
		{
			continue;
		}
		int32 Chunks = 0;
		int64 Blocks = 0;
		int32 MinY = MAX_int32;
		int32 MaxY = MIN_int32;
		for (int32 ChunkZ = 0; ChunkZ < 16; ++ChunkZ)
		{
			for (int32 ChunkX = 0; ChunkX < 16; ++ChunkX)
			{
				int32 Here = 0;
				Carver.Carve(ChunkX, ChunkZ, 12345, [](int32, int32) { return true; }, [&](int32 X, int32 Y, int32 Z)
				{
					++Here;
					MinY = FMath::Min(MinY, Y);
					MaxY = FMath::Max(MaxY, Y);
				});
				Chunks += Here > 0 ? 1 : 0;
				Blocks += Here;
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d of 256 chunks carved, %lld blocks, y %d to %d"), Id, Chunks, Blocks, MinY, MaxY));
		TestTrue(FString::Printf(TEXT("%s carves some chunks"), Id), Chunks > 0);
		TestTrue(FString::Printf(TEXT("%s stays off the bedrock floor"), Id), MinY > -64);
	}
	return true;
}

#endif
