#include "McPhysics.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

namespace
{
	// Gravity and air drag on y; drag for x and z in the air, and the extra factor slipperiness multiplies.
	constexpr double Gravity = 0.08;
	constexpr double VerticalDrag = 0.98;
	constexpr double AirDrag = 0.91;
	// Walking speed attribute, and how much of it reaches the ground: 0.6 slipperiness gives 0.546 per tick.
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
	// Swimming: drag per tick, input acceleration, sinking (a quarter of gravity), and the upward kick of Jump.
	constexpr double WaterDrag = 0.8;
	constexpr double LavaDrag = 0.5;
	constexpr double FluidAcceleration = 0.02;
	constexpr double FluidSinking = Gravity / 4.0;
	constexpr double SwimUp = 0.04;
	// Flowing fluid nudges bodies by this much per tick.
	constexpr double WaterPush = 0.014;
	constexpr double LavaPush = 0.007;
	// Pushing against a ledge from the water lifts you out of it.
	constexpr double LedgeHop = 0.3;
	// Ladders and vines.
	constexpr double ClimbSpeed = 0.15;
	constexpr double ClimbUp = 0.2;

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

	// How deep in a fluid a body is and which way the fluid pushes it.
	struct FFluidContact
	{
		bool bIn = false;
		double Depth = 0.0;  // how far the surface is above the body's feet
		FVector2D Flow = FVector2D::ZeroVector;
	};

	// The height of a fluid block's surface: a source is 8/9, flowing fluid 1/9 less per level, and a block with
	// the same fluid above it is full.
	double SurfaceHeight(const FMcBlockWorld& World, const FIntVector& Cell, EMcFluid Kind)
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		if (Shapes.GetFluid(World.GetBlock(Cell + FIntVector(0, 1, 0))) == Kind)
		{
			return 1.0;
		}
		const int32 Level = Shapes.GetFluidLevel(World.GetBlock(Cell));
		return Level >= 8 ? 8.0 / 9.0 : (8 - Level) / 9.0;
	}

	// Which way a fluid block pushes, from its neighbours' surfaces: downhill, and off an edge into a drop.
	FVector2D FlowAt(const FMcBlockWorld& World, const FIntVector& Cell, EMcFluid Kind)
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		static const FIntVector Sides[4] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };
		const double Own = SurfaceHeight(World, Cell, Kind);
		FVector2D Flow = FVector2D::ZeroVector;
		for (const FIntVector& Side : Sides)
		{
			const FIntVector Next = Cell + Side;
			const FMcStateId Neighbour = World.GetBlock(Next);
			double Other;
			if (Shapes.GetFluid(Neighbour) == Kind)
			{
				Other = SurfaceHeight(World, Next, Kind);
			}
			else if (Shapes.HasCollision(Neighbour) || Shapes.GetFluid(World.GetBlock(Next - FIntVector(0, 1, 0))) != Kind)
			{
				continue;
			}
			else
			{
				Other = 0.0;
			}
			Flow += FVector2D(Side.X, Side.Z) * (Own - Other);
		}
		return Flow;
	}

	FFluidContact SampleFluid(const FMcBlockWorld& World, const FBox& Box, EMcFluid Kind)
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		const FBox Inner = Box.ExpandBy(-0.001);
		FFluidContact Contact;
		for (int32 Y = FMath::FloorToInt32(Inner.Min.Y); Y <= FMath::FloorToInt32(Inner.Max.Y); ++Y)
		{
			for (int32 Z = FMath::FloorToInt32(Inner.Min.Z); Z <= FMath::FloorToInt32(Inner.Max.Z); ++Z)
			{
				for (int32 X = FMath::FloorToInt32(Inner.Min.X); X <= FMath::FloorToInt32(Inner.Max.X); ++X)
				{
					const FIntVector Cell(X, Y, Z);
					if (Shapes.GetFluid(World.GetBlock(Cell)) != Kind)
					{
						continue;
					}
					const double Surface = Y + SurfaceHeight(World, Cell, Kind);
					if (Surface > Inner.Min.Y)
					{
						Contact.bIn = true;
						Contact.Depth = FMath::Max(Contact.Depth, Surface - Box.Min.Y);
						Contact.Flow += FlowAt(World, Cell, Kind);
					}
				}
			}
		}
		return Contact;
	}

	// The strongest tangling of the blocks the box is in (all 1 when it is in none).
	FVector StuckIn(const FMcBlockWorld& World, const FBox& Box)
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		const FBox Inner = Box.ExpandBy(-0.001);
		FVector Multiplier(1.0, 1.0, 1.0);
		for (int32 Y = FMath::FloorToInt32(Inner.Min.Y); Y <= FMath::FloorToInt32(Inner.Max.Y); ++Y)
		{
			for (int32 Z = FMath::FloorToInt32(Inner.Min.Z); Z <= FMath::FloorToInt32(Inner.Max.Z); ++Z)
			{
				for (int32 X = FMath::FloorToInt32(Inner.Min.X); X <= FMath::FloorToInt32(Inner.Max.X); ++X)
				{
					const FVector Here(Shapes.GetStuckMultiplier(World.GetBlock(FIntVector(X, Y, Z))));
					Multiplier = FVector(FMath::Min(Multiplier.X, Here.X), FMath::Min(Multiplier.Y, Here.Y), FMath::Min(Multiplier.Z, Here.Z));
				}
			}
		}
		return Multiplier;
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

