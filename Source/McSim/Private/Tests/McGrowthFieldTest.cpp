#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// The same field the vanilla reference grew (E:\mc-reference\scripts\growth_wheat.txt, growth_melon.txt), run for as
// long, with its stage counts logged every 200 ticks as "mcgrowth ..." lines to compare with the "mcref box" lines.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGrowthFieldTest, "McClient.McSim.GrowthField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGrowthFieldTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	const auto Fill = [&](FMcBlockWorld& World, FIntVector A, FIntVector B, const FString& Text)
	{
		for (int32 X = A.X; X <= B.X; ++X) for (int32 Y = A.Y; Y <= B.Y; ++Y) for (int32 Z = A.Z; Z <= B.Z; ++Z) World.SetBlock(FIntVector(X, Y, Z), State(Text));
	};
	const auto MakeWorld = [&](FMcBlockWorld& World)
	{
		TArray<FIntPoint> Chunks;
		for (int32 Z = -1; Z <= 2; ++Z) for (int32 X = -1; X <= 2; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); Chunks.Add(FIntPoint(X, Z)); }
		return Chunks;
	};
	// Ground y 0 (the reference's -61), crops at y 1, light blocks stand in as glowstone at y 3.
	{
		FMcBlockWorld World{FMcDimensionType()};
		FMcSimulation Sim(World);
		Sim.SetTickingChunks(MakeWorld(World));
		Fill(World, FIntVector(0, 0, 0), FIntVector(16, 0, 8), TEXT("minecraft:farmland[moisture=7]"));
		Fill(World, FIntVector(0, 0, 4), FIntVector(16, 0, 4), TEXT("minecraft:water[level=0]"));
		Fill(World, FIntVector(0, 3, 0), FIntVector(16, 3, 8), TEXT("minecraft:glowstone"));
		for (const int32 Z : { 0, 2, 6, 8 }) Fill(World, FIntVector(0, 1, Z), FIntVector(16, 1, Z), TEXT("minecraft:wheat[age=0]"));
		for (int32 Tick = 0; Tick <= 72000; ++Tick)
		{
			if (Tick % 200 == 0)
			{
				int32 Ages[8] = {};
				for (int32 X = 0; X <= 16; ++X) for (const int32 Z : { 0, 2, 6, 8 }) Ages[FCString::Atoi(*Registry.GetPropertyValue(World.GetBlock(FIntVector(X, 1, Z)), TEXT("age")).ToString())]++;
				UE_LOG(LogTemp, Display, TEXT("mcgrowth wheat t=%d ages=%d,%d,%d,%d,%d,%d,%d,%d"), Tick, Ages[0], Ages[1], Ages[2], Ages[3], Ages[4], Ages[5], Ages[6], Ages[7]);
			}
			Sim.Tick();
		}
	}
	{
		// 16 by 16 stems, three apart, with water between, sampled every 500 ticks (growth_melon256.txt).
		FMcBlockWorld World{FMcDimensionType()};
		FMcSimulation Sim(World);
		Sim.SetTickingChunks(MakeWorld(World));
		const int32 Span = 48;
		Fill(World, FIntVector(0, 0, 0), FIntVector(Span - 2, 0, Span - 2), TEXT("minecraft:farmland[moisture=7]"));
		Fill(World, FIntVector(0, 3, 0), FIntVector(Span - 2, 3, Span - 2), TEXT("minecraft:glowstone"));
		for (int32 I = 0; I < 16; ++I) for (int32 J = 0; J < 16; ++J)
		{
			World.SetBlock(FIntVector(3 * I + 2, 0, 3 * J + 2), State(TEXT("minecraft:water[level=0]")));
			World.SetBlock(FIntVector(3 * I, 1, 3 * J), State(TEXT("minecraft:melon_stem[age=7]")));
		}
		for (int32 Tick = 0; Tick <= 20000; ++Tick)
		{
			if (Tick % 500 == 0)
			{
				int32 Attached = 0;
				for (int32 X = 0; X <= Span; ++X) for (int32 Z = 0; Z <= Span; ++Z)
				{
					Attached += Registry.GetBlockOf(World.GetBlock(FIntVector(X, 1, Z))).Name.ToString() == TEXT("minecraft:attached_melon_stem");
				}
				UE_LOG(LogTemp, Display, TEXT("mcgrowth melon256 t=%d attached=%d"), Tick, Attached);
			}
			Sim.Tick();
		}
	}
	return true;
}

#endif
