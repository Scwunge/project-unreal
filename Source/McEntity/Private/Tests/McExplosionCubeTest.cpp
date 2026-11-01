#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McExplosion.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Which blocks of a solid 9x9x9 TNT cube a blast in its hollow middle reaches, against the reference client (scripts
// tnt6.txt: three cubes, a primed TNT of fuse 10 summoned at y 245.0 in each hollow centre, `log ents tnt`; the TNT
// entities that appear on the first blast tick are the blocks its rays destroyed, the rest light from later blasts):
// 447, 508 and 497 of the 728 blocks, mean 484. By distance of the block from the middle (shells of half a block) the
// two cubes at x 0 and 40 lit 0.97/0.96 at 4.0, 0.88/0.86 at 4.5, 0.56/0.46 at 5.0, 0.10/0.12 at 5.5 and none at 6.0; the
// cube at x -40 was looser (0.74, 0.62, 0.36, 0.23, 0.06), so the bands below take in all three. Our rays (the same
// 1352 rays, power times 0.7 to 1.3, 0.3 steps) give 0.96, 0.89, 0.53, 0.12 and none at 60 trials.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcExplosionCubeTest, "McClient.McEntity.ExplosionTntCube",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcExplosionCubeTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const FMcStateId Tnt = Registry.ParseState(TEXT("minecraft:tnt")).Get(0);
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	const FIntVector Middle(7, 14, 7);
	for (int32 X = -4; X <= 4; ++X)
	{
		for (int32 Y = -4; Y <= 4; ++Y)
		{
			for (int32 Z = -4; Z <= 4; ++Z)
			{
				if (X != 0 || Y != 0 || Z != 0)
				{
					World.SetBlock(Middle + FIntVector(X, Y, Z), Tnt);
				}
			}
		}
	}
	// Shells by twice the distance from the middle cell, counting how many of the blocks were reached.
	TMap<int32, int64> Hit, Total;
	for (int32 X = -4; X <= 4; ++X)
	{
		for (int32 Y = -4; Y <= 4; ++Y)
		{
			for (int32 Z = -4; Z <= 4; ++Z)
			{
				if (X != 0 || Y != 0 || Z != 0)
				{
					++Total.FindOrAdd(FMath::RoundToInt(FMath::Sqrt(static_cast<float>(X * X + Y * Y + Z * Z)) * 2.0f));
				}
			}
		}
	}
	constexpr int32 Trials = 60;
	int64 Sum = 0;
	for (int32 Trial = 0; Trial < Trials; ++Trial)
	{
		FRandomStream Random(100 + Trial);
		TArray<FIntVector> Cells;
		McExplosion::Collect(World, FVector(7.5, 14.0625, 7.5), 4.0f, Random, Cells);
		for (const FIntVector& Cell : Cells)
		{
			const FIntVector Off = Cell - Middle;
			if (FMath::Abs(Off.X) <= 4 && FMath::Abs(Off.Y) <= 4 && FMath::Abs(Off.Z) <= 4 && !(Off.X == 0 && Off.Y == 0 && Off.Z == 0))
			{
				++Sum;
				++Hit.FindOrAdd(FMath::RoundToInt(FMath::Sqrt(static_cast<float>(Off.X * Off.X + Off.Y * Off.Y + Off.Z * Off.Z)) * 2.0f));
			}
		}
	}
	const double Mean = static_cast<double>(Sum) / Trials;
	TestTrue(*FString::Printf(TEXT("a blast reaches about 484 of the 728 blocks (got %.1f)"), Mean), FMath::Abs(Mean - 484.0) < 40.0);
	const auto Share = [&](float Distance)
	{
		const int32 Key = FMath::RoundToInt(Distance * 2.0f);
		return static_cast<double>(Hit.FindRef(Key)) / (static_cast<double>(Total.FindRef(Key)) * Trials);
	};
	for (float Distance = 3.5f; Distance <= 7.0f; Distance += 0.5f)
	{
		AddInfo(FString::Printf(TEXT("share at %.1f: %.3f"), Distance, Share(Distance)));
	}
	TestTrue(*FString::Printf(TEXT("nearly all within 3.5 blocks (%.2f)"), Share(3.5f)), Share(3.5f) > 0.97);
	TestTrue(*FString::Printf(TEXT("4.0 away 0.89 (%.2f)"), Share(4.0f)), FMath::Abs(Share(4.0f) - 0.89) < 0.12);
	TestTrue(*FString::Printf(TEXT("4.5 away 0.79 (%.2f)"), Share(4.5f)), FMath::Abs(Share(4.5f) - 0.79) < 0.13);
	TestTrue(*FString::Printf(TEXT("5.0 away 0.46 (%.2f)"), Share(5.0f)), FMath::Abs(Share(5.0f) - 0.46) < 0.12);
	TestTrue(*FString::Printf(TEXT("5.5 away 0.15 (%.2f)"), Share(5.5f)), FMath::Abs(Share(5.5f) - 0.15) < 0.09);
	TestTrue(*FString::Printf(TEXT("6.0 away 0.02 (%.2f)"), Share(6.0f)), Share(6.0f) < 0.1);
	// And the game's own blast lights exactly those TNT blocks (each becomes a primed TNT, the block gone), none of the
	// rest, which wait for the lit ones' blasts.
	{
		FMcEntityManager Manager(World);
		Manager.Explode(FVector(7.5, 14.0625, 7.5), 4.0f);
		int32 Gone = 0;
		for (int32 X = -4; X <= 4; ++X)
		{
			for (int32 Y = -4; Y <= 4; ++Y)
			{
				for (int32 Z = -4; Z <= 4; ++Z)
				{
					const FIntVector Cell = Middle + FIntVector(X, Y, Z);
					Gone += (X != 0 || Y != 0 || Z != 0) && World.GetBlock(Cell) != Tnt ? 1 : 0;
				}
			}
		}
		TestTrue(*FString::Printf(TEXT("one blast lights 400 to 570 of the cube's TNT (%d), not all of it"), Gone), Gone > 400 && Gone < 570);
	}
	return true;
}

#endif
