#include "McFishing.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

namespace McFishing
{
	int32 RollWait(FRandomStream& Random, int32 Lure)
	{
		const int32 Cut = FMath::Max(0, Lure) * LureStep;
		return FMath::Max(1, Random.RandRange(MinWait - Cut, MaxWait - Cut));
	}

	int32 CountdownStep(FRandomStream& Random, bool bSky, bool bRaining)
	{
		if (!bSky && Random.FRand() < 0.5f)
		{
			return 0;
		}
		return bRaining && Random.FRand() < 0.25f ? 2 : 1;
	}

	bool IsOpenWater(const FMcBlockWorld& World, const FIntVector& Block)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		static const FName LilyPad(TEXT("minecraft:lily_pad"));
		static const FName Water(TEXT("minecraft:water"));
		enum class ELayer : uint8 { Unknown, Air, Water };
		for (int32 Y = -1; Y <= 2; ++Y)
		{
			ELayer Layer = ELayer::Unknown;
			for (int32 Z = -2; Z <= 2; ++Z)
			{
				for (int32 X = -2; X <= 2; ++X)
				{
					const FMcStateId State = World.GetBlock(Block + FIntVector(X, Y, Z));
					ELayer Here;
					if (Registry.IsAir(State) || Registry.GetBlockOf(State).Name == LilyPad)
					{
						Here = ELayer::Air;
					}
					else if ((Registry.GetBlockOf(State).Name == Water && Registry.GetPropertyValue(State, TEXT("level")) == TEXT("0"))
						|| (Registry.GetPropertyValue(State, TEXT("waterlogged")) == TEXT("true") && !Shapes.HasCollision(State)))
					{
						Here = ELayer::Water;
					}
					else
					{
						return false;
					}
					if (Layer != ELayer::Unknown && Here != Layer)
					{
						return false;
					}
					Layer = Here;
				}
			}
		}
		return true;
	}
}

const FName& FMcFishingBobber::StaticType()
{
	static const FName Name(TEXT("minecraft:fishing_bobber"));
	return Name;
}

FMcFishingBobber::FMcFishingBobber()
{
	Body.Width = 0.25f;
	Body.Height = 0.25f;
	Body.StepHeight = 0.0f;
}

void FMcFishingBobber::Cast(const FVector& Eye, float LookYaw, float LookPitch, FRandomStream& Random)
{
	const double YawRad = FMath::DegreesToRadians(static_cast<double>(LookYaw));
	const double PitchRad = FMath::DegreesToRadians(static_cast<double>(LookPitch));
	const FVector Look(-FMath::Sin(YawRad) * FMath::Cos(PitchRad), -FMath::Sin(PitchRad), FMath::Cos(YawRad) * FMath::Cos(PitchRad));
	Body.Position = Eye + Look * 0.3 - FVector(0.0, 0.1, 0.0);
	const auto Spread = [&Random]() { return (Random.FRand() - Random.FRand()) * 0.0103365; };
	Body.Velocity = (Look + FVector(Spread(), Spread(), Spread())) * 0.6;
	Yaw = LookYaw;
	Pitch = LookPitch;
}

TOptional<double> FMcFishingBobber::WaterSurface(const FMcBlockWorld& World) const
{
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FIntVector Cell(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z));
	for (const int32 Down : { 0, 1 })
	{
		FIntVector At = Cell - FIntVector(0, Down, 0);
		if (Shapes.GetFluid(World.GetBlock(At)) != EMcFluid::Water)
		{
			continue;
		}
		// Up to the top of the water in this column: a full block under more water, else the eight ninths a source fills.
		while (Shapes.GetFluid(World.GetBlock(At + FIntVector(0, 1, 0))) == EMcFluid::Water)
		{
			At.Y += 1;
		}
		return At.Y + 8.0 / 9.0;
	}
	return TOptional<double>();
}

FVector FMcFishingBobber::ThrowToward(const FVector& From, const FVector& To)
{
	const FVector Delta = To - From;
	return FVector(Delta.X * 0.1, Delta.Y * 0.1 + FMath::Sqrt(Delta.Size()) * 0.08, Delta.Z * 0.1);
}

