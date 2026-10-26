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
	return true;
}

#endif
