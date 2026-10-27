#include "McBlockEntity.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

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
		// An observer pulses for 2 ticks, 2 ticks after the block it faces changes, and lights a lamp.
		FMcBlockWorld World{FMcDimensionType()};
		MakeWorld(World);
		FMcSimulation Sim(World);
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:observer[facing=east,powered=false]")));
		World.SetBlock(FIntVector(-1, 1, 0), State(TEXT("minecraft:redstone_lamp[lit=false]")));
		Run(Sim, 3);
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
		// A daylight detector reads the sun.
		World.SetBlock(FIntVector(12, 1, 0), State(TEXT("minecraft:daylight_detector[inverted=false,power=0]")));
		Sim.SetSunLight(11);
		Run(Sim, 3);
		TestEqual(TEXT("a detector gives the sun level"), Registry.GetPropertyValue(World.GetBlock(FIntVector(12, 1, 0)), TEXT("power")).ToString(), FString(TEXT("11")));
	}
	return true;
}

#endif
