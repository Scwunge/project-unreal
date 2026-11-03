// Shulker box lids against vanilla's logged state (mc-reference shulker_log.txt): 0.1 a tick each way, the collision
// grown by half the progress, a player on top lifted 0.11 a tick, and what in front stops the box opening.

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McPhysics.h"
#include "McShulkerLids.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcShulkerLidsTest, "McClient.McEntity.ShulkerLids",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcShulkerLidsTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	const FIntVector Up(2, 4, 2);
	const FIntVector East(8, 4, 2);
	World.SetBlock(Up, State(TEXT("minecraft:shulker_box[facing=up]")));
	World.SetBlock(East, State(TEXT("minecraft:red_shulker_box[facing=east]")));
	FMcShulkerLids Lids(World);

	// Opening: the shape's top at 1.05, 1.1, ... 1.5 (vanilla's log), and a player standing on it rises with it.
	FMcBody Player;
	Player.Position = FVector(2.5, 5.0, 2.5);
	Lids.SetOpeners(Up, 1);
	for (int32 Tick = 1; Tick <= 10; ++Tick)
	{
		Lids.Tick();
		const FVector Push = Lids.GetPush(Player.GetBox());
		McPhysics::Move(World, Player, Push, false);
		const TArray<FBox3f>* Shape = World.FindCollisionOverride(Up);
		TestTrue(FString::Printf(TEXT("tick %d: grown by half the progress"), Tick), Shape && FMath::IsNearlyEqual((*Shape)[0].Max.Y, 1.0f + 0.05f * Tick, 1e-4f));
		TestTrue(FString::Printf(TEXT("tick %d: the player lifted 0.11 a tick"), Tick), FMath::IsNearlyEqual(Player.Position.Y, 5.0 + 0.11 * Tick, 1e-4));
	}
	Lids.Tick();
	TestTrue(TEXT("open, it stops"), FMath::IsNearlyEqual(Lids.GetProgress(Up), 1.0f) && FMath::IsNearlyEqual(Player.Position.Y, 6.1, 1e-4) && Lids.GetPush(Player.GetBox()).IsZero());
	TArray<FBox> Boxes;
	McPhysics::GatherBoxes(World, FBox(FVector(2.1, 5.2, 2.1), FVector(2.9, 5.4, 2.9)), Boxes);
	TestEqual(TEXT("the open shell stands in the half block above"), Boxes.Num(), 1);

	// Closing: back 0.1 a tick, pushing nothing, and the block's own shape once shut.
	Lids.SetOpeners(Up, 0);
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Lids.Tick();
		TestTrue(TEXT("closing pushes nothing"), Lids.GetPush(FBox(FVector(2.0, 5.0, 2.0), FVector(3.0, 6.0, 3.0))).IsZero());
	}
	TestTrue(TEXT("shut"), Lids.GetProgress(Up) == 0.0f && World.FindCollisionOverride(Up) == nullptr);

	// Facing east, a player in front is moved along x (vanilla: 22.5 to 23.38 over the opening, starting the second tick).
	FMcBody Front;
	Front.Position = FVector(East.X + 1.5, East.Y, East.Z + 0.5);
	Lids.SetOpeners(East, 2);
	for (int32 Tick = 0; Tick < 11; ++Tick)
	{
		Lids.Tick();
		McPhysics::Move(World, Front, Lids.GetPush(Front.GetBox()), false);
	}
	TestTrue(TEXT("pushed 0.88 east"), FMath::IsNearlyEqual(Front.Position.X, East.X + 2.38, 1e-4));
	const TArray<FBox3f>* EastShape = World.FindCollisionOverride(East);
	TestTrue(TEXT("grown east"), EastShape && FMath::IsNearlyEqual((*EastShape)[0].Max.X, 1.5f) && FMath::IsNearlyEqual((*EastShape)[0].Max.Y, 1.0f));
	TestFalse(TEXT("its own open shell does not stop a second opener"), FMcShulkerLids::IsBlocked(World, East));
	World.SetBlock(East, State(TEXT("minecraft:air")));
	TestTrue(TEXT("broken: its shell goes"), World.FindCollisionOverride(East) == nullptr);
	Lids.Tick();
	TestEqual(TEXT("and the lid is forgotten"), Lids.GetOpeners(East), 0);

	// A box already open when its chunk came starts open, and its shell is there after the next tick.
	World.SetBlock(East, State(TEXT("minecraft:shulker_box[facing=east]")));
	Lids.SetOpen(East, 1);
	TestTrue(TEXT("already open: up at once"), FMath::IsNearlyEqual(Lids.GetProgress(East), 1.0f) && Lids.GetOpeners(East) == 1);
	Lids.Tick();
	const TArray<FBox3f>* OpenShape = World.FindCollisionOverride(East);
	TestTrue(TEXT("already open: grown east"), OpenShape && FMath::IsNearlyEqual((*OpenShape)[0].Max.X, 1.5f));
	World.SetBlock(East, State(TEXT("minecraft:air")));
	Lids.Tick();

	// What above a box facing up stops it opening (vanilla's run).
	const FIntVector Above = Up + FIntVector(0, 1, 0);
	const TArray<TPair<const TCHAR*, bool>> Cases = {
		{ TEXT("minecraft:air"), false },
		{ TEXT("minecraft:stone"), true },
		{ TEXT("minecraft:stone_slab[type=bottom]"), true },
		{ TEXT("minecraft:stone_slab[type=top]"), false },
		{ TEXT("minecraft:white_carpet"), true },
		{ TEXT("minecraft:torch"), false },
		{ TEXT("minecraft:chest"), true },
	};
	for (const TPair<const TCHAR*, bool>& Case : Cases)
	{
		World.SetBlock(Above, State(Case.Key));
		TestEqual(FString::Printf(TEXT("%s above"), Case.Key), FMcShulkerLids::IsBlocked(World, Up), Case.Value);
	}
	return true;
}

#endif
