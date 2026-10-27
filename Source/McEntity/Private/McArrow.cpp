#include "McArrow.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

const FName& FMcArrowEntity::StaticType()
{
	static const FName Name(TEXT("minecraft:arrow"));
	return Name;
}

FMcArrowEntity::FMcArrowEntity()
{
	Body.Width = 0.5f;
	Body.Height = 0.5f;
	Body.StepHeight = 0.0f;
}

void FMcArrowEntity::Tick(FMcEntityManager& Manager)
{
	++Age;
	if (bStuck)
	{
		if (++StuckTicks >= LifeTicks)
		{
			bRemoved = true;
		}
		return;
	}
	if (Age >= LifeTicks)
	{
		bRemoved = true;
		return;
	}

	FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	// In steps of a quarter block, so a fast arrow can't skip over a thin wall or a player.
	const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Body.Velocity.Size() / 0.25));
	const FVector Step = Body.Velocity / Steps;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		const FVector Next = Body.Position + Step;
		const FIntVector Cell(FMath::FloorToInt32(Next.X), FMath::FloorToInt32(Next.Y), FMath::FloorToInt32(Next.Z));
		if (!World.HasChunk(FIntPoint(Cell.X >> 4, Cell.Z >> 4)))
		{
			bRemoved = true;
			return;
		}
		const FMcStateId State = World.GetBlock(Cell);
		const FVector3f Local(Next.X - Cell.X, Next.Y - Cell.Y, Next.Z - Cell.Z);
		for (const FBox3f& Box : Shapes.GetCollision(State))
		{
			if (Box.IsInside(Local))
			{
				bStuck = true;
				Body.Velocity = FVector::ZeroVector;
				Body.Position = Next;
				Manager.PlaySound(FName("entity.arrow.hit"), Next, 1.0f, 1.2f / (Manager.GetRandom().FRand() * 0.2f + 0.9f));
				return;
			}
		}
		for (const FVector& Player : Manager.PlayerPositions)
		{
			// The player's box, a little fatter for the arrow's own width.
			const FBox PlayerBox(Player + FVector(-0.4, -0.1, -0.4), Player + FVector(0.4, 1.9, 0.4));
			if (PlayerBox.IsInside(Next))
			{
				Manager.HurtPlayer(Player, Damage);
				bRemoved = true;
				return;
			}
		}
		Body.Position = Next;
	}

	const bool bInWater = Shapes.GetFluid(World.GetBlock(FIntVector(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z)))) != EMcFluid::None;
	Body.Velocity *= bInWater ? 0.6 : Drag;
	Body.Velocity.Y -= Gravity;
	const double Flat = FMath::Sqrt(FMath::Square(Body.Velocity.X) + FMath::Square(Body.Velocity.Z));
	Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Body.Velocity.X, Body.Velocity.Z));
	Pitch = -FMath::RadiansToDegrees(FMath::Atan2(Body.Velocity.Y, Flat));
}
