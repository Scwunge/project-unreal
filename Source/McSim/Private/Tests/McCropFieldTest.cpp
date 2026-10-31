#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// The crop fields the vanilla reference grew (tools/refscripts/gen_crops.py, E:\mc-reference\scripts\crops1.txt): six
// variants of crop in rows of 14 patches, random ticks at 100 a section, counted every 80 ticks. Logged as "mccrop row=R
// t=T ..." lines for tools/refscripts/cropcmp.py to put beside the "mcref box" lines.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCropFieldTest, "McClient.McSim.CropField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	struct FCropVariant
	{
		const TCHAR* Crop;
		int32 Moisture;
		TArray<FIntPoint> Crops;
		TArray<FIntPoint> Waters;
		TArray<FIntPoint> Farmland;
	};

	TArray<FCropVariant> MakeVariants()
	{
		const auto Box = [](int32 X0, int32 X1, int32 Z0, int32 Z1)
		{
			TArray<FIntPoint> Cells;
			for (int32 X = X0; X <= X1; ++X) for (int32 Z = Z0; Z <= Z1; ++Z) Cells.Add(FIntPoint(X, Z));
			return Cells;
		};
		TArray<FCropVariant> Variants;
		Variants.Add({ TEXT("wheat"), 0, { FIntPoint(3, 3) }, {}, Box(2, 4, 2, 4) });
		Variants.Add({ TEXT("wheat"), 7, { FIntPoint(3, 3) }, { FIntPoint(5, 3) }, Box(2, 4, 2, 4) });
		Variants.Add({ TEXT("wheat"), 7, Box(1, 5, 3, 3), { FIntPoint(3, 5) }, Box(0, 6, 2, 4) });
		TArray<FIntPoint> Checker;
		for (const int32 X : { 1, 3, 5 }) for (const int32 Z : { 1, 3, 5 }) Checker.Add(FIntPoint(X, Z));
		Variants.Add({ TEXT("wheat"), 7, Checker, { FIntPoint(0, 3), FIntPoint(6, 3) }, Box(0, 6, 0, 6) });
		Variants.Add({ TEXT("wheat"), 7, Box(1, 5, 1, 5), { FIntPoint(0, 3), FIntPoint(6, 3) }, Box(0, 6, 0, 6) });
		Variants.Add({ TEXT("melon_stem"), 7, { FIntPoint(3, 3) }, { FIntPoint(5, 3) }, Box(2, 4, 2, 4) });
		return Variants;
	}
}

bool FMcCropFieldTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	constexpr int32 Copies = 14, Spacing = 8;
	FMcBlockWorld World{FMcDimensionType()};
	TArray<FIntPoint> Chunks;
	for (int32 Z = -1; Z <= 5; ++Z) for (int32 X = -1; X <= 8; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); Chunks.Add(FIntPoint(X, Z)); }
	FMcSimulation Sim(World);
	Sim.SetTickingChunks(Chunks);
	Sim.SetRandomTickSpeed(100);
	Sim.SetWeatherCycle(false);
	Sim.SetWeather(EMcWeather::Clear, 1000000);
	Sim.SetDayTime(6000);
	const TArray<FCropVariant> Variants = MakeVariants();
	for (int32 X = -2; X <= 130; ++X) for (int32 Z = -2; Z <= 80; ++Z) World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
	// Ground y 0 (the reference's -61), crops at y 1.
	for (int32 Row = 0; Row < Variants.Num(); ++Row)
	{
		const FCropVariant& Var = Variants[Row];
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			const int32 OX = Copy * Spacing, OZ = 12 * Row;
			for (const FIntPoint& Cell : Var.Farmland) World.SetBlock(FIntVector(OX + Cell.X, 0, OZ + Cell.Y), State(FString::Printf(TEXT("minecraft:farmland[moisture=%d]"), Var.Moisture)));
			for (const FIntPoint& Cell : Var.Waters) World.SetBlock(FIntVector(OX + Cell.X, 0, OZ + Cell.Y), State(TEXT("minecraft:water")));
			for (const FIntPoint& Cell : Var.Crops) World.SetBlock(FIntVector(OX + Cell.X, 1, OZ + Cell.Y), State(FString::Printf(TEXT("minecraft:%s[age=0]"), Var.Crop)));
		}
	}
	for (int32 Tick = 0; Tick < 20; ++Tick) Sim.Tick();
	for (int32 Step = 1; Step <= 10; ++Step)
	{
		for (int32 Tick = 0; Tick < 80; ++Tick) Sim.Tick();
		for (int32 Row = 0; Row < Variants.Num(); ++Row)
		{
			TMap<FString, int32> Counts;
			for (int32 X = 0; X <= (Copies - 1) * Spacing + 6; ++X) for (int32 Z = 12 * Row; Z <= 12 * Row + 6; ++Z)
			{
				const FMcStateId Block = World.GetBlock(FIntVector(X, 1, Z));
				if (!Registry.IsAir(Block)) Counts.FindOrAdd(Registry.Describe(Block))++;
			}
			FString Out;
			for (const TPair<FString, int32>& Pair : Counts) Out += FString::Printf(TEXT("%s=%d;"), *Pair.Key, Pair.Value);
			UE_LOG(LogTemp, Display, TEXT("mccrop row=%d t=%d %s"), Row, Step * 80 + 20, *Out);
		}
	}
	return true;
}

#endif