void FMcFishingBobber::Tick(FMcEntityManager& Manager)
{
	++Age;
	FMcBlockWorld& World = Manager.GetWorld();
	FRandomStream& Random = Manager.GetRandom();
	if (!World.HasChunk(FIntPoint(FMath::FloorToInt32(Body.Position.X) >> 4, FMath::FloorToInt32(Body.Position.Z) >> 4)))
	{
		bRemoved = true;
		return;
	}

	if (State == EState::Hooked)
	{
		// Rides on what it caught, near the top of it, until that is gone.
		const FMcEntity* Target = Manager.Find(HookedId);
		if (Target && !Target->bRemoved)
		{
			Body.Position = Target->Body.Position + FVector(0.0, Target->Body.Height * 0.8, 0.0);
			Body.Velocity = Target->Body.Velocity;
			return;
		}
		State = EState::Flying;
		HookedId = 0;
	}

	const TOptional<double> Surface = WaterSurface(World);
	if (Surface.IsSet())
	{
		if (State != EState::Bobbing)
		{
			State = EState::Bobbing;
			WaitTicks = McFishing::RollWait(Random, Lure);
			GroundTime = 0;
		}
		// Floats with its middle at the surface, pulled under a little while a fish bites.
		const double Rest = *Surface - Body.Height * 0.5 - (BiteTicks > 0 ? 0.1 : 0.0);
		Body.Velocity.Y = Body.Velocity.Y * 0.9 + (Rest - Body.Position.Y) * 0.12;
		Body.Velocity.X *= 0.9;
		Body.Velocity.Z *= 0.9;
		McPhysics::Move(World, Body, Body.Velocity, false);

		const FIntVector Cell(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(*Surface - 0.5), FMath::FloorToInt32(Body.Position.Z));
		bOpenWater = McFishing::IsOpenWater(World, Cell);
		if (BiteTicks > 0)
		{
			if (--BiteTicks == 0)
			{
				// Missed: it waits again.
				WaitTicks = McFishing::RollWait(Random, Lure);
			}
		}
		else if (ApproachTicks > 0)
		{
			// The fish comes straight in, its wake shortening to the bobber.
			FishOffset *= static_cast<double>(ApproachTicks - 1) / ApproachTicks;
			if (--ApproachTicks == 0)
			{
				BiteTicks = Random.RandRange(McFishing::MinBite, McFishing::MaxBite);
				++Bites;
				FishOffset = FVector2D::ZeroVector;
				Body.Velocity.Y -= 0.2 * Random.FRandRange(0.6f, 1.0f);
				Manager.PlaySound(FName("entity.fishing_bobber.splash"), Body.Position, 0.25f, 1.0f + (Random.FRand() - Random.FRand()) * 0.4f);
			}
		}
		else
		{
			const FIntVector Above(Cell.X, Cell.Y + 1, Cell.Z);
			const bool bSky = Manager.IsSkyExposed(Above);
			WaitTicks -= McFishing::CountdownStep(Random, bSky, Manager.bRaining && bSky);
			if (WaitTicks <= 0)
			{
				ApproachTicks = Random.RandRange(McFishing::MinApproach, McFishing::MaxApproach);
				const double Angle = Random.FRand() * 2.0 * PI;
				FishOffset = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (ApproachTicks * 0.1);
			}
		}
		return;
	}

	// In the air: flying, or lying where it landed.
	if (State == EState::Bobbing)
	{
		State = EState::Flying;
		BiteTicks = ApproachTicks = 0;
		FishOffset = FVector2D::ZeroVector;
	}
	const FVector From = Body.Position;
	McPhysics::Move(World, Body, Body.Velocity, false);
	const bool bLanded = Body.bOnGround || Body.bHorizontalCollision;
	if (!bLanded)
	{
		// The first mob, vehicle or lying item it passes through takes the hook.
		TArray<FMcEntity*> Near;
		const FBox Swept = FBox(From - FVector(0.125, 0.0, 0.125), From + FVector(0.125, 0.25, 0.125)) + Body.GetBox();
		Manager.FindInBox(Swept.ExpandBy(0.1), Near);
		for (FMcEntity* Entity : Near)
		{
			if (Entity != this && !Entity->bRemoved && (Entity->IsAttackable() || Entity->GetType() == FMcItemEntity::StaticType()))
			{
				State = EState::Hooked;
				HookedId = Entity->Id;
				return;
			}
		}
	}
	if (bLanded)
	{
		Body.Velocity *= 0.5;
		if (++GroundTime >= McFishing::GroundTicks)
		{
			bRemoved = true;
			return;
		}
	}
	Body.Velocity.X *= 0.99;
	Body.Velocity.Z *= 0.99;
	Body.Velocity.Y = Body.Velocity.Y * 0.92 - 0.03;
	const double Flat = FMath::Sqrt(FMath::Square(Body.Velocity.X) + FMath::Square(Body.Velocity.Z));
	if (!bLanded)
	{
		Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Body.Velocity.X, Body.Velocity.Z));
		Pitch = -FMath::RadiansToDegrees(FMath::Atan2(Body.Velocity.Y, Flat));
	}
}

FMcFishingBobber::FReel FMcFishingBobber::Reel(FMcEntityManager& Manager)
{
	FReel Out;
	bRemoved = true;
	if (State == EState::Hooked)
	{
		if (FMcEntity* Target = Manager.Find(HookedId); Target && !Target->bRemoved)
		{
			Target->Body.Velocity += (AnglerEye - Target->Body.Position) * 0.1;
			Out.Damage = Target->GetType() == FMcItemEntity::StaticType() ? McFishing::ItemDamage : McFishing::EntityDamage;
		}
		return Out;
	}
	if (State == EState::Bobbing && BiteTicks > 0)
	{
		Out.bCatch = true;
		Out.Damage = McFishing::CatchDamage;
		return Out;
	}
	if (State == EState::Flying && GroundTime > 0)
	{
		Out.Damage = McFishing::GroundDamage;
	}
	return Out;
}
