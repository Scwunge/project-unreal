#include "McPhysics.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

namespace
{
	// Gravity and air drag on y; drag for x and z in the air, and the extra factor slipperiness multiplies.
	constexpr double Gravity = 0.08;
	constexpr double VerticalDrag = 0.98;
	constexpr double AirDrag = 0.91;
	// Walking speed attribute, and how much of it reaches the ground: 0.6 slipperiness gives 0.546 per tick.
	constexpr double MovementSpeed = 0.1;
	constexpr double GroundFactor = 0.16277136;  // 0.546 cubed
	constexpr double AirAcceleration = 0.02;
	constexpr double SprintAirAcceleration = 0.026;
	constexpr double SprintMultiplier = 1.3;
	constexpr double SneakMultiplier = 0.3;
	constexpr double JumpVelocity = 0.42;
	constexpr double SprintJumpBoost = 0.2;
	constexpr double StopThreshold = 0.003;
	// Creative flight.
	constexpr double FlyingSpeed = 0.05;
	constexpr double FlyingVerticalDrag = 0.6;
	// Keeps boxes that touch from counting as overlapping.
	constexpr double Epsilon = 1e-7;

	// How far a moving box can go along one axis before it meets Other.
	double ClipAxis(const FBox& Box, const FBox& Other, int32 Axis, double Distance)
	{
		for (int32 A = 0; A < 3; ++A)
		{
			if (A != Axis && (Other.Max[A] <= Box.Min[A] + Epsilon || Other.Min[A] >= Box.Max[A] - Epsilon))
			{
				return Distance;
			}
		}
		if (Distance > 0.0 && Other.Min[Axis] >= Box.Max[Axis] - Epsilon)
		{
			return FMath::Min(Distance, Other.Min[Axis] - Box.Max[Axis]);
		}
		if (Distance < 0.0 && Other.Max[Axis] <= Box.Min[Axis] + Epsilon)
		{
			return FMath::Max(Distance, Other.Max[Axis] - Box.Min[Axis]);
		}
		return Distance;
	}

	double CollideAxis(const TArray<FBox>& Boxes, const FBox& Box, int32 Axis, double Distance)
	{
		if (FMath::Abs(Distance) < Epsilon)
		{
			return 0.0;
		}
		for (const FBox& Other : Boxes)
		{
			Distance = ClipAxis(Box, Other, Axis, Distance);
		}
		return FMath::Abs(Distance) < Epsilon ? 0.0 : Distance;
	}

	FVector Shift(int32 Axis, double Distance)
	{
		FVector V = FVector::ZeroVector;
		V[Axis] = Distance;
		return V;
	}

	// y first, then the larger horizontal axis, then the other.
	FVector CollideBoxes(const TArray<FBox>& Boxes, FBox Box, const FVector& Motion)
	{
		FVector Result = FVector::ZeroVector;
		const int32 Order[3] = { 1, FMath::Abs(Motion.X) < FMath::Abs(Motion.Z) ? 2 : 0, FMath::Abs(Motion.X) < FMath::Abs(Motion.Z) ? 0 : 2 };
		for (const int32 Axis : Order)
		{
			Result[Axis] = CollideAxis(Boxes, Box, Axis, Motion[Axis]);
			Box = Box.ShiftBy(Shift(Axis, Result[Axis]));
		}
		return Result;
	}

	double HorizontalSizeSquared(const FVector& V)
	{
		return V.X * V.X + V.Z * V.Z;
	}

	// The slipperiness of the block under the body's feet, or 0.6 when nothing is there.
	float SlipperinessBelow(const FMcBlockWorld& World, const FMcBody& Body)
	{
		const FIntVector Below(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y - 0.5000001), FMath::FloorToInt32(Body.Position.Z));
		return FMcBlockShapes::Get().GetSlipperiness(World.GetBlock(Below));
	}

	float SpeedFactorAt(const FMcBlockWorld& World, const FMcBody& Body)
	{
		// The block the feet are in (soul sand and honey are lower than a full block), else the one under it.
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		const FIntVector Feet(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z));
		const float Inside = Shapes.GetSpeedFactor(World.GetBlock(Feet));
		if (Inside != 1.0f)
		{
			return Inside;
		}
		return Shapes.GetSpeedFactor(World.GetBlock(Feet - FIntVector(0, 1, 0)));
	}

	// Adds input to velocity: forward and strafe turned by yaw, scaled to Acceleration, never faster on a diagonal.
	void Accelerate(FMcBody& Body, double Strafe, double Forward, double Acceleration, float Yaw)
	{
		double LengthSquared = Strafe * Strafe + Forward * Forward;
		if (LengthSquared < 1e-7)
		{
			return;
		}
		const double Scale = Acceleration / FMath::Max(1.0, FMath::Sqrt(LengthSquared));
		Strafe *= Scale;
		Forward *= Scale;
		const double Radians = FMath::DegreesToRadians(static_cast<double>(Yaw));
		const double Sin = FMath::Sin(Radians);
		const double Cos = FMath::Cos(Radians);
		// Yaw 0 faces +z; strafing left at yaw 0 goes +x.
		Body.Velocity.X += Strafe * Cos - Forward * Sin;
		Body.Velocity.Z += Forward * Cos + Strafe * Sin;
	}
}

