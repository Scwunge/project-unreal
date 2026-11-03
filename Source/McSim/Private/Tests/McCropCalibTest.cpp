#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// The crop calibration the vanilla reference ran (tools/refscripts/gen_crops2.py, E:\mc-reference\scripts\crops2.txt): a
// wheat plant over six shapes of farmland (dry or hydrated), 21 patches in each of two rows per shape, random ticks at 100,
// counted every 80 ticks for 1600. Logged as "mccalib row=R t=T ..." for tools/refscripts/cropcmp2.py.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCropCalibTest, "McClient.McSim.CropCalib",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcCropCalibTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	const auto Box = [](int32 X0, int32 X1, int32 Z0, int32 Z1)
	{
		TArray<FIntPoint> Cells;
		for (int32 X = X0; X <= X1; ++X) for (int32 Z = Z0; Z <= Z1; ++Z) Cells.Add(FIntPoint(X, Z));
		return Cells;
	};
	struct FShape { int32 Moisture; TArray<FIntPoint> Cells; };
	const TArray<FShape> Shapes = {
		{ 0, { FIntPoint(3, 3) } }, { 0, Box(2, 4, 2, 4) }, { 7, { FIntPoint(3, 3) } },
		{ 7, { FIntPoint(2, 3), FIntPoint(3, 3), FIntPoint(4, 3) } },
		{ 7, { FIntPoint(3, 3), FIntPoint(2, 3), FIntPoint(4, 3), FIntPoint(3, 2), FIntPoint(3, 4) } }, { 7, Box(2, 4, 2, 4) },
	};
	constexpr int32 Copies = 21, Spacing = 6, Reps = 2, RowH = 8;
	const int32 NumRows = Shapes.Num() * Reps;
	FMcBlockWorld World{FMcDimensionType()};
	TArray<FIntPoint> Chunks;
	for (int32 Z = -1; Z <= 7; ++Z) for (int32 X = -1; X <= 8; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); Chunks.Add(FIntPoint(X, Z)); }
	FMcSimulation Sim(World);
	Sim.SetTickingChunks(Chunks);
	Sim.SetRandomTickSpeed(100);
	Sim.SetWeatherCycle(false);
	Sim.SetWeather(EMcWeather::Clear, 1000000);
	Sim.SetDayTime(6000);
	for (int32 X = -2; X <= 130; ++X) for (int32 Z = -2; Z <= RowH * NumRows + 4; ++Z) World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
	for (int32 Row = 0; Row < NumRows; ++Row)
	{
		const FShape& Shape = Shapes[Row / Reps];
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			const int32 OX = Copy * Spacing, OZ = Row * RowH;
			for (const FIntPoint& Cell : Shape.Cells) World.SetBlock(FIntVector(OX + Cell.X, 0, OZ + Cell.Y), State(FString::Printf(TEXT("minecraft:farmland[moisture=%d]"), Shape.Moisture)));
			if (Shape.Moisture > 0) World.SetBlock(FIntVector(OX + 6, 0, OZ + 3), State(TEXT("minecraft:water")));
			World.SetBlock(FIntVector(OX + 3, 1, OZ + 3), State(TEXT("minecraft:wheat[age=0]")));
		}
	}
	for (int32 Tick = 0; Tick < 20; ++Tick) Sim.Tick();
	for (int32 Step = 1; Step <= 20; ++Step)
	{
		for (int32 Tick = 0; Tick < 80; ++Tick) Sim.Tick();
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			int32 Sum = 0, Plants = 0;
			for (int32 X = 0; X <= (Copies - 1) * Spacing + 6; ++X) for (int32 Z = RowH * Row; Z <= RowH * Row + 6; ++Z)
			{
				const FMcStateId Block = World.GetBlock(FIntVector(X, 1, Z));
				if (!Registry.IsAir(Block) && Registry.GetBlockOf(Block).Name == TEXT("minecraft:wheat"))
				{
					++Plants;
					Sum += FCString::Atoi(*Registry.GetPropertyValue(Block, TEXT("age")).ToString());
				}
			}
			UE_LOG(LogTemp, Display, TEXT("mccalib row=%d t=%d plants=%d sum=%d"), Row, Step * 80 + 20, Plants, Sum);
		}
	}
	return true;
}

#endif
