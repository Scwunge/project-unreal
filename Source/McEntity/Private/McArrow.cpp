#include "McArrow.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McEntityManager.h"
#include "McMob.h"

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
				if (Manager.OnArrowHit)
				{
					Manager.OnArrowHit(Cell, Next, Body.Velocity);
				}
				Body.Velocity = FVector::ZeroVector;
				Body.Position = Next;
				Manager.PlaySound(FName("entity.arrow.hit"), Next, 1.0f, GetHitPitch(FMath::FRand()));
				return;
			}
		}
		if (PlayerBase > 0.0f)
		{
			// A mob in the way takes the hit.
			for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
			{
				if (!Other->IsLiving() || Other->bRemoved || !Other->Body.GetBox().ExpandBy(0.1).IsInside(Next))
				{
					continue;
				}
				float Hit = FMath::CeilToFloat(static_cast<float>(Body.Velocity.Size()) * PlayerBase);
				if (bCritical)
				{
					Hit += static_cast<float>(Manager.GetRandom().RandRange(0, static_cast<int32>(Hit / 2.0f) + 1));
				}
				const FVector From = Next - Body.Velocity.GetSafeNormal() * 2.0;
				static_cast<FMcMob*>(Other.Get())->Damage(Hit, &From);
				Manager.PlaySound(FName("entity.arrow.hit"), Next, 1.0f, GetHitPitch(FMath::FRand()));
				bRemoved = true;
				return;
			}
		}
		for (const FVector& Player : Manager.PlayerPositions)
		{
			// A player's own shot passes the players (the shooter stands in its way at first).
			if (PlayerBase > 0.0f)
			{
				break;
			}
			// The player's box, a little fatter for the arrow's own width.
			const FBox PlayerBox(Player + FVector(-0.4, -0.1, -0.4), Player + FVector(0.4, 1.9, 0.4));
			if (PlayerBox.IsInside(Next))
			{
				// The arrow's hit sounds on a body as on a block; the player's own hurt sound comes with the damage.
				Manager.PlaySound(FName("entity.arrow.hit"), Next, 1.0f, GetHitPitch(FMath::FRand()));
				Manager.HurtPlayer(Player, Damage);
				if (!Effect.IsNone() && Manager.OnPlayerEffect)
				{
					Manager.OnPlayerEffect(Player, Effect, 0, EffectDuration);
				}
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
