#include "McBlockEntity.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTrappedChestTest, "McClient.McSim.TrappedChest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// Reference run trapchest (tools/refscripts/trapchest.txt): with one player at it, an open trapped chest powers dust beside it to
// 1 and lights a lamp on any side; on a stone block it powers the block (a lamp beside the block lights, and dust beside it takes 1);
// both halves of a double chest power the same; a plain chest powers nothing; a comparator reads the contents (9 full stacks: 5),
// open or not. The count for several players is the wiki's (one for each, to 15) and is not in the log.
bool FMcTrappedChestTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 2; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	for (int32 X = -8; X <= 40; ++X)
	{
		for (int32 Z = -8; Z <= 20; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
		}
	}
	FMcSimulation Sim(World);
	const auto Run = [&](int32 Ticks)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Sim.Tick();
		}
	};
	const auto Lit = [&](const FIntVector& Pos) { return Registry.GetPropertyValue(World.GetBlock(Pos), TEXT("lit")) == TEXT("true"); };

	// A: chest, dust beside it, lamps on both sides
	World.SetBlock(FIntVector(3, 1, 6), State(TEXT("minecraft:trapped_chest[facing=south]")));
	World.SetBlock(FIntVector(4, 1, 6), State(TEXT("minecraft:redstone_wire")));
	World.SetBlock(FIntVector(5, 1, 6), State(TEXT("minecraft:redstone_lamp")));
	World.SetBlock(FIntVector(2, 1, 6), State(TEXT("minecraft:redstone_lamp")));
	// B: chest on stone, dust and lamps beside the stone
	World.SetBlock(FIntVector(10, 1, 6), State(TEXT("minecraft:stone")));
	World.SetBlock(FIntVector(10, 2, 6), State(TEXT("minecraft:trapped_chest[facing=south]")));
	World.SetBlock(FIntVector(11, 1, 6), State(TEXT("minecraft:redstone_wire")));
	World.SetBlock(FIntVector(12, 1, 6), State(TEXT("minecraft:redstone_lamp")));
	World.SetBlock(FIntVector(9, 1, 6), State(TEXT("minecraft:redstone_lamp")));
	// D: a double chest with dust and a lamp at each end
	World.SetBlock(FIntVector(22, 1, 12), State(TEXT("minecraft:trapped_chest[facing=south,type=right]")));
	World.SetBlock(FIntVector(23, 1, 12), State(TEXT("minecraft:trapped_chest[facing=south,type=left]")));
	World.SetBlock(FIntVector(21, 1, 12), State(TEXT("minecraft:redstone_wire")));
	World.SetBlock(FIntVector(24, 1, 12), State(TEXT("minecraft:redstone_wire")));
	World.SetBlock(FIntVector(20, 1, 12), State(TEXT("minecraft:redstone_lamp")));
	World.SetBlock(FIntVector(25, 1, 12), State(TEXT("minecraft:redstone_lamp")));
	// E: a plain chest
	World.SetBlock(FIntVector(28, 1, 6), State(TEXT("minecraft:chest[facing=south]")));
	World.SetBlock(FIntVector(29, 1, 6), State(TEXT("minecraft:redstone_wire")));
	Run(10);
	TestEqual(TEXT("closed: dust 0"), Sim.GetDustPower(FIntVector(4, 1, 6)), 0);
	TestFalse(TEXT("closed: lamp dark"), Lit(FIntVector(5, 1, 6)));

	Sim.SetChestViewers(FIntVector(3, 1, 6), 1);
	Sim.SetChestViewers(FIntVector(10, 2, 6), 1);
	Sim.SetChestViewers(FIntVector(22, 1, 12), 1);
	Sim.SetChestViewers(FIntVector(28, 1, 6), 1);
	Run(10);
	TestEqual(TEXT("open: dust beside it 1"), Sim.GetDustPower(FIntVector(4, 1, 6)), 1);
	TestTrue(TEXT("open: lamp beyond the dust lit"), Lit(FIntVector(5, 1, 6)));
	TestTrue(TEXT("open: lamp on the other side lit"), Lit(FIntVector(2, 1, 6)));
	TestEqual(TEXT("on stone: dust beside the stone 1"), Sim.GetDustPower(FIntVector(11, 1, 6)), 1);
	TestTrue(TEXT("on stone: lamp beside the stone lit"), Lit(FIntVector(9, 1, 6)));
	TestFalse(TEXT("on stone: lamp beyond the dust dark"), Lit(FIntVector(12, 1, 6)));
	TestEqual(TEXT("double, left end 1"), Sim.GetDustPower(FIntVector(21, 1, 12)), 1);
	TestEqual(TEXT("double, right end 1"), Sim.GetDustPower(FIntVector(24, 1, 12)), 1);
	TestTrue(TEXT("double: both lamps lit"), Lit(FIntVector(20, 1, 12)) && Lit(FIntVector(25, 1, 12)));
	TestEqual(TEXT("a plain chest gives nothing"), Sim.GetDustPower(FIntVector(29, 1, 6)), 0);

	// Two players: two; the count tops out at 15.
	Sim.SetChestViewers(FIntVector(3, 1, 6), 2);
	Run(4);
	TestEqual(TEXT("two viewers: 2"), Sim.GetDustPower(FIntVector(4, 1, 6)), 2);
	Sim.SetChestViewers(FIntVector(3, 1, 6), 40);
	Run(4);
	TestEqual(TEXT("many viewers: 15"), Sim.GetDustPower(FIntVector(4, 1, 6)), 15);

	Sim.SetChestViewers(FIntVector(3, 1, 6), 0);
	Sim.SetChestViewers(FIntVector(10, 2, 6), 0);
	Sim.SetChestViewers(FIntVector(23, 1, 12), 0);
	Run(10);
	TestEqual(TEXT("closed again: dust 0"), Sim.GetDustPower(FIntVector(4, 1, 6)), 0);
	TestFalse(TEXT("closed again: lamp dark"), Lit(FIntVector(5, 1, 6)) || Lit(FIntVector(2, 1, 6)) || Lit(FIntVector(9, 1, 6)));
	TestEqual(TEXT("double closed from either half"), Sim.GetDustPower(FIntVector(21, 1, 12)) + Sim.GetDustPower(FIntVector(24, 1, 12)), 0);

	// A comparator reads the contents of a trapped chest like any chest: 9 full stacks of 27 slots is 5.
	World.SetBlock(FIntVector(16, 1, 6), State(TEXT("minecraft:trapped_chest[facing=south]")));
	FMcBlockEntity Chest = FMcBlockEntity::MakeContainer(27);
	for (int32 Slot = 0; Slot < 9; ++Slot)
	{
		Chest.Slots[Slot].Item = TEXT("minecraft:stone");
		Chest.Slots[Slot].Count = 64;
	}
	World.FindChunk(FIntPoint(1, 0))->SetBlockEntity(0, 1, 6, Chest);
	World.SetBlock(FIntVector(16, 1, 7), State(TEXT("minecraft:comparator[facing=north,mode=compare]")));
	World.SetBlock(FIntVector(16, 1, 8), State(TEXT("minecraft:redstone_wire")));
	Run(6);
	TestEqual(TEXT("a trapped chest of 9 full stacks reads 5"), Sim.GetDustPower(FIntVector(16, 1, 8)), 5);
	return true;
}

#endif