FBox FMcBody::GetBox() const
{
	const double Half = Width * 0.5;
	return FBox(FVector(Position.X - Half, Position.Y, Position.Z - Half), FVector(Position.X + Half, Position.Y + Height, Position.Z + Half));
}

void McPhysics::GatherBoxes(const FMcBlockWorld& World, const FBox& Area, TArray<FBox>& Out)
{
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	// One block lower than the area, for fences and walls that reach up 1.5 blocks.
	const FIntVector Min(FMath::FloorToInt32(Area.Min.X - Epsilon), FMath::FloorToInt32(Area.Min.Y - Epsilon) - 1, FMath::FloorToInt32(Area.Min.Z - Epsilon));
	const FIntVector Max(FMath::FloorToInt32(Area.Max.X + Epsilon), FMath::FloorToInt32(Area.Max.Y + Epsilon), FMath::FloorToInt32(Area.Max.Z + Epsilon));
	for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
	{
		for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				const FVector Corner(X, Y, Z);
				for (const FBox3f& Local : Shapes.GetCollision(World.GetBlock(FIntVector(X, Y, Z))))
				{
					const FBox Box(Corner + FVector(Local.Min), Corner + FVector(Local.Max));
					if (Box.Max.X > Area.Min.X && Box.Min.X < Area.Max.X && Box.Max.Y > Area.Min.Y && Box.Min.Y < Area.Max.Y && Box.Max.Z > Area.Min.Z && Box.Min.Z < Area.Max.Z)
					{
						Out.Add(Box);
					}
				}
			}
		}
	}
}

bool McPhysics::Collides(const FMcBlockWorld& World, const FBox& Box)
{
	TArray<FBox> Found;
	GatherBoxes(World, Box.ExpandBy(-Epsilon), Found);
	return Found.Num() > 0;
}

FVector McPhysics::Collide(const FMcBlockWorld& World, const FBox& Box, const FVector& Motion, float StepHeight, bool bOnGround)
{
	TArray<FBox> Boxes;
	// Everything the box could touch on the way, including stepping up.
	const FBox Swept = (Box + Box.ShiftBy(Motion)).ExpandBy(FVector(0.0, StepHeight, 0.0));
	GatherBoxes(World, Swept, Boxes);

	const FVector Result = CollideBoxes(Boxes, Box, Motion);
	const bool bBlockedSideways = Result.X != Motion.X || Result.Z != Motion.Z;
	const bool bLanding = bOnGround || (Motion.Y < 0.0 && Result.Y != Motion.Y);
	if (StepHeight <= 0.0f || !bBlockedSideways || !bLanding)
	{
		return Result;
	}

	// Try again from StepHeight higher, then settle back down onto whatever is there.
	const double Up = CollideAxis(Boxes, Box, 1, StepHeight);
	const FBox Raised = Box.ShiftBy(FVector(0.0, Up, 0.0));
	FVector Stepped = CollideBoxes(Boxes, Raised, FVector(Motion.X, 0.0, Motion.Z));
	const FBox Across = Raised.ShiftBy(Stepped);
	Stepped.Y = Up + CollideAxis(Boxes, Across, 1, -Up + FMath::Min(Motion.Y, 0.0));
	return HorizontalSizeSquared(Stepped) > HorizontalSizeSquared(Result) + Epsilon ? Stepped : Result;
}

