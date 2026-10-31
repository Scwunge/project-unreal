#include "McBlockEntity.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "McFurnace.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRedstoneTest, "McClient.McSim.Redstone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcRedstoneTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Air = State(TEXT("minecraft:air"));

	// A stone floor at y 0 under 3x3 chunks of air.
	const auto MakeWorld = [&](FMcBlockWorld& World)
	{
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		for (int32 X = -20; X <= 20; ++X)
		{
			for (int32 Z = -20; Z <= 20; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Stone);
			}
		}
	};
	const auto Run = [](FMcSimulation& Sim, int32 Ticks)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Sim.Tick();
		}
	};
	const auto Lever = [&](FMcBlockWorld& World, const FIntVector& Pos, bool bOn)
	{
		World.SetBlock(Pos, State(bOn ? TEXT("minecraft:lever[face=floor,facing=north,powered=true]") : TEXT("minecraft:lever[face=floor,facing=north,powered=false]")));
	};

	{
		// Dust loses 1 a block from a lever, and drops to nothing the moment it goes off.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		for (int32 X = 1; X <= 17; ++X)
		{
			World.SetBlock(FIntVector(X, 1, 0), State(TEXT("minecraft:redstone_wire")));
		}
		Lever(World, FIntVector(0, 1, 0), true);
		TestEqual(TEXT("next to the lever, 15"), Sim.GetDustPower(FIntVector(1, 1, 0)), 15);
		TestEqual(TEXT("five out, 11"), Sim.GetDustPower(FIntVector(5, 1, 0)), 11);
		TestEqual(TEXT("fifteen out, 1"), Sim.GetDustPower(FIntVector(15, 1, 0)), 1);
		TestEqual(TEXT("sixteen out, 0"), Sim.GetDustPower(FIntVector(16, 1, 0)), 0);
		TestEqual(TEXT("a line of dust is drawn through"), Registry.GetPropertyValue(World.GetBlock(FIntVector(5, 1, 0)), TEXT("east")).ToString(), FString(TEXT("side")));
		Lever(World, FIntVector(0, 1, 0), false);
		TestEqual(TEXT("off at the lever, off along the dust"), Sim.GetDustPower(FIntVector(1, 1, 0)) + Sim.GetDustPower(FIntVector(8, 1, 0)), 0);
	}
	{
		// A ring of dust holds no power of its own once the lever lets go.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		for (int32 X = 5; X <= 8; ++X)
		{
			for (int32 Z = 5; Z <= 8; ++Z)
			{
				if (X == 5 || X == 8 || Z == 5 || Z == 8)
				{
					World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:redstone_wire")));
				}
			}
		}
		Lever(World, FIntVector(4, 1, 5), true);
		TestEqual(TEXT("the ring is fed"), Sim.GetDustPower(FIntVector(5, 1, 5)), 15);
		TestTrue(TEXT("and fades round it"), Sim.GetDustPower(FIntVector(8, 1, 8)) > 0 && Sim.GetDustPower(FIntVector(8, 1, 8)) < 15);
		Lever(World, FIntVector(4, 1, 5), false);
		int32 Total = 0;
		for (int32 X = 5; X <= 8; ++X)
		{
			for (int32 Z = 5; Z <= 8; ++Z)
			{
				Total += Sim.GetDustPower(FIntVector(X, 1, Z));
			}
		}
		TestEqual(TEXT("no phantom power"), Total, 0);
	}
	{
		// Dust climbs a block and goes back down, a step weaker each time.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(1, 1, 0), Stone);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(1, 2, 0), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:redstone_wire")));
		Lever(World, FIntVector(-1, 1, 0), true);
		TestEqual(TEXT("at the foot"), Sim.GetDustPower(FIntVector(0, 1, 0)), 15);
		TestEqual(TEXT("on top"), Sim.GetDustPower(FIntVector(1, 2, 0)), 14);
		TestEqual(TEXT("down the other side"), Sim.GetDustPower(FIntVector(2, 1, 0)), 13);
		TestEqual(TEXT("up is a connection"), Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 1, 0)), TEXT("east")).ToString(), FString(TEXT("up")));
	}
	{
		// A repeater passes a signal on after 1 to 4 redstone ticks (2 game ticks each).
		for (int32 Delay = 1; Delay <= 4; ++Delay)
		{
			FMcBlockWorld World{FMcDimensionType()};
			MakeWorld(World);
			FMcSimulation Sim(World);
			World.SetBlock(FIntVector(1, 1, 0), State(*FString::Printf(TEXT("minecraft:repeater[delay=%d,facing=west]"), Delay)));
			World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:redstone_wire")));
			Lever(World, FIntVector(0, 1, 0), true);
			Run(Sim, Delay * 2 - 1);
			TestEqual(*FString::Printf(TEXT("delay %d: not yet"), Delay), Sim.GetDustPower(FIntVector(2, 1, 0)), 0);
			Run(Sim, 1);
			TestEqual(*FString::Printf(TEXT("delay %d: on at %d ticks"), Delay, Delay * 2), Sim.GetDustPower(FIntVector(2, 1, 0)), 15);
			Lever(World, FIntVector(0, 1, 0), false);
			Run(Sim, Delay * 2 - 1);
			TestEqual(*FString::Printf(TEXT("delay %d: still on"), Delay), Sim.GetDustPower(FIntVector(2, 1, 0)), 15);
			Run(Sim, 1);
			TestEqual(*FString::Printf(TEXT("delay %d: off again"), Delay), Sim.GetDustPower(FIntVector(2, 1, 0)), 0);
		}
	}
	{
		// A repeater pointed into the side of another holds it.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		// The locker, south of it and pointing north into its side, fed by a lever.
		World.SetBlock(FIntVector(1, 1, 1), State(TEXT("minecraft:repeater[delay=1,facing=south]")));
		World.SetBlock(FIntVector(1, 1, 2), State(TEXT("minecraft:redstone_block")));
		Run(Sim, 4);
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:repeater[delay=1,facing=west,powered=true,locked=false]")));
		Run(Sim, 4);
		TestEqual(TEXT("it is locked"), Registry.GetPropertyValue(World.GetBlock(FIntVector(1, 1, 0)), TEXT("locked")).ToString(), FString(TEXT("true")));
		TestEqual(TEXT("and stays on without an input"), Registry.GetPropertyValue(World.GetBlock(FIntVector(1, 1, 0)), TEXT("powered")).ToString(), FString(TEXT("true")));
	}
	{
		// A torch inverts: on until the block it is on is powered, off two ticks after, on again two after that.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(1, 1, 0), Stone);
		World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:redstone_wall_torch[facing=east,lit=true]")));
		const auto Lit = [&] { return Registry.GetPropertyValue(World.GetBlock(FIntVector(2, 1, 0)), TEXT("lit")).ToString() == TEXT("true"); };
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:lever[face=wall,facing=west,powered=true]")));
		Run(Sim, 1);
		TestTrue(TEXT("torch still lit after 1 tick"), Lit());
		Run(Sim, 1);
		TestFalse(TEXT("torch off after 2"), Lit());
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:lever[face=wall,facing=west,powered=false]")));
		Run(Sim, 2);
		TestTrue(TEXT("torch back on after 2 more"), Lit());

		// Switched on and off every 2 ticks it burns out and stays dark until it has rested.
		for (int32 Index = 0; Index < 24; ++Index)
		{
			World.SetBlock(FIntVector(0, 1, 0), State(Index % 2 == 0 ? TEXT("minecraft:lever[face=wall,facing=west,powered=true]") : TEXT("minecraft:lever[face=wall,facing=west,powered=false]")));
			Run(Sim, 2);
		}
		Run(Sim, 4);
		TestFalse(TEXT("a torch flicked too often burns out"), Lit());
		Run(Sim, 400);
		TestTrue(TEXT("and comes back"), Lit());
	}
	{
		// A torch under dust feeds it; a torch on a block over a powered block goes dark.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:redstone_torch[lit=true]")));
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:redstone_wire")));
		TestEqual(TEXT("a lit torch powers dust beside it"), Sim.GetDustPower(FIntVector(1, 1, 0)), 15);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:redstone_block")));
		TestEqual(TEXT("a redstone block does too"), Sim.GetDustPower(FIntVector(1, 1, 0)), 15);
		World.SetBlock(FIntVector(0, 1, 0), Air);
		TestEqual(TEXT("and takes it away"), Sim.GetDustPower(FIntVector(1, 1, 0)), 0);
	}
	{
		// A comparator reads a container and gives its fullness; a bigger signal on its side cuts it off.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:chest")));
		FMcBlockEntity Chest = FMcBlockEntity::MakeContainer(27);
		for (int32 Slot = 0; Slot < 14; ++Slot)
		{
			Chest.Slots[Slot].Item = TEXT("minecraft:stone");
			Chest.Slots[Slot].Count = 64;
		}
		World.FindChunk(FIntPoint(0, 0))->SetBlockEntity(1, 1, 0, Chest);
		World.SetBlock(FIntVector(-1, 1, 0), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:comparator[facing=east,mode=compare]")));
		Run(Sim, 4);
		TestEqual(TEXT("a half full chest gives 8"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 8 + 0);
		World.SetBlock(FIntVector(0, 1, -1), State(TEXT("minecraft:redstone_block")));
		Run(Sim, 4);
		TestEqual(TEXT("a stronger side signal shuts it off"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 0);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:comparator[facing=east,mode=subtract]")));
		World.SetBlock(FIntVector(0, 1, -1), Air);
		World.SetBlock(FIntVector(0, 1, 1), State(TEXT("minecraft:redstone_block")));
		Run(Sim, 4);
		TestEqual(TEXT("subtract mode gives nothing when the side beats it"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 0);
	}
	{
		// A comparator also reads blocks by their state: a cake by its bites, a composter by its level, a double chest by
		// both halves together.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(-1, 1, 0), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:comparator[facing=east,mode=compare]")));
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:cake[bites=2]")));
		Run(Sim, 4);
		TestEqual(TEXT("a cake with 2 bites gone gives 10"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 10);
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:composter[level=5]")));
		Run(Sim, 4);
		TestEqual(TEXT("a composter gives its level"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 5);
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:end_portal_frame[eye=true,facing=north]")));
		Run(Sim, 4);
		TestEqual(TEXT("a portal frame with an eye gives 15"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 15);
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:chest[facing=north,type=right]")));
		World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:chest[facing=north,type=left]")));
		FMcBlockEntity Full = FMcBlockEntity::MakeContainer(27);
		for (FMcItemStack& Slot : Full.Slots)
		{
			Slot.Item = TEXT("minecraft:stone");
			Slot.Count = 64;
		}
		World.FindChunk(FIntPoint(0, 0))->SetBlockEntity(1, 1, 0, Full);
		Run(Sim, 4);
		TestEqual(TEXT("one full half of a double chest is half full: 8"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 8);
		// A furnace is read from what it holds: one full slot of three is a third, 5.
		World.SetBlock(FIntVector(2, 1, 0), Air);
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:furnace[facing=north,lit=false]")));
		FMcBlockEntity Oven;
		Oven.Kind = EMcBlockEntityKind::Furnace;
		FMcFurnace Machine;
		FMcItemStack Ore(TEXT("minecraft:iron_ore"));
		Ore.Count = 64;
		Machine.GetItems()->Set(FMcFurnace::InputSlot, Ore);
		FMemoryWriter Writer(Oven.Data);
		Machine.Serialize(Writer);
		World.FindChunk(FIntPoint(0, 0))->SetBlockEntity(1, 1, 0, Oven);
		Run(Sim, 4);
		TestEqual(TEXT("a furnace with a full stack in one of its three slots gives 5"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 5);
	}
	{
		// An observer pulses for 2 ticks, 2 ticks after the block it faces changes, and lights a lamp.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:observer[facing=east,powered=false]")));
		World.SetBlock(FIntVector(-1, 1, 0), State(TEXT("minecraft:redstone_lamp[lit=false]")));
		// A new observer pulses once by itself (reference run rs_observer); let that go by.
		Run(Sim, 12);
		const auto Powered = [&] { return Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 1, 0)), TEXT("powered")).ToString() == TEXT("true"); };
		const auto Lit = [&] { return Registry.GetPropertyValue(World.GetBlock(FIntVector(-1, 1, 0)), TEXT("lit")).ToString() == TEXT("true"); };
		TestFalse(TEXT("quiet"), Powered());
		World.SetBlock(FIntVector(1, 1, 0), Stone);
		Run(Sim, 1);
		TestFalse(TEXT("not yet"), Powered());
		Run(Sim, 1);
		TestTrue(TEXT("pulses after 2"), Powered());
		Run(Sim, 1);
		TestTrue(TEXT("the lamp lights"), Lit());
		Run(Sim, 1);
		TestFalse(TEXT("the pulse ends after 2 more"), Powered());
		Run(Sim, 5);
		TestFalse(TEXT("and the lamp goes out 4 ticks after that"), Lit());
	}
	{
		// A door opens when dust powers it, and shuts again.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(3, 1, 0), State(TEXT("minecraft:oak_door[facing=north,half=lower,hinge=left,open=false,powered=false]")));
		World.SetBlock(FIntVector(3, 2, 0), State(TEXT("minecraft:oak_door[facing=north,half=upper,hinge=left,open=false,powered=false]")));
		const auto Open = [&](int32 Y) { return Registry.GetPropertyValue(World.GetBlock(FIntVector(3, Y, 0)), TEXT("open")).ToString() == TEXT("true"); };
		Lever(World, FIntVector(0, 1, 0), true);
		Run(Sim, 2);
		TestTrue(TEXT("both halves open"), Open(1) && Open(2));
		Lever(World, FIntVector(0, 1, 0), false);
		Run(Sim, 2);
		TestFalse(TEXT("and close"), Open(1) || Open(2));
		// A trapdoor and a gate follow a redstone block next to them.
		World.SetBlock(FIntVector(8, 1, 0), State(TEXT("minecraft:oak_trapdoor[facing=north,half=bottom,open=false,powered=false]")));
		World.SetBlock(FIntVector(10, 1, 0), State(TEXT("minecraft:oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]")));
		World.SetBlock(FIntVector(9, 1, 0), State(TEXT("minecraft:redstone_block")));
		Run(Sim, 2);
		TestEqual(TEXT("the trapdoor opens"), Registry.GetPropertyValue(World.GetBlock(FIntVector(8, 1, 0)), TEXT("open")).ToString(), FString(TEXT("true")));
		TestEqual(TEXT("the gate opens"), Registry.GetPropertyValue(World.GetBlock(FIntVector(10, 1, 0)), TEXT("open")).ToString(), FString(TEXT("true")));
	}
	{
		// A button gives a pulse of 30 ticks (wood) or 20 (stone).
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:stone_button[face=floor,facing=north,powered=false]")));
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(0, 1, 2), State(TEXT("minecraft:oak_button[face=floor,facing=north,powered=false]")));
		World.SetBlock(FIntVector(1, 1, 2), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:stone_button[face=floor,facing=north,powered=true]")));
		World.SetBlock(FIntVector(0, 1, 2), State(TEXT("minecraft:oak_button[face=floor,facing=north,powered=true]")));
		TestEqual(TEXT("pressed"), Sim.GetDustPower(FIntVector(1, 1, 0)), 15);
		Run(Sim, 19);
		TestEqual(TEXT("stone still on at 19"), Sim.GetDustPower(FIntVector(1, 1, 0)), 15);
		Run(Sim, 1);
		TestEqual(TEXT("stone off at 20"), Sim.GetDustPower(FIntVector(1, 1, 0)), 0);
		TestEqual(TEXT("wood still on at 20"), Sim.GetDustPower(FIntVector(1, 1, 2)), 15);
		Run(Sim, 10);
		TestEqual(TEXT("wood off at 30"), Sim.GetDustPower(FIntVector(1, 1, 2)), 0);
	}
	{
		// A piston pushes 12 blocks and no more.
		for (int32 Count = 12; Count <= 13; ++Count)
		{
			FMcBlockWorld World{FMcDimensionType()};
			MakeWorld(World);
			FMcSimulation Sim(World);
			const FMcStateId Cobble = State(TEXT("minecraft:cobblestone"));
			World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:piston[extended=false,facing=east]")));
			for (int32 X = 1; X <= Count; ++X)
			{
				World.SetBlock(FIntVector(X, 1, 0), Cobble);
			}
			World.SetBlock(FIntVector(0, 1, -1), State(TEXT("minecraft:redstone_block")));
			Run(Sim, 5);
			const auto Name = [&](int32 X) { return Registry.GetBlockOf(World.GetBlock(FIntVector(X, 1, 0))).Name.ToString(); };
			if (Count == 12)
			{
				TestEqual(TEXT("the head comes out"), Name(1), FString(TEXT("minecraft:piston_head")));
				TestEqual(TEXT("the first block moves one on"), Name(2), FString(TEXT("minecraft:cobblestone")));
				TestEqual(TEXT("so does the twelfth"), Name(13), FString(TEXT("minecraft:cobblestone")));
				TestEqual(TEXT("the piston is extended"), Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 1, 0)), TEXT("extended")).ToString(), FString(TEXT("true")));
				// Powered off, it draws back (a plain piston leaves the blocks).
				World.SetBlock(FIntVector(0, 1, -1), Air);
				Run(Sim, 3);
				TestEqual(TEXT("the head goes"), Name(1), FString(TEXT("minecraft:air")));
				TestEqual(TEXT("a plain piston leaves the blocks"), Name(13), FString(TEXT("minecraft:cobblestone")));
			}
			else
			{
				TestEqual(TEXT("a 13th block stops it"), Name(1), FString(TEXT("minecraft:cobblestone")));
				TestEqual(TEXT("so it is not extended"), Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 1, 0)), TEXT("extended")).ToString(), FString(TEXT("false")));
			}
		}
	}
	{
		// A sticky piston pulls its block back, and a piston facing obsidian does nothing.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:sticky_piston[extended=false,facing=east]")));
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:cobblestone")));
		World.SetBlock(FIntVector(0, 1, -1), State(TEXT("minecraft:redstone_block")));
		Run(Sim, 5);
		const auto Name = [&](int32 X) { return Registry.GetBlockOf(World.GetBlock(FIntVector(X, 1, 0))).Name.ToString(); };
		TestEqual(TEXT("the block is pushed"), Name(2), FString(TEXT("minecraft:cobblestone")));
		World.SetBlock(FIntVector(0, 1, -1), Air);
		Run(Sim, 1);
		TestEqual(TEXT("the block is on its way back"), Name(1), FString(TEXT("minecraft:moving_piston")));
		Run(Sim, 4);
		TestEqual(TEXT("and pulled back"), Name(1), FString(TEXT("minecraft:cobblestone")));
		TestEqual(TEXT("leaving nothing behind"), Name(2), FString(TEXT("minecraft:air")));

		FMcBlockWorld Wall{FMcDimensionType()};
		MakeWorld(Wall);
		FMcSimulation WallSim(Wall);
		Wall.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:piston[extended=false,facing=east]")));
		Wall.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:obsidian")));
		Wall.SetBlock(FIntVector(0, 1, -1), State(TEXT("minecraft:redstone_block")));
		Run(WallSim, 5);
		TestEqual(TEXT("obsidian does not move"), Registry.GetBlockOf(Wall.GetBlock(FIntVector(1, 1, 0))).Name.ToString(), FString(TEXT("minecraft:obsidian")));
		TestEqual(TEXT("and the piston stays in"), Registry.GetPropertyValue(Wall.GetBlock(FIntVector(0, 1, 0)), TEXT("extended")).ToString(), FString(TEXT("false")));
	}
	{
		// A pulse under 3 ticks leaves the block pushed where it is; a longer one pulls it back.
		for (int32 Long = 0; Long < 2; ++Long)
		{
			FMcBlockWorld World{FMcDimensionType()};
			MakeWorld(World);
			FMcSimulation Sim(World);
			World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:sticky_piston[extended=false,facing=east]")));
			World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:cobblestone")));
			World.SetBlock(FIntVector(0, 1, -1), State(TEXT("minecraft:redstone_block")));
			Run(Sim, Long ? 4 : 1);
			World.SetBlock(FIntVector(0, 1, -1), Air);
			Run(Sim, 8);
			const auto Name = [&](int32 X) { return Registry.GetBlockOf(World.GetBlock(FIntVector(X, 1, 0))).Name.ToString(); };
			TestEqual(TEXT("the piston is back in"), Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 1, 0)), TEXT("extended")).ToString(), FString(TEXT("false")));
			TestEqual(TEXT("a short pulse leaves the block out, a long one pulls it"), Name(Long ? 1 : 2), FString(TEXT("minecraft:cobblestone")));
			TestEqual(TEXT("and nothing is where it was"), Name(Long ? 2 : 1), FString(TEXT("minecraft:air")));
		}
	}
	{
		// Retracting, a slime block takes what is stuck to it along: behind it and beside it.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(2, 5, 0), State(TEXT("minecraft:slime_block")));
		World.SetBlock(FIntVector(3, 5, 0), State(TEXT("minecraft:cobblestone")));
		World.SetBlock(FIntVector(2, 5, 1), State(TEXT("minecraft:cobblestone")));
		World.SetBlock(FIntVector(1, 5, 0), State(TEXT("minecraft:piston_head[facing=east,short=false,type=sticky]")));
		World.SetBlock(FIntVector(0, 5, 0), State(TEXT("minecraft:sticky_piston[extended=true,facing=east]")));
		Run(Sim, 6);
		const auto Name = [&](const FIntVector& Pos) { return Registry.GetBlockOf(World.GetBlock(Pos)).Name.ToString(); };
		TestEqual(TEXT("the slime is pulled in"), Name(FIntVector(1, 5, 0)), FString(TEXT("minecraft:slime_block")));
		TestEqual(TEXT("the block behind it comes too"), Name(FIntVector(2, 5, 0)), FString(TEXT("minecraft:cobblestone")));
		TestEqual(TEXT("so does the one beside it"), Name(FIntVector(1, 5, 1)), FString(TEXT("minecraft:cobblestone")));
		TestEqual(TEXT("and nothing is left at the far end"), Name(FIntVector(3, 5, 0)), FString(TEXT("minecraft:air")));
	}
	{
		// A flower in the way is broken; slime takes the blocks beside it along.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:piston[extended=false,facing=east]")));
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:dandelion")));
		World.SetBlock(FIntVector(0, 1, -1), State(TEXT("minecraft:redstone_block")));
		Run(Sim, 5);
		TestEqual(TEXT("the piston extends over a flower"), Registry.GetBlockOf(World.GetBlock(FIntVector(1, 1, 0))).Name.ToString(), FString(TEXT("minecraft:piston_head")));

		FMcBlockWorld Slime{FMcDimensionType()};
		MakeWorld(Slime);
		FMcSimulation SlimeSim(Slime);
		Slime.SetBlock(FIntVector(0, 5, 0), State(TEXT("minecraft:piston[extended=false,facing=east]")));
		Slime.SetBlock(FIntVector(1, 5, 0), State(TEXT("minecraft:slime_block")));
		Slime.SetBlock(FIntVector(1, 5, 1), State(TEXT("minecraft:cobblestone")));
		Slime.SetBlock(FIntVector(1, 6, 0), State(TEXT("minecraft:dirt")));
		Slime.SetBlock(FIntVector(1, 5, -1), State(TEXT("minecraft:obsidian")));
		Slime.SetBlock(FIntVector(0, 5, -1), State(TEXT("minecraft:redstone_block")));
		Run(SlimeSim, 5);
		const auto Name = [&](const FIntVector& Pos) { return Registry.GetBlockOf(Slime.GetBlock(Pos)).Name.ToString(); };
		TestEqual(TEXT("the slime moves"), Name(FIntVector(2, 5, 0)), FString(TEXT("minecraft:slime_block")));
		TestEqual(TEXT("the block beside it comes along"), Name(FIntVector(2, 5, 1)), FString(TEXT("minecraft:cobblestone")));
		TestEqual(TEXT("and the one above"), Name(FIntVector(2, 6, 0)), FString(TEXT("minecraft:dirt")));
		TestEqual(TEXT("obsidian is left"), Name(FIntVector(1, 5, -1)), FString(TEXT("minecraft:obsidian")));
	}
	{
		// Quasi-connectivity: a piston is powered by what would power the block above it, with nothing touching it.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(5, 1, 0), State(TEXT("minecraft:piston[extended=false,facing=east]")));
		World.SetBlock(FIntVector(5, 3, 0), State(TEXT("minecraft:redstone_block")));
		Run(Sim, 3);
		TestEqual(TEXT("the piston fires"), Registry.GetBlockOf(World.GetBlock(FIntVector(6, 1, 0))).Name.ToString(), FString(TEXT("minecraft:piston_head")));
	}
	{
		// A clock: two observers watching each other, kicked once, keep pulsing.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:observer[facing=east,powered=false]")));
		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:observer[facing=west,powered=false]")));
		int32 Changes = 0;
		bool bLast = false;
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Sim.Tick();
			const bool bNow = Registry.GetPropertyValue(World.GetBlock(FIntVector(0, 1, 0)), TEXT("powered")).ToString() == TEXT("true");
			Changes += bNow != bLast ? 1 : 0;
			bLast = bNow;
		}
		TestTrue(TEXT("the observers keep each other going"), Changes >= 8);
	}
	{
		// Dust holds a block power and loses its support: dust on air breaks.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(3, 1, 3), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(3, 0, 3), Air);
		TestTrue(TEXT("dust with nothing under it is gone"), Registry.IsAir(World.GetBlock(FIntVector(3, 1, 3))));
		// A pressure plate with something on it powers dust; weighted plates scale with the count.
		World.SetBlock(FIntVector(8, 1, 0), State(TEXT("minecraft:stone_pressure_plate[powered=false]")));
		World.SetBlock(FIntVector(9, 1, 0), State(TEXT("minecraft:redstone_wire")));
		Sim.SetPressurePlate(FIntVector(8, 1, 0), 1);
		Run(Sim, 2);
		TestEqual(TEXT("a pressed plate powers the dust beside it"), Sim.GetDustPower(FIntVector(9, 1, 0)), 15);
		Sim.SetPressurePlate(FIntVector(8, 1, 0), 0);
		Run(Sim, 21);
		TestEqual(TEXT("and lets go after a while"), Sim.GetDustPower(FIntVector(9, 1, 0)), 0);
		World.SetBlock(FIntVector(8, 1, 4), State(TEXT("minecraft:light_weighted_pressure_plate[power=0]")));
		Sim.SetPressurePlate(FIntVector(8, 1, 4), 5);
		Run(Sim, 2);
		TestEqual(TEXT("a light plate gives 1 a thing on it"), Registry.GetPropertyValue(World.GetBlock(FIntVector(8, 1, 4)), TEXT("power")).ToString(), FString(TEXT("5")));
		// A target block hit by an arrow gives its strength for 20 ticks, and a new hit starts the time again.
			World.SetBlock(FIntVector(14, 1, 0), State(TEXT("minecraft:target[power=0]")));
			World.SetBlock(FIntVector(15, 1, 0), State(TEXT("minecraft:redstone_wire")));
			Sim.HitTarget(FIntVector(14, 1, 0), 9, 20);
			Run(Sim, 1);
			TestEqual(TEXT("a hit target powers the dust beside it"), Sim.GetDustPower(FIntVector(15, 1, 0)), 9);
			Run(Sim, 10);
			Sim.HitTarget(FIntVector(14, 1, 0), 9, 20);
			Run(Sim, 15);
			TestEqual(TEXT("a second hit keeps it on"), Registry.GetPropertyValue(World.GetBlock(FIntVector(14, 1, 0)), TEXT("power")).ToString(), FString(TEXT("9")));
			Run(Sim, 10);
			TestEqual(TEXT("and it lets go 20 ticks after the last"), Registry.GetPropertyValue(World.GetBlock(FIntVector(14, 1, 0)), TEXT("power")).ToString(), FString(TEXT("0")));
		// Tripwire: two hooks facing each other with string between, 1 to 40 apart. Something in the string powers both
		// hooks, which let go 10 ticks after it leaves. Placed in the row z=7: hooks at x=0 (facing east) and x=4 (facing west).
		const auto HookValue = [&](int32 X, const TCHAR* Property) { return Registry.GetPropertyValue(World.GetBlock(FIntVector(X, 1, 7)), Property).ToString(); };
		World.SetBlock(FIntVector(0, 1, 7), State(TEXT("minecraft:tripwire_hook[attached=false,facing=east,powered=false]")));
		for (int32 X = 1; X <= 3; ++X)
		{
			World.SetBlock(FIntVector(X, 1, 7), State(TEXT("minecraft:tripwire[attached=false,disarmed=false,east=false,north=false,powered=false,south=false,west=false]")));
		}
		World.SetBlock(FIntVector(4, 1, 7), State(TEXT("minecraft:tripwire_hook[attached=false,facing=west,powered=false]")));
		Run(Sim, 2);
		TestEqual(TEXT("hooks with string between are attached"), HookValue(0, TEXT("attached")), FString(TEXT("true")));
		Sim.SetPressurePlate(FIntVector(2, 1, 7), 1);
		Run(Sim, 1);
		TestEqual(TEXT("something in the string powers the near hook"), HookValue(0, TEXT("powered")), FString(TEXT("true")));
		TestEqual(TEXT("and the far one"), HookValue(4, TEXT("powered")), FString(TEXT("true")));
		Sim.SetPressurePlate(FIntVector(2, 1, 7), 0);
		Run(Sim, 8);
		TestEqual(TEXT("it holds a few ticks after the thing leaves"), HookValue(0, TEXT("powered")), FString(TEXT("true")));
		Run(Sim, 4);
		TestEqual(TEXT("and lets go after the 10"), HookValue(0, TEXT("powered")), FString(TEXT("false")));
			// A daylight detector reads the sun.
		World.SetBlock(FIntVector(12, 1, 0), State(TEXT("minecraft:daylight_detector[inverted=false,power=0]")));
		Sim.SetSunLight(11);
		Run(Sim, 3);
		TestEqual(TEXT("a detector gives the sun level"), Registry.GetPropertyValue(World.GetBlock(FIntVector(12, 1, 0)), TEXT("power")).ToString(), FString(TEXT("11")));
	}
	{
		// A player's hand: levers flip, buttons press and let go, doors, trapdoors and gates open and shut; iron ones
		// don't move for a hand. Each makes its sound.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		TArray<FName> Sounds;
		Sim.OnSound = [&Sounds](const FMcSoundEvent& Event) { Sounds.Add(Event.Name); };
		const auto Value = [&](const FIntVector& Pos, const TCHAR* Property) { return Registry.GetPropertyValue(World.GetBlock(Pos), Property).ToString(); };

		World.SetBlock(FIntVector(1, 1, 0), State(TEXT("minecraft:redstone_wire")));
		Lever(World, FIntVector(0, 1, 0), false);
		TestTrue(TEXT("a lever takes the click"), Sim.UseBlock(FIntVector(0, 1, 0), 0.0f));
		TestEqual(TEXT("and turns on"), Value(FIntVector(0, 1, 0), TEXT("powered")), FString(TEXT("true")));
		TestEqual(TEXT("powering the dust"), Sim.GetDustPower(FIntVector(1, 1, 0)), 15);
		Sim.UseBlock(FIntVector(0, 1, 0), 0.0f);
		TestEqual(TEXT("a second click turns it off"), Sim.GetDustPower(FIntVector(1, 1, 0)), 0);
		TestTrue(TEXT("it clicks"), Sounds.Num() == 2 && Sounds[0] == FName(TEXT("block.lever.click")));

		Sounds.Reset();
		World.SetBlock(FIntVector(4, 1, 0), State(TEXT("minecraft:oak_button[face=floor,facing=north,powered=false]")));
		Sim.UseBlock(FIntVector(4, 1, 0), 0.0f);
		TestEqual(TEXT("a pressed button is on"), Value(FIntVector(4, 1, 0), TEXT("powered")), FString(TEXT("true")));
		Run(Sim, 29);
		TestEqual(TEXT("for 30 ticks (wood)"), Value(FIntVector(4, 1, 0), TEXT("powered")), FString(TEXT("true")));
		Run(Sim, 1);
		TestEqual(TEXT("then lets go"), Value(FIntVector(4, 1, 0), TEXT("powered")), FString(TEXT("false")));
		TestTrue(TEXT("clicking on and off"), Sounds.Num() == 2 && Sounds[0] == FName(TEXT("block.wooden_button.click_on")) && Sounds[1] == FName(TEXT("block.wooden_button.click_off")));
		World.SetBlock(FIntVector(4, 1, 2), State(TEXT("minecraft:stone_button[face=floor,facing=north,powered=false]")));
		Sim.UseBlock(FIntVector(4, 1, 2), 0.0f);
		Run(Sim, 20);
		TestEqual(TEXT("a stone one for 20"), Value(FIntVector(4, 1, 2), TEXT("powered")), FString(TEXT("false")));

		Sounds.Reset();
		World.SetBlock(FIntVector(8, 1, 0), State(TEXT("minecraft:spruce_door[facing=north,half=lower,hinge=left,open=false,powered=false]")));
		World.SetBlock(FIntVector(8, 2, 0), State(TEXT("minecraft:spruce_door[facing=north,half=upper,hinge=left,open=false,powered=false]")));
		TestTrue(TEXT("a door takes the click on its top half"), Sim.UseBlock(FIntVector(8, 2, 0), 0.0f));
		TestEqual(TEXT("its bottom opens with it"), Value(FIntVector(8, 1, 0), TEXT("open")), FString(TEXT("true")));
		TestEqual(TEXT("and its top"), Value(FIntVector(8, 2, 0), TEXT("open")), FString(TEXT("true")));
		Sim.UseBlock(FIntVector(8, 1, 0), 0.0f);
		TestEqual(TEXT("and shuts again"), Value(FIntVector(8, 2, 0), TEXT("open")), FString(TEXT("false")));
		TestTrue(TEXT("a wooden door's sounds"), Sounds.Num() == 2 && Sounds[0] == FName(TEXT("block.wooden_door.open")) && Sounds[1] == FName(TEXT("block.wooden_door.close")));

		World.SetBlock(FIntVector(10, 1, 0), State(TEXT("minecraft:iron_door[facing=north,half=lower,hinge=left,open=false,powered=false]")));
		World.SetBlock(FIntVector(10, 2, 0), State(TEXT("minecraft:iron_door[facing=north,half=upper,hinge=left,open=false,powered=false]")));
		TestFalse(TEXT("an iron door doesn't take the click"), Sim.UseBlock(FIntVector(10, 1, 0), 0.0f));
		TestEqual(TEXT("and stays shut"), Value(FIntVector(10, 1, 0), TEXT("open")), FString(TEXT("false")));

		Sounds.Reset();
		World.SetBlock(FIntVector(12, 1, 0), State(TEXT("minecraft:cherry_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")));
		Sim.UseBlock(FIntVector(12, 1, 0), 0.0f);
		TestEqual(TEXT("a trapdoor opens"), Value(FIntVector(12, 1, 0), TEXT("open")), FString(TEXT("true")));
		TestTrue(TEXT("with cherry's sound"), Sounds.Num() == 1 && Sounds[0] == FName(TEXT("block.cherry_wood_trapdoor.open")));
		World.SetBlock(FIntVector(12, 1, 2), State(TEXT("minecraft:iron_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")));
		TestFalse(TEXT("an iron trapdoor doesn't"), Sim.UseBlock(FIntVector(12, 1, 2), 0.0f));

		// A gate facing north, opened by a player looking south (yaw 0) from its north side, swings to face south.
		World.SetBlock(FIntVector(14, 1, 0), State(TEXT("minecraft:oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]")));
		Sim.UseBlock(FIntVector(14, 1, 0), 0.0f);
		TestEqual(TEXT("a gate opens"), Value(FIntVector(14, 1, 0), TEXT("open")), FString(TEXT("true")));
		TestEqual(TEXT("away from the player"), Value(FIntVector(14, 1, 0), TEXT("facing")), FString(TEXT("south")));
		Sim.UseBlock(FIntVector(14, 1, 0), 180.0f);
		TestEqual(TEXT("and shuts as it is"), Value(FIntVector(14, 1, 0), TEXT("facing")), FString(TEXT("south")));
		// Looking north (yaw 180) at a gate facing north: it opens the way it faces.
		World.SetBlock(FIntVector(16, 1, 0), State(TEXT("minecraft:oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]")));
		Sim.UseBlock(FIntVector(16, 1, 0), 180.0f);
		TestEqual(TEXT("a gate opened from behind keeps its facing"), Value(FIntVector(16, 1, 0), TEXT("facing")), FString(TEXT("north")));

		TestFalse(TEXT("stone takes no click"), Sim.UseBlock(FIntVector(0, 0, 0), 0.0f));
	}
	{
		// A dispenser fires 4 ticks after it is powered, once a pulse; a powered hopper is locked.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		TArray<FIntVector> Fired;
		Sim.OnDispense = [&](const FIntVector& Pos) { Fired.Add(Pos); };
		const FIntVector Dispenser(0, 1, 0);
		World.SetBlock(Dispenser, State(TEXT("minecraft:dispenser[facing=east,triggered=false]")));
		World.SetBlock(FIntVector(0, 1, -1), State(TEXT("minecraft:redstone_block")));
		const auto Value = [&](const FIntVector& Pos, const TCHAR* Property) { return Registry.GetPropertyValue(World.GetBlock(Pos), Property).ToString(); };
		TestEqual(TEXT("powered, it is triggered"), Value(Dispenser, TEXT("triggered")), FString(TEXT("true")));
		Run(Sim, 3);
		TestEqual(TEXT("not yet fired"), Fired.Num(), 0);
		Run(Sim, 2);
		TestEqual(TEXT("fired after 4 ticks"), Fired.Num(), 1);
		Run(Sim, 10);
		TestEqual(TEXT("once while held"), Fired.Num(), 1);
		World.SetBlock(FIntVector(0, 1, -1), Air);
		Run(Sim, 1);
		TestEqual(TEXT("unpowered, it resets"), Value(Dispenser, TEXT("triggered")), FString(TEXT("false")));
		// Quasi-connectivity: power beside the block above counts.
		World.SetBlock(FIntVector(0, 2, -1), State(TEXT("minecraft:redstone_block")));
		Run(Sim, 5);
		TestEqual(TEXT("no update reaches it yet (vanilla rs_quasi)"), Fired.Num(), 1);
		World.SetBlock(FIntVector(0, 1, -1), State(TEXT("minecraft:stone")));
		Run(Sim, 5);
		TestEqual(TEXT("powered through the block above once a neighbour updates"), Fired.Num(), 2);

		const FIntVector Hopper(4, 1, 0);
		World.SetBlock(Hopper, State(TEXT("minecraft:hopper[enabled=true,facing=down]")));
		World.SetBlock(FIntVector(4, 1, -1), State(TEXT("minecraft:redstone_block")));
		TestEqual(TEXT("a powered hopper is locked"), Value(Hopper, TEXT("enabled")), FString(TEXT("false")));
		World.SetBlock(FIntVector(4, 1, -1), Air);
		TestEqual(TEXT("and free again"), Value(Hopper, TEXT("enabled")), FString(TEXT("true")));
	}
	{
		// Held to the reference client's dropper run (log_dropper1): power at tick 3 fires on 7 and holding it adds nothing; a one-tick
		// pulse still fires 4 ticks on; powered and dropped within the delay it fires with nothing powering it; power put back the
		// tick after it fired is a new pulse.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		int32 Now = 0;
		TArray<int32> Fired;
		Sim.OnDispense = [&](const FIntVector&) { Fired.Add(Now + 1); };
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:dropper[facing=east,triggered=false]")));
		const auto Step = [&](int32 Count) { for (int32 Index = 0; Index < Count; ++Index) { Sim.Tick(); ++Now; } };
		const auto Power = [&](bool bOn) { World.SetBlock(FIntVector(0, 1, -1), bOn ? State(TEXT("minecraft:redstone_block")) : Air); };
		Step(3); Power(true); Step(25); Power(false); Step(5);
		Power(true); Step(1); Power(false); Step(15);
		Power(true); Step(3); Power(false); Step(1); Power(true); Step(20);
		TestEqual(TEXT("dropper fires four times"), Fired.Num(), 4);
		if (Fired.Num() == 4)
		{
			TestEqual(TEXT("first fire"), Fired[0], 7);
			TestEqual(TEXT("pulse fire"), Fired[1], 37);
			TestEqual(TEXT("fire after power dropped inside the delay"), Fired[2], 53);
			TestEqual(TEXT("fire on the new pulse"), Fired[3], 57);
		}
	}
	return true;
}

#endif
