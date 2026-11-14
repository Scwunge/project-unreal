// Where chunk decoration spends its time. Decorates the 9 by 9 chunks inside a generated patch one thread, in a fixed
// order, at three places (the world start and two others), and prints chunks per second, the seconds in the structure
// stage and the placed features that take the most (GMcDecorateProfile). No asserts about speed; a speed-up must not
// change a block, which McClient.McGen.DecorateThreads and the printed block hash checks.
#include "Async/ParallelFor.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McNoiseGenerator.h"
#include "McStructures.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDecorateProfileTest, "McClient.McGen.DecorateProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDecorateProfileTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	constexpr int32 Across = 11;
	const uint64 Seeds[] = { 12ull, 777ull, 31337ull };
	const FIntPoint Corners[] = { FIntPoint(-5, -5), FIntPoint(900, -400), FIntPoint(-1500, 2200) };
	TMap<FString, double> Profile;
	int64 HeightCalls = 0;
	double HeightSeconds = 0.0;
	TSet<FIntPoint> HeightColumns;
	double TotalSeconds = 0.0;
	int32 TotalChunks = 0;
	uint32 Hash = 0;
	for (int32 Place = 0; Place < 3; ++Place)
	{
		const FMcNoiseGenerator Generator(Settings, Seeds[Place], TEXT("minecraft:overworld"));
		TArray<TUniquePtr<FMcChunk>> Patch;
		Patch.SetNum(Across * Across);
		ParallelFor(Across * Across, [&](int32 Index)
		{
			Patch[Index] = MakeUnique<FMcChunk>(Corners[Place] + FIntPoint(Index % Across, Index / Across), Settings.MinY, Settings.Height / 16);
			Generator.Generate(*Patch[Index]);
		});
		FMcFeatureLibrary Library(Settings.SeaLevel, Settings.MinY, Settings.Height);
		Library.LoadBiomes();
		const FMcNoiseGenerator* Noise = &Generator;
		Library.SetHeightSource([Noise, &HeightCalls, &HeightColumns, &HeightSeconds](int32 X, int32 Z) { const double T0 = FPlatformTime::Seconds(); const int32 Result = Noise->FindSurfaceY(X, Z) + 1; HeightSeconds += FPlatformTime::Seconds() - T0; ++HeightCalls; HeightColumns.Add(FIntPoint(X, Z)); return Result; });
		Library.SetBiomeSource([Noise](int32 X, int32 Y, int32 Z) { return Noise->GetBiome(X, Y, Z); });

		GMcDecorateProfile = &Profile;
		const double Began = FPlatformTime::Seconds();
		for (int32 Z = 1; Z < Across - 1; ++Z)
		{
			for (int32 X = 1; X < Across - 1; ++X)
			{
				FMcChunk* Around[9];
				for (int32 Index = 0; Index < 9; ++Index)
				{
					Around[Index] = Patch[(Z + Index / 3 - 1) * Across + X + Index % 3 - 1].Get();
				}
				FMcGenRegion Region(Corners[Place] + FIntPoint(X, Z), Around);
				Library.Decorate(Region, Seeds[Place]);
				++TotalChunks;
			}
		}
		TotalSeconds += FPlatformTime::Seconds() - Began;
		GMcDecorateProfile = nullptr;
		for (const TUniquePtr<FMcChunk>& Chunk : Patch)
		{
			for (int32 Y = Chunk->GetMinY(); Y <= Chunk->GetMaxY(); ++Y)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 X = 0; X < 16; ++X)
					{
						Hash = HashCombineFast(Hash, static_cast<uint32>(Chunk->GetBlock(X, Y, Z)));
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("DECOPROF height lookups %lld, distinct columns %d, %.2f ms a chunk, %.2f ms a lookup"), HeightCalls, HeightColumns.Num(), 1000.0 * HeightSeconds / TotalChunks, 1000.0 * HeightSeconds / FMath::Max<int64>(1, HeightCalls)));
	Profile.ValueSort([](double A, double B) { return A > B; });
	AddInfo(FString::Printf(TEXT("DECOPROF %d chunks in %.2f s: %.2f chunks/s, %.1f ms a chunk, block hash %08x"), TotalChunks, TotalSeconds, TotalChunks / TotalSeconds, 1000.0 * TotalSeconds / TotalChunks, Hash));
	int32 Shown = 0;
	for (const TPair<FString, double>& Entry : Profile)
	{
		AddInfo(FString::Printf(TEXT("DECOPROF %6.1f ms/chunk  %4.1f%%  %s"), 1000.0 * Entry.Value / TotalChunks, 100.0 * Entry.Value / TotalSeconds, *Entry.Key));
		if (++Shown >= 30)
		{
			break;
		}
	}
	return true;
}

#endif
