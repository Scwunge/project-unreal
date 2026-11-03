#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McPhysics.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPhysicsTest, "McClient.McEntity.Physics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPhysicsTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()) || !TestTrue(TEXT("shapes load"), FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Slab = State(TEXT("minecraft:stone_slab[type=bottom]"));
	const FMcStateId Ice = State(TEXT("minecraft:ice"));

	TestEqual(TEXT("stone is one full box"), FMcBlockShapes::Get().GetCollision(Stone).Num(), 1);
	TestFalse(TEXT("air has no collision"), FMcBlockShapes::Get().HasCollision(State(TEXT("minecraft:air"))));
	TestFalse(TEXT("short grass has no collision"), FMcBlockShapes::Get().HasCollision(State(TEXT("minecraft:short_grass"))));
	TestFalse(TEXT("torches have no collision"), FMcBlockShapes::Get().HasCollision(State(TEXT("minecraft:torch"))));
	TestEqual(TEXT("fences are 1.5 high"), FMcBlockShapes::Get().GetCollision(State(TEXT("minecraft:oak_fence"))).Last().Max.Y, 1.5f);
	TestEqual(TEXT("ice is slippery"), FMcBlockShapes::Get().GetSlipperiness(Ice), 0.98f);

	// A stone floor at y = 0 over chunks -1..1.
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 CZ = -1; CZ <= 1; ++CZ)
	{
		for (int32 CX = -1; CX <= 1; ++CX)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(CX, CZ)));
		}
	}
	auto Fill = [&](FIntVector A, FIntVector B, FMcStateId Id)
	{
		for (int32 Y = A.Y; Y <= B.Y; ++Y)
			for (int32 Z = A.Z; Z <= B.Z; ++Z)
				for (int32 X = A.X; X <= B.X; ++X)
					World.SetBlock(FIntVector(X, Y, Z), Id);
	};
	Fill(FIntVector(-16, 0, -16), FIntVector(31, 0, 31), Stone);

	auto Run = [&](FMcBody& Body, const FMcMoveInput& Input, int32 Ticks)
	{
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			McPhysics::TickPlayer(World, Body, Input);
		}
	};
	auto Standing = [](double X, double Z) { FMcBody Body; Body.Position = FVector(X, 1.0, Z); Body.bOnGround = true; return Body; };

	// Falling lands on the floor.
	{
		FMcBody Body;
		Body.Position = FVector(0.5, 5.0, 0.5);
		Run(Body, FMcMoveInput(), 40);
		TestEqual(TEXT("falls onto the floor"), Body.Position.Y, 1.0, 1e-6);
		TestTrue(TEXT("and is on the ground"), Body.bOnGround);
	}

	// Walking speed: 4.317 blocks a second; sprinting 5.612.
	auto MeasureSpeed = [&](FMcMoveInput Input)
	{
		FMcBody Body = Standing(-10.5, 0.5);
		Body.Position.Z = -10.5;
		Run(Body, Input, 60);
		const FVector Before = Body.Position;
		Run(Body, Input, 20);
		return FVector::Dist(Before, Body.Position);
	};
	FMcMoveInput Walk;
	Walk.Forward = 1.0f;
	TestEqual(TEXT("walks 4.317 blocks a second"), MeasureSpeed(Walk), 4.317, 0.01);
	FMcMoveInput Sprint = Walk;
	Sprint.bSprint = true;
	TestEqual(TEXT("sprints 5.612 blocks a second"), MeasureSpeed(Sprint), 5.612, 0.01);
	FMcMoveInput Sneak = Walk;
	Sneak.bSneak = true;
	TestEqual(TEXT("sneaks 1.295 blocks a second"), MeasureSpeed(Sneak), 1.295, 0.01);
	FMcMoveInput Diagonal = Walk;
	Diagonal.Strafe = 1.0f;
	TestEqual(TEXT("diagonals are no faster"), MeasureSpeed(Diagonal), 4.317 / 0.98, 0.02);

	// Yaw 0 walks south (+z), 90 west (-x).
	{
		FMcBody Body = Standing(0.5, 0.5);
		Run(Body, Walk, 10);
		TestTrue(TEXT("yaw 0 walks south"), Body.Position.Z > 1.0 && FMath::Abs(Body.Position.X - 0.5) < 1e-6);
		Body = Standing(0.5, 0.5);
		FMcMoveInput West = Walk;
		West.Yaw = 90.0f;
		Run(Body, West, 10);
		TestTrue(TEXT("yaw 90 walks west"), Body.Position.X < 0.0 && FMath::Abs(Body.Position.Z - 0.5) < 1e-6);
	}

	// A standing jump peaks 1.2522 blocks up and lands 12 ticks later.
	{
		FMcBody Body = Standing(0.5, 0.5);
		FMcMoveInput Jump;
		Jump.bJump = true;
		McPhysics::TickPlayer(World, Body, Jump);
		double Peak = Body.Position.Y;
		int32 Ticks = 1;
		while (!Body.bOnGround && Ticks < 40)
		{
			McPhysics::TickPlayer(World, Body, FMcMoveInput());
			Peak = FMath::Max(Peak, Body.Position.Y);
			++Ticks;
		}
		TestEqual(TEXT("jump height"), Peak - 1.0, 1.2522, 0.001);
		TestEqual(TEXT("jump lasts 12 ticks"), Ticks, 12);
	}

	// Walking into a slab steps up onto it; into a full block stops.
	{
		Fill(FIntVector(0, 1, 3), FIntVector(0, 1, 31), Slab);
		FMcBody Body = Standing(0.5, 0.5);
		Run(Body, Walk, 30);
		TestEqual(TEXT("steps up onto a slab"), Body.Position.Y, 1.5, 1e-6);
		TestTrue(TEXT("and walks on along it"), Body.Position.Z > 4.0);

		Fill(FIntVector(2, 1, 3), FIntVector(2, 1, 3), Stone);
		Body = Standing(2.5, 0.5);
		Run(Body, Walk, 30);
		TestEqual(TEXT("doesn't step up a full block"), Body.Position.Y, 1.0, 1e-6);
		TestEqual(TEXT("stops at its face"), Body.Position.Z, 3.0 - 0.3, 1e-6);
		TestTrue(TEXT("and knows it hit a wall"), Body.bHorizontalCollision);
	}

	// Sneaking off a platform's edge stops at the edge; walking falls off.
	{
		Fill(FIntVector(8, 1, 0), FIntVector(8, 1, 0), Stone);
		FMcBody Body;
		Body.Position = FVector(8.5, 2.0, 0.5);
		Body.bOnGround = true;
		Run(Body, Sneak, 60);
		TestEqual(TEXT("sneaking stays on the platform"), Body.Position.Y, 2.0, 1e-6);
		TestTrue(TEXT("hanging over its edge"), Body.Position.Z > 1.0 && Body.Position.Z < 1.3 + 1e-6);

		Body.Position = FVector(8.5, 2.0, 0.5);
		Body.bOnGround = true;
		Body.Velocity = FVector::ZeroVector;
		Run(Body, Walk, 30);
		TestEqual(TEXT("walking falls off"), Body.Position.Y, 1.0, 1e-6);
	}

	// Ice carries you further after letting go.
	{
		auto Glide = [&](FMcStateId Floor)
		{
			Fill(FIntVector(-16, 0, 8), FIntVector(-14, 0, 31), Floor);
			FMcBody Body = Standing(-14.5, 8.5);
			Run(Body, Walk, 40);
			const double Start = Body.Position.Z;
			Run(Body, FMcMoveInput(), 40);
			return Body.Position.Z - Start;
		};
		const double OnStone = Glide(Stone);
		const double OnIce = Glide(Ice);
		TestTrue(TEXT("you glide much further on ice"), OnIce > OnStone * 4.0);
	}

	// Creative flight: 10.89 blocks a second.
	{
		FMcMoveInput Fly = Walk;
		Fly.bFlying = true;
		FMcBody Body;
		Body.Position = FVector(-10.5, 5.0, -10.5);
		Run(Body, Fly, 60);
		const FVector Before = Body.Position;
		Run(Body, Fly, 20);
		TestEqual(TEXT("flies 10.89 blocks a second"), FVector::Dist(Before, Body.Position), 10.89, 0.02);
		TestEqual(TEXT("and doesn't fall"), Body.Position.Y, 5.0, 1e-6);
	}

	// Collision shapes in detail.
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		auto Boxes = [&](const TCHAR* Text) { return Shapes.GetCollision(State(Text)); };
		auto Near = [](const FBox3f& Box, float X0, float Y0, float Z0, float X1, float Y1, float Z1)
		{
			return Box.Min.Equals(FVector3f(X0, Y0, Z0) / 16.0f, 1e-5f) && Box.Max.Equals(FVector3f(X1, Y1, Z1) / 16.0f, 1e-5f);
		};

		const TConstArrayView<FBox3f> OuterLeft = Boxes(TEXT("minecraft:oak_stairs[facing=north,half=bottom,shape=outer_left]"));
		if (TestEqual(TEXT("outer stairs: slab plus one quarter"), OuterLeft.Num(), 2))
		{
			TestTrue(TEXT("facing north, outer_left is the north-west quarter"), Near(OuterLeft[1], 0, 8, 0, 8, 16, 8));
		}
		const TConstArrayView<FBox3f> OuterRightEast = Boxes(TEXT("minecraft:oak_stairs[facing=east,half=top,shape=outer_right]"));
		if (TestEqual(TEXT("outer stairs, upside down: slab plus one quarter"), OuterRightEast.Num(), 2))
		{
			TestTrue(TEXT("facing east, upside down, outer_right is the south-east quarter, hung from the top"), Near(OuterRightEast[1], 8, 0, 8, 16, 8, 16));
		}
		const TConstArrayView<FBox3f> InnerRight = Boxes(TEXT("minecraft:oak_stairs[facing=north,half=bottom,shape=inner_right]"));
		if (TestEqual(TEXT("inner stairs: slab, step and a quarter"), InnerRight.Num(), 3))
		{
			TestTrue(TEXT("facing north, inner_right adds the south-east quarter"), Near(InnerRight[2], 8, 8, 8, 16, 16, 16));
		}
		TestEqual(TEXT("straight stairs: slab and step"), Boxes(TEXT("minecraft:oak_stairs[facing=south,half=bottom,shape=straight]")).Num(), 2);

		TestEqual(TEXT("a wall that runs straight has no post"), Boxes(TEXT("minecraft:cobblestone_wall[north=low,south=low,east=none,west=none,up=false]")).Num(), 2);
		TestEqual(TEXT("a wall corner has a post and two arms"), Boxes(TEXT("minecraft:cobblestone_wall[north=low,east=low,south=none,west=none,up=true]")).Num(), 3);
		TestTrue(TEXT("wall arms run to the middle"), Near(Boxes(TEXT("minecraft:cobblestone_wall[north=low,south=none,east=none,west=none,up=true]"))[1], 5, 0, 0, 11, 24, 8));

		TestTrue(TEXT("a standing lantern is 9 pixels high"), FMath::IsNearlyEqual(Boxes(TEXT("minecraft:lantern[hanging=false,waterlogged=false]")).Last().Max.Y, 9.0f / 16.0f));
		TestTrue(TEXT("a hanging lantern hangs from the top"), FMath::IsNearlyEqual(Boxes(TEXT("minecraft:lantern[hanging=true,waterlogged=false]")).Last().Max.Y, 10.0f / 16.0f));
		TestTrue(TEXT("a chain along x lies down"), Near(Boxes(TEXT("minecraft:iron_chain[axis=x,waterlogged=false]"))[0], 0, 6.5f, 6.5f, 16, 9.5f, 9.5f));
		TestEqual(TEXT("an anvil has four parts"), Boxes(TEXT("minecraft:anvil[facing=north]")).Num(), 4);
		TestTrue(TEXT("a north-facing anvil's top bar runs north to south"), Near(Boxes(TEXT("minecraft:anvil[facing=north]"))[3], 3, 10, 0, 13, 16, 16));
		TestTrue(TEXT("an east-facing anvil's top bar runs east to west"), Near(Boxes(TEXT("minecraft:anvil[facing=east]"))[3], 0, 10, 3, 16, 16, 13));
		TestEqual(TEXT("a cauldron: floor, four walls and eight leg pieces"), Boxes(TEXT("minecraft:cauldron")).Num(), 13);
		TestTrue(TEXT("a hopper's spout points down"), Near(Boxes(TEXT("minecraft:hopper[facing=down,enabled=true]")).Last(), 6, 0, 6, 10, 4, 10));
		TestTrue(TEXT("a hopper's spout points east"), Near(Boxes(TEXT("minecraft:hopper[facing=east,enabled=true]")).Last(), 12, 4, 6, 16, 8, 10));
		TestTrue(TEXT("a brewing stand's rod is 14 high"), Near(Boxes(TEXT("minecraft:brewing_stand"))[1], 7, 0, 7, 9, 14, 9));
		TestTrue(TEXT("a campfire is 7 high"), Near(Boxes(TEXT("minecraft:campfire[facing=north,lit=true,signal_fire=false,waterlogged=false]"))[0], 0, 0, 0, 16, 7, 16));
		TestTrue(TEXT("a stonecutter is 9 high"), Near(Boxes(TEXT("minecraft:stonecutter[facing=north]"))[0], 0, 0, 0, 16, 9, 16));
		TestTrue(TEXT("a cake with three bites left on the east side"), Near(Boxes(TEXT("minecraft:cake[bites=3]"))[0], 7, 0, 1, 15, 8, 15));
		TestTrue(TEXT("four candles"), Near(Boxes(TEXT("minecraft:candle[candles=4,lit=false,waterlogged=false]"))[0], 5, 0, 5, 11, 6, 10));
		TestTrue(TEXT("a sea pickle"), Near(Boxes(TEXT("minecraft:sea_pickle[pickles=4,waterlogged=true]"))[0], 2, 0, 2, 14, 7, 14));
		TestTrue(TEXT("a skeleton skull"), Near(Boxes(TEXT("minecraft:skeleton_skull[powered=false,rotation=0]"))[0], 4, 0, 4, 12, 8, 12));
		TestTrue(TEXT("a decorated pot"), Near(Boxes(TEXT("minecraft:decorated_pot[cracked=false,facing=north,waterlogged=false]"))[0], 1, 0, 1, 15, 16, 15));
		TestTrue(TEXT("a conduit"), Near(Boxes(TEXT("minecraft:conduit[waterlogged=false]"))[0], 5, 5, 5, 11, 11, 11));
		TestTrue(TEXT("a pointed dripstone tip growing down hangs from the top"), Near(Boxes(TEXT("minecraft:pointed_dripstone[thickness=tip,vertical_direction=down,waterlogged=false]"))[0], 5, 5, 5, 11, 16, 11));
		TestTrue(TEXT("a large amethyst bud on a north wall"), Near(Boxes(TEXT("minecraft:large_amethyst_bud[facing=north,waterlogged=false]"))[0], 3, 3, 11, 13, 13, 16));
		TestTrue(TEXT("an end rod pointing down"), Near(Boxes(TEXT("minecraft:end_rod[facing=down]"))[1], 7, 0, 7, 9, 15, 9));
		TestTrue(TEXT("an extended piston facing up keeps its body at the bottom"), Near(Boxes(TEXT("minecraft:piston[extended=true,facing=up]"))[0], 0, 0, 0, 16, 12, 16));
		TestTrue(TEXT("a retracted piston is a full cube"), Near(Boxes(TEXT("minecraft:piston[extended=false,facing=up]"))[0], 0, 0, 0, 16, 16, 16));
		TestTrue(TEXT("a sculk sensor is half height"), Near(Boxes(TEXT("minecraft:sculk_sensor[power=0,sculk_sensor_phase=inactive,waterlogged=false]"))[0], 0, 0, 0, 16, 8, 16));
		TestTrue(TEXT("an end portal frame with an eye adds the eye"), Boxes(TEXT("minecraft:end_portal_frame[eye=true,facing=north]")).Num() == 2);
		TestFalse(TEXT("coral wall fans have no collision"), Shapes.HasCollision(State(TEXT("minecraft:tube_coral_wall_fan[facing=north,waterlogged=false]"))));
		TestTrue(TEXT("a closed fence gate is 1.5 high"), FMath::IsNearlyEqual(Boxes(TEXT("minecraft:oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]"))[0].Max.Y, 1.5f));
		TestEqual(TEXT("an open fence gate has none"), Boxes(TEXT("minecraft:oak_fence_gate[facing=north,in_wall=false,open=true,powered=false]")).Num(), 0);

		TestTrue(TEXT("ladders are climbable"), Shapes.IsClimbable(State(TEXT("minecraft:ladder[facing=north,waterlogged=false]"))));
		TestTrue(TEXT("vines are climbable"), Shapes.IsClimbable(State(TEXT("minecraft:vine"))));
		TestTrue(TEXT("scaffolding is climbable"), Shapes.IsClimbable(State(TEXT("minecraft:scaffolding"))));
		TestFalse(TEXT("stone isn't climbable"), Shapes.IsClimbable(Stone));
		TestTrue(TEXT("cobwebs tangle"), Shapes.SlowsLikeCobweb(State(TEXT("minecraft:cobweb"))));
		TestTrue(TEXT("sweet berry bushes slow you"), Shapes.GetStuckMultiplier(State(TEXT("minecraft:sweet_berry_bush[age=0]"))).X == 0.8f);
		TestTrue(TEXT("water is water"), Shapes.GetFluid(State(TEXT("minecraft:water[level=0]"))) == EMcFluid::Water);
		TestTrue(TEXT("lava is lava"), Shapes.GetFluid(State(TEXT("minecraft:lava[level=3]"))) == EMcFluid::Lava);
		TestEqual(TEXT("fluid levels"), static_cast<int32>(Shapes.GetFluidLevel(State(TEXT("minecraft:water[level=5]")))), 5);
		TestTrue(TEXT("waterlogged stairs hold water"), Shapes.GetFluid(State(TEXT("minecraft:oak_stairs[facing=north,half=bottom,shape=straight,waterlogged=true]"))) == EMcFluid::Water);
		TestTrue(TEXT("kelp holds water"), Shapes.GetFluid(State(TEXT("minecraft:kelp[age=0]"))) == EMcFluid::Water);
	}

	// Swimming.
	{
		const FMcStateId Water = State(TEXT("minecraft:water[level=0]"));
		const FMcStateId Lava = State(TEXT("minecraft:lava[level=0]"));
		Fill(FIntVector(12, 1, 0), FIntVector(14, 6, 31), Water);

		// Sinking is slow: ten ticks cost about half a block, where falling through air would cost over a block.
		FMcBody Body;
		Body.Position = FVector(13.5, 3.0, 13.5);
		Run(Body, FMcMoveInput(), 10);
		TestTrue(TEXT("in water"), Body.bInWater);
		TestFalse(TEXT("not in lava"), Body.bInLava);
		TestEqual(TEXT("sinks slowly"), Body.Position.Y, 2.87, 0.03);

		// Space swims up.
		FMcMoveInput SwimUp;
		SwimUp.bJump = true;
		Body = FMcBody();
		Body.Position = FVector(13.5, 2.0, 13.5);
		Run(Body, SwimUp, 10);
		TestTrue(TEXT("Space swims up"), Body.Position.Y > 2.5);

		// Sneak sinks faster than doing nothing.
		FMcMoveInput SinkInput;
		SinkInput.bSneak = true;
		Body = FMcBody();
		Body.Position = FVector(13.5, 5.0, 13.5);
		FMcBody Idle = Body;
		Run(Body, SinkInput, 10);
		Run(Idle, FMcMoveInput(), 10);
		TestTrue(TEXT("sneaking sinks faster"), Body.Position.Y < Idle.Position.Y - 0.2);

		// Wading on the bottom: the drag of 0.8 and acceleration of 0.02 limit speed to about 2 blocks a second (0.098 a tick: the push is added, the body moves, then the drag).
		Body = FMcBody();
		Body.Position = FVector(13.5, 1.0, 1.0);
		Body.bOnGround = true;
		Run(Body, Walk, 40);
		const double Before = Body.Position.Z;
		Run(Body, Walk, 20);
		TestEqual(TEXT("swims along slowly"), Body.Position.Z - Before, 20.0 * 0.098, 0.02);

		// Lava drags harder than water.
		Fill(FIntVector(-8, 1, -8), FIntVector(-6, 6, -6), Lava);
		Body = FMcBody();
		Body.Position = FVector(-6.5, 3.0, -6.5);
		Run(Body, FMcMoveInput(), 10);
		TestTrue(TEXT("in lava"), Body.bInLava);
		TestTrue(TEXT("sinks even more slowly in lava"), Body.Position.Y > 2.6 && Body.Position.Y < 2.95);

		// Flowing water pushes: a flowing block next to a source carries you away from it.
		World.SetBlock(FIntVector(20, 1, 20), Water);
		World.SetBlock(FIntVector(21, 1, 20), State(TEXT("minecraft:water[level=1]")));
		Body = FMcBody();
		Body.Position = FVector(21.5, 1.0, 20.5);
		Run(Body, FMcMoveInput(), 5);
		TestTrue(TEXT("flowing water carries you downstream"), Body.Position.X > 21.55 && FMath::Abs(Body.Position.Z - 20.5) < 1e-6);
	}

	// Ladders.
	{
		Fill(FIntVector(26, 1, 26), FIntVector(26, 12, 26), Stone);
		Fill(FIntVector(26, 1, 25), FIntVector(26, 12, 25), State(TEXT("minecraft:ladder[facing=north]")));
		FMcMoveInput Press = Walk;  // yaw 0 faces south, into the wall
		FMcBody Body = Standing(26.5, 25.5);
		Run(Body, Press, 3);
		const double Start = Body.Position.Y;
		Run(Body, Press, 10);
		TestTrue(TEXT("climbing"), Body.bClimbing);
		TestEqual(TEXT("pressing against the wall climbs at 0.1176 a tick"), Body.Position.Y - Start, 1.176, 0.05);

		// Sneaking holds you where you are.
		Body = FMcBody();
		Body.Position = FVector(26.5, 6.0, 25.5);
		FMcMoveInput Hold;
		Hold.bSneak = true;
		Run(Body, Hold, 10);
		TestEqual(TEXT("sneaking on a ladder holds still"), Body.Position.Y, 6.0, 1e-6);

		// Letting go slides down at no more than 0.15 a tick.
		Body = FMcBody();
		Body.Position = FVector(26.5, 10.0, 25.5);
		Run(Body, FMcMoveInput(), 10);
		TestTrue(TEXT("slides down slowly"), Body.Position.Y < 9.0 && Body.Position.Y > 8.4);
	}

	// Cobwebs.
	{
		const FMcStateId Web = State(TEXT("minecraft:cobweb"));
		Fill(FIntVector(28, 1, 28), FIntVector(28, 2, 31), Web);
		FMcBody Body = Standing(28.5, 28.5);
		Run(Body, Walk, 20);
		TestTrue(TEXT("tangled in a web"), Body.bStuck);
		TestTrue(TEXT("a web slows walking to a crawl"), Body.Position.Z - 28.5 < 0.8);

		Fill(FIntVector(29, 1, 29), FIntVector(29, 8, 29), Web);
		Body = FMcBody();
		Body.Position = FVector(29.5, 6.0, 29.5);
		Run(Body, FMcMoveInput(), 20);
		TestTrue(TEXT("a web holds a falling body"), Body.Position.Y > 5.8);
	}
	// Bubble columns (fitted to the bubble1 and bubble2 vanilla runs).
	{
		const FMcStateId Up = State(TEXT("minecraft:bubble_column[drag=false]"));
		const FMcStateId Down = State(TEXT("minecraft:bubble_column[drag=true]"));
		Fill(FIntVector(-14, 1, -14), FIntVector(-14, 6, -14), Up);
		Fill(FIntVector(-10, 1, -14), FIntVector(-10, 6, -14), Down);
		const auto Box = [](double X, double Y, double Z) { return FBox(FVector(X - 0.125, Y, Z - 0.125), FVector(X + 0.125, Y + 0.25, Z + 0.125)); };

		FVector Velocity = FVector::ZeroVector;
		TestTrue(TEXT("an item in a column touches it"), McPhysics::BubbleColumnPush(World, Box(-13.5, 3.2, -13.5), Velocity));
		TestEqual(TEXT("up pushes 0.06 inside"), Velocity.Y, 0.06, 1e-9);
		Velocity = FVector::ZeroVector;
		McPhysics::BubbleColumnPush(World, Box(-13.5, 2.9, -13.5), Velocity);
		TestEqual(TEXT("on a block boundary it is pushed twice"), Velocity.Y, 0.12, 1e-9);
		Velocity.Y = 0.68;
		McPhysics::BubbleColumnPush(World, Box(-13.5, 3.2, -13.5), Velocity);
		TestEqual(TEXT("up is capped at 0.7"), Velocity.Y, 0.7, 1e-9);
		Velocity = FVector::ZeroVector;
		McPhysics::BubbleColumnPush(World, Box(-13.5, 6.2, -13.5), Velocity);
		TestEqual(TEXT("the top block pushes 0.1"), Velocity.Y, 0.1, 1e-9);
		Velocity = FVector::ZeroVector;
		McPhysics::BubbleColumnPush(World, Box(-9.5, 3.2, -13.5), Velocity);
		TestEqual(TEXT("down pulls 0.03"), Velocity.Y, -0.03, 1e-9);
		Velocity.Y = -0.29;
		McPhysics::BubbleColumnPush(World, Box(-9.5, 3.2, -13.5), Velocity);
		TestEqual(TEXT("down is capped at -0.3"), Velocity.Y, -0.3, 1e-9);
		Velocity = FVector::ZeroVector;
		TestFalse(TEXT("outside a column nothing happens"), McPhysics::BubbleColumnPush(World, Box(-5.5, 3.2, -13.5), Velocity));

		// A player in the column picks up speed (0.115, 0.207, 0.341 ... in the vanilla log) and rises through it.
		FMcBody Body;
		Body.Position = FVector(-13.5, 1.0, -13.5);
		Run(Body, FMcMoveInput(), 8);
		TestTrue(TEXT("the column lifts a player"), Body.Position.Y > 2.5 && Body.Velocity.Y > 0.3);
		Body = FMcBody();
		Body.Position = FVector(-9.5, 5.0, -13.5);
		Run(Body, FMcMoveInput(), 6);
		TestTrue(TEXT("a column over magma pulls a player down"), Body.Position.Y < 4.6 && Body.Velocity.Y < -0.1);
	}
	return true;
}

#endif
