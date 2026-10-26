#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McRaycast.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRaycastTest, "McClient.McEntity.Raycast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcRaycastTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()) || !TestTrue(TEXT("shapes load"), FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));

	FMcBlockWorld World{FMcDimensionType()};
	for (int32 CZ = -1; CZ <= 0; ++CZ)
	{
		for (int32 CX = -1; CX <= 0; ++CX)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(CX, CZ)));
		}
	}
	World.SetBlock(FIntVector(3, 0, 0), Stone);
	World.SetBlock(FIntVector(0, -1, 0), Stone);
	World.SetBlock(FIntVector(-3, 0, -3), State(TEXT("minecraft:stone_slab[type=bottom]")));
	World.SetBlock(FIntVector(0, 0, -3), State(TEXT("minecraft:water[level=0]")));
	World.SetBlock(FIntVector(0, 0, -5), Stone);
	World.SetBlock(FIntVector(-5, 0, 0), State(TEXT("minecraft:short_grass")));

	// Looking east at a stone block hits its west face.
	TOptional<FMcBlockHit> Hit = McRaycast::Trace(World, FVector(0.5, 0.5, 0.5), FVector(1, 0, 0), 5.0);
	if (TestTrue(TEXT("hits the stone to the east"), Hit.IsSet()))
	{
		TestEqual(TEXT("the right block"), Hit->Block, FIntVector(3, 0, 0));
		TestEqual(TEXT("through its west face"), Hit->Face, 4);
		TestEqual(TEXT("at x = 3"), Hit->Point.X, 3.0, 1e-9);
		TestEqual(TEXT("placing goes beside it"), Hit->GetAdjacent(), FIntVector(2, 0, 0));
	}
	TestFalse(TEXT("out of reach"), McRaycast::Trace(World, FVector(-1.5, 0.5, 0.5), FVector(1, 0, 0), 4.0).IsSet());

	// Looking down hits the top face.
	Hit = McRaycast::Trace(World, FVector(0.5, 1.62, 0.5), FVector(0, -1, 0), 5.0);
	if (TestTrue(TEXT("hits the floor"), Hit.IsSet()))
	{
		TestEqual(TEXT("the floor block"), Hit->Block, FIntVector(0, -1, 0));
		TestEqual(TEXT("its top"), Hit->Face, 1);
	}

	// A slab's outline is its lower half: a ray over it misses, one into it hits at y = 0.5.
	TestFalse(TEXT("passes over a bottom slab"), McRaycast::Trace(World, FVector(-0.5, 0.75, -2.5), FVector(-1, 0, 0), 5.0).IsSet());
	Hit = McRaycast::Trace(World, FVector(-2.5, 2.0, -2.5), FVector(0, -1, 0), 5.0);
	if (TestTrue(TEXT("hits a slab from above"), Hit.IsSet()))
	{
		TestEqual(TEXT("on its top, half way up"), Hit->Point.Y, 0.5, 1e-9);
	}

	// Rays go through water to the block behind it, and stop at grass.
	Hit = McRaycast::Trace(World, FVector(0.5, 0.5, -1.5), FVector(0, 0, -1), 5.0);
	TestTrue(TEXT("sees through water"), Hit.IsSet() && Hit->Block == FIntVector(0, 0, -5));
	Hit = McRaycast::Trace(World, FVector(-1.5, 0.5, 0.5), FVector(-1, 0, 0), 5.0);
	TestTrue(TEXT("stops at grass"), Hit.IsSet() && Hit->Block == FIntVector(-5, 0, 0));

	// A diagonal ray finds the stone too.
	Hit = McRaycast::Trace(World, FVector(2.5, 1.5, 0.5), FVector(1, -0.5, 0), 5.0);
	TestTrue(TEXT("diagonal ray"), Hit.IsSet() && Hit->Block == FIntVector(3, 0, 0) && Hit->Face == 1);

	TestTrue(TEXT("grass is replaceable"), McRaycast::IsReplaceable(State(TEXT("minecraft:short_grass"))));
	TestFalse(TEXT("stone isn't"), McRaycast::IsReplaceable(Stone));
	return true;
}

#endif