bool McPhysics::BubbleColumnPush(const FMcBlockWorld& World, const FBox& Box, FVector& Velocity, bool* bOutDown)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	auto IsAir = [&](FMcStateId State)
	{
		if (!Registry.IsValidState(State))
		{
			return true;
		}
		const FName Name = Registry.GetBlockOf(State).Name;
		return Name == TEXT("minecraft:air") || Name == TEXT("minecraft:cave_air") || Name == TEXT("minecraft:void_air");
	};
	// Once for each column block the box overlaps (a box on a block boundary is pushed twice).
	const FBox Inner = Box.ExpandBy(-1e-5);
	bool bTouched = false;
	for (int32 Y = FMath::FloorToInt32(Inner.Min.Y); Y <= FMath::FloorToInt32(Inner.Max.Y); ++Y)
	{
		for (int32 Z = FMath::FloorToInt32(Inner.Min.Z); Z <= FMath::FloorToInt32(Inner.Max.Z); ++Z)
		{
			for (int32 X = FMath::FloorToInt32(Inner.Min.X); X <= FMath::FloorToInt32(Inner.Max.X); ++X)
			{
				const FMcStateId State = World.GetBlock(FIntVector(X, Y, Z));
				if (!Registry.IsValidState(State) || Registry.GetBlockOf(State).Name != TEXT("minecraft:bubble_column"))
				{
					continue;
				}
				bTouched = true;
				const bool bDown = Registry.GetPropertyValue(State, TEXT("drag")) == TEXT("true");
				if (bOutDown)
				{
					*bOutDown = bDown;
				}
				if (IsAir(World.GetBlock(FIntVector(X, Y + 1, Z))))
				{
					// The top of the column throws things further than the inside does.
					Velocity.Y = bDown ? FMath::Max(-0.9, Velocity.Y - 0.03) : FMath::Min(1.8, Velocity.Y + 0.1);
				}
				else
				{
					Velocity.Y = bDown ? FMath::Max(-0.3, Velocity.Y - 0.03) : FMath::Min(0.7, Velocity.Y + 0.06);
				}
			}
		}
	}
	return bTouched;
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
		Body.bInBubbleColumn = false;
		const double Vertical = (Input.bJump ? 1.0 : 0.0) - (Input.bSneak ? 1.0 : 0.0);
		Body.Velocity.Y += Vertical * FlyingSpeed * 3.0;
		Accelerate(Body, Strafe, Forward, FlyingSpeed * (bSprinting ? 2.0 : 1.0), Input.Yaw);
		Move(World, Body, Body.Velocity, false);
		Body.Velocity.X *= AirDrag;
		Body.Velocity.Z *= AirDrag;
		Body.Velocity.Y *= FlyingVerticalDrag;
		return;
	}

	// What the body is in.
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FBox Box = Body.GetBox();
	const FFluidContact Water = SampleFluid(World, Box, EMcFluid::Water);
	const FFluidContact Lava = Water.bIn ? FFluidContact() : SampleFluid(World, Box, EMcFluid::Lava);
	const FFluidContact& Fluid = Water.bIn ? Water : Lava;
	Body.bInWater = Water.bIn;
	Body.bInLava = Lava.bIn;
	const FVector Stuck = StuckIn(World, Box);
	Body.bStuck = !Stuck.Equals(FVector(1.0, 1.0, 1.0), 1e-9);
	Body.bClimbing = !Fluid.bIn && Shapes.IsClimbable(World.GetBlock(FIntVector(
		FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z))));

	if (Fluid.bIn && !Fluid.Flow.IsNearlyZero(1e-9))
	{
		const FVector2D Push = Fluid.Flow.GetSafeNormal() * (Water.bIn ? WaterPush : LavaPush);
		Body.Velocity.X += Push.X;
		Body.Velocity.Z += Push.Y;
	}

	if (Input.bJump && Fluid.bIn && (!Body.bOnGround || Fluid.Depth > 0.4))
	{
		Body.Velocity.Y += SwimUp;
	}
	else if (Input.bJump && Body.bOnGround)
	{
		const FIntVector Feet(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y - 0.5000001), FMath::FloorToInt32(Body.Position.Z));
		Body.Velocity.Y = JumpVelocity * Shapes.GetJumpFactor(World.GetBlock(Feet)) + Input.JumpBonus;
		if (bSprinting)
		{
			const double Radians = FMath::DegreesToRadians(static_cast<double>(Input.Yaw));
			Body.Velocity.X -= FMath::Sin(Radians) * SprintJumpBoost;
			Body.Velocity.Z += FMath::Cos(Radians) * SprintJumpBoost;
		}
	}

	if (Fluid.bIn)
	{
		if (Input.bSneak && !Input.bJump)
		{
			Body.Velocity.Y -= SwimUp;
		}
		Accelerate(Body, Strafe, Forward, FluidAcceleration, Input.Yaw);
		const FVector Before = Body.Velocity;
		Move(World, Body, Body.Velocity, false);

		// Against a ledge with room above it, hop out.
		const EMcFluid Kind = Water.bIn ? EMcFluid::Water : EMcFluid::Lava;
		const FBox Raised = Body.GetBox().ShiftBy(FVector(Before.X, Before.Y + 0.6, Before.Z));
		if (Body.bHorizontalCollision && !Collides(World, Raised) && !SampleFluid(World, Raised, Kind).bIn)
		{
			Body.Velocity.Y = LedgeHop;
		}

		const double Drag = Water.bIn ? WaterDrag : LavaDrag;
		Body.Velocity.X *= Drag;
		Body.Velocity.Z *= Drag;
		Body.Velocity.Y = Body.Velocity.Y * Drag - FluidSinking;
		Body.bInBubbleColumn = BubbleColumnPush(World, Body.GetBox(), Body.Velocity, &Body.bBubbleDown);
		return;
	}

	const float Slipperiness = Body.bOnGround ? SlipperinessBelow(World, Body) : 1.0f;
	const double Friction = Slipperiness * AirDrag;
	double Acceleration;
	if (Body.bOnGround)
	{
		Acceleration = Input.Speed * (bSprinting ? SprintMultiplier : 1.0) * GroundFactor / (Friction * Friction * Friction);
	}
	else
	{
		Acceleration = bSprinting ? SprintAirAcceleration : AirAcceleration;
	}
	Accelerate(Body, Strafe, Forward, Acceleration, Input.Yaw);

	if (Body.bClimbing)
	{
		// Slow on the way down and sideways; sneaking holds you where you are.
		Body.Velocity.X = FMath::Clamp(Body.Velocity.X, -ClimbSpeed, ClimbSpeed);
		Body.Velocity.Z = FMath::Clamp(Body.Velocity.Z, -ClimbSpeed, ClimbSpeed);
		Body.Velocity.Y = FMath::Max(Body.Velocity.Y, -ClimbSpeed);
		if (Input.bSneak && Body.Velocity.Y < 0.0)
		{
			Body.Velocity.Y = 0.0;
		}
	}

	const float SpeedFactor = SpeedFactorAt(World, Body);
	FVector Motion = Body.Velocity * FVector(SpeedFactor, 1.0, SpeedFactor);
	if (Body.bStuck)
	{
		Motion *= Stuck;
	}
	Move(World, Body, Motion, Input.bSneak);
	if (Body.bStuck)
	{
		// Whatever speed you had is gone once the web has held you.
		Body.Velocity = FVector::ZeroVector;
	}
	if (Body.bClimbing && (Body.bHorizontalCollision || Input.bJump))
	{
		Body.Velocity.Y = ClimbUp;
	}

	Body.Velocity.Y = (Body.Velocity.Y - Gravity) * VerticalDrag;
	Body.Velocity.X *= Friction;
	Body.Velocity.Z *= Friction;
	Body.bInBubbleColumn = BubbleColumnPush(World, Body.GetBox(), Body.Velocity, &Body.bBubbleDown);
}