void McPhysics::Move(const FMcBlockWorld& World, FMcBody& Body, FVector Motion, bool bSneakAtEdges)
{
	if (bSneakAtEdges && Body.bOnGround && Motion.Y <= 0.0)
	{
		// Sneaking stops at edges: cut horizontal motion, a twentieth of a block at a time, until there is
		// still ground within a step below.
		const FBox Box = Body.GetBox();
		auto HasGround = [&](double X, double Z) { return Collides(World, Box.ShiftBy(FVector(X, -Body.StepHeight, Z))); };
		constexpr double Step = 0.05;
		auto Approach = [](double V) { return FMath::Abs(V) <= Step ? 0.0 : V - FMath::Sign(V) * Step; };
		while (Motion.X != 0.0 && !HasGround(Motion.X, 0.0))
		{
			Motion.X = Approach(Motion.X);
		}
		while (Motion.Z != 0.0 && !HasGround(0.0, Motion.Z))
		{
			Motion.Z = Approach(Motion.Z);
		}
		while (Motion.X != 0.0 && Motion.Z != 0.0 && !HasGround(Motion.X, Motion.Z))
		{
			Motion.X = Approach(Motion.X);
			Motion.Z = Approach(Motion.Z);
		}
	}

	const FVector Result = Collide(World, Body.GetBox(), Motion, Body.StepHeight, Body.bOnGround);
	Body.Position += Result;

	const bool bHitX = !FMath::IsNearlyEqual(Result.X, Motion.X, 1e-5);
	const bool bHitZ = !FMath::IsNearlyEqual(Result.Z, Motion.Z, 1e-5);
	const bool bHitY = Result.Y != Motion.Y;
	Body.bHorizontalCollision = bHitX || bHitZ;
	Body.bOnGround = bHitY && Motion.Y < 0.0;
	if (bHitX)
	{
		Body.Velocity.X = 0.0;
	}
	if (bHitZ)
	{
		Body.Velocity.Z = 0.0;
	}
	if (bHitY)
	{
		Body.Velocity.Y = 0.0;
	}
}

void McPhysics::TickPlayer(const FMcBlockWorld& World, FMcBody& Body, const FMcMoveInput& Input)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::Abs(Body.Velocity[Axis]) < StopThreshold)
		{
			Body.Velocity[Axis] = 0.0;
		}
	}

	// Each key gives 0.98 of a full input; sneaking (on foot) slows it to 30%.
	double Forward = FMath::Clamp(Input.Forward, -1.0f, 1.0f) * 0.98;
	double Strafe = FMath::Clamp(Input.Strafe, -1.0f, 1.0f) * 0.98;
	const bool bSprinting = Input.bSprint && Input.Forward > 0.0f && !Input.bSneak;
	if (Input.bSneak && !Input.bFlying)
	{
		Forward *= SneakMultiplier;
		Strafe *= SneakMultiplier;
	}

	if (Input.bFlying)
	{
		const double Vertical = (Input.bJump ? 1.0 : 0.0) - (Input.bSneak ? 1.0 : 0.0);
		Body.Velocity.Y += Vertical * FlyingSpeed * 3.0;
		Accelerate(Body, Strafe, Forward, FlyingSpeed * (bSprinting ? 2.0 : 1.0), Input.Yaw);
		Move(World, Body, Body.Velocity, false);
		Body.Velocity.X *= AirDrag;
		Body.Velocity.Z *= AirDrag;
		Body.Velocity.Y *= FlyingVerticalDrag;
		return;
	}

	if (Input.bJump && Body.bOnGround)
	{
		const FIntVector Feet(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y - 0.5000001), FMath::FloorToInt32(Body.Position.Z));
		Body.Velocity.Y = JumpVelocity * FMcBlockShapes::Get().GetJumpFactor(World.GetBlock(Feet));
		if (bSprinting)
		{
			const double Radians = FMath::DegreesToRadians(static_cast<double>(Input.Yaw));
			Body.Velocity.X -= FMath::Sin(Radians) * SprintJumpBoost;
			Body.Velocity.Z += FMath::Cos(Radians) * SprintJumpBoost;
		}
	}

	const float Slipperiness = Body.bOnGround ? SlipperinessBelow(World, Body) : 1.0f;
	const double Friction = Slipperiness * AirDrag;
	double Acceleration;
	if (Body.bOnGround)
	{
		Acceleration = MovementSpeed * (bSprinting ? SprintMultiplier : 1.0) * GroundFactor / (Friction * Friction * Friction);
	}
	else
	{
		Acceleration = bSprinting ? SprintAirAcceleration : AirAcceleration;
	}
	Accelerate(Body, Strafe, Forward, Acceleration, Input.Yaw);

	const float SpeedFactor = SpeedFactorAt(World, Body);
	Move(World, Body, Body.Velocity * FVector(SpeedFactor, 1.0, SpeedFactor), Input.bSneak);

	Body.Velocity.Y = (Body.Velocity.Y - Gravity) * VerticalDrag;
	Body.Velocity.X *= Friction;
	Body.Velocity.Z *= Friction;
}
