// The pure rules of Unreal Mode's immersive audio: enclosure to reverb, occlusion, light and the cave mood, echoes.

#include "McAcoustics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcAcousticsTest, "McClient.McAudio.Acoustics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcAcousticsTest::RunTest(const FString& Parameters)
{
	using namespace McAcoustics;
	TestEqual(TEXT("twelve rays"), GetRayDirections().Num(), NumRays);

	// Open ground: only the floor is solid.
	const auto Ground = [](const FIntVector& Cell) { return Cell.Y <= 0; };
	const FEnclosure Open = EstimateEnclosure(FVector(0.5, 1.5, 0.5), Ground);
	TestTrue(TEXT("open sky overhead"), Open.SkyOpenness > 0.99f);
	const FReverb OpenReverb = ToReverb(Open, false);
	TestTrue(*FString::Printf(TEXT("open air has almost no reverb (%.2f)"), OpenReverb.Volume), OpenReverb.Volume < 0.15f);

	// A closet: a 3x3x3 hollow in stone.
	const auto Closet = [](const FIntVector& Cell) { return Cell.X < 0 || Cell.X > 2 || Cell.Y < 0 || Cell.Y > 2 || Cell.Z < 0 || Cell.Z > 2; };
	const FEnclosure Small = EstimateEnclosure(FVector(1.5, 1.5, 1.5), Closet);
	TestEqual(TEXT("nothing escapes a closet"), Small.Openness, 0.0f);
	TestTrue(TEXT("it is small"), Small.MeanDistance < 3.0f);
	const FReverb SmallReverb = ToReverb(Small, false);
	TestTrue(TEXT("a small room has a short tail"), SmallReverb.DecayTime < 0.8f);
	TestTrue(TEXT("and a good deal of it"), SmallReverb.Volume > 0.5f);

	// A cavern 30 blocks across.
	const auto Cavern = [](const FIntVector& Cell) { return FMath::Abs(Cell.X) > 15 || FMath::Abs(Cell.Y) > 15 || FMath::Abs(Cell.Z) > 15; };
	const FEnclosure Big = EstimateEnclosure(FVector(0.5, 0.5, 0.5), Cavern);
	TestEqual(TEXT("nothing escapes a cavern"), Big.Openness, 0.0f);
	const FReverb BigReverb = ToReverb(Big, false);
	TestTrue(*FString::Printf(TEXT("a cavern has a long echo (%.1f s)"), BigReverb.DecayTime), BigReverb.DecayTime > 2.5f);
	TestTrue(TEXT("longer than the closet's"), BigReverb.DecayTime > SmallReverb.DecayTime * 4.0f);
	TestTrue(TEXT("with the highs damped"), BigReverb.GainHF < SmallReverb.GainHF);

	// The Nether is a big dark hall even on open ground.
	const FReverb Nether = ToReverb(Open, true);
	TestTrue(TEXT("hell has reverb in the open"), Nether.Volume >= 0.55f);
	TestTrue(TEXT("a long tail"), Nether.DecayTime >= 4.5f);
	TestTrue(TEXT("and a dark one"), Nether.GainHF <= 0.4f);

	// Summarising by hand: all rays escaped.
	TArray<float> Escaped;
	Escaped.Init(MaxRayDistance, NumRays);
	TestEqual(TEXT("all escaped is open"), Summarise(Escaped).Openness, 1.0f);

	// Occlusion: each solid block between takes more.
	const FOcclusion None = GetOcclusion(0);
	TestEqual(TEXT("clear air is full volume"), None.Volume, 1.0f);
	TestTrue(TEXT("with no filter"), None.LowpassHz >= 20000.0f);
	const FOcclusion One = GetOcclusion(1);
	const FOcclusion Two = GetOcclusion(2);
	const FOcclusion Many = GetOcclusion(20);
	TestTrue(TEXT("a block muffles"), One.Volume < 1.0f && One.LowpassHz < 10000.0f);
	TestTrue(TEXT("two muffle more"), Two.Volume < One.Volume && Two.LowpassHz < One.LowpassHz);
	TestTrue(TEXT("but not to nothing"), Many.Volume >= 0.35f && Many.LowpassHz >= 500.0f);

	// The solid blocks between two points.
	const auto Wall = [](const FIntVector& Cell) { return Cell.X == 4 || Cell.X == 5; };
	TestEqual(TEXT("a wall two blocks thick"), CountSolidBetween(FVector(0.5, 0.5, 0.5), FVector(10.5, 0.5, 0.5), Wall), 2);
	TestEqual(TEXT("nothing on this side of it"), CountSolidBetween(FVector(0.5, 0.5, 0.5), FVector(3.5, 0.5, 0.5), Wall), 0);
	TestEqual(TEXT("a sound inside the wall isn't muffled by the block it is in"), CountSolidBetween(FVector(4.5, 0.5, 0.5), FVector(0.5, 0.5, 0.5), Wall), 0);
	TestEqual(TEXT("the other way round"), CountSolidBetween(FVector(10.5, 0.5, 0.5), FVector(0.5, 0.5, 0.5), Wall), 2);
	TestTrue(TEXT("water is duller than any wall"), UnderwaterLowpassHz < 1000.0f);

	// Light: open sky overhead, and a lamp nearby.
	TestTrue(TEXT("sky is open"), IsSkyOpen(FIntVector(0, 1, 0), Ground));
	const auto Roof = [](const FIntVector& Cell) { return Cell.Y == 10; };
	TestFalse(TEXT("a roof shuts it"), IsSkyOpen(FIntVector(0, 1, 0), Roof));
	const auto Lamp = [](const FIntVector& Cell) { return Cell == FIntVector(3, 0, 0) ? 14 : 0; };
	TestEqual(TEXT("a lamp three blocks away"), EstimateBlockLight(FIntVector(0, 0, 0), Lamp), 11);
	TestEqual(TEXT("no lamp, no light"), EstimateBlockLight(FIntVector(40, 0, 0), Lamp), 0);

	// The mood meter: dark fills it in the delay, light never does and empties it.
	float Meter = 0.0f;
	int32 Ticks = 0;
	while (!TickMood(Meter, 0, 100) && Ticks < 1000)
	{
		++Ticks;
	}
	TestTrue(*FString::Printf(TEXT("full darkness plays in the delay (%d ticks)"), Ticks), Ticks >= 98 && Ticks <= 101);
	TestEqual(TEXT("and starts over"), Meter, 0.0f);
	Meter = 0.5f;
	for (int32 Tick = 0; Tick < 5000; ++Tick)
	{
		TestFalse(TEXT("lit, it never plays"), TickMood(Meter, 15, 100));
	}
	TestEqual(TEXT("the light empties it"), Meter, 0.0f);
	float Dim = 0.0f;
	int32 DimTicks = 0;
	while (!TickMood(Dim, 4, 100) && DimTicks < 1000)
	{
		++DimTicks;
	}
	TestTrue(TEXT("dim takes longer than dark"), DimTicks > Ticks);

	// Echoes: a far sound in a cavern comes back, a near one or one in the open doesn't.
	const FEcho Near = GetEcho(5.0f, Big);
	TestEqual(TEXT("no echo for a near sound"), Near.Volume, 0.0f);
	const FEcho Far = GetEcho(30.0f, Big);
	TestTrue(TEXT("a far one in a cavern echoes"), Far.Volume > 0.1f && Far.DelaySeconds >= 0.2f && Far.DelaySeconds <= 0.9f);
	TestEqual(TEXT("none in the open"), GetEcho(30.0f, Open).Volume, 0.0f);
	return true;
}

#endif
