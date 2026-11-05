#include "McFireball.h"
#include "McMob.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

const FName& FMcFireball::GhastType()
{
	static const FName Name(TEXT("minecraft:fireball"));
	return Name;
}

const FName& FMcFireball::SmallType()
{
	static const FName Name(TEXT("minecraft:small_fireball"));
	return Name;
}

const FName& FMcFireball::SkullType()
{
	static const FName Name(TEXT("minecraft:wither_skull"));
	return Name;
}

const FName& FMcFireball::DragonType()
{
	static const FName Name(TEXT("minecraft:dragon_fireball"));
	return Name;
}

void FMcFireball::LaunchDragon(const FVector& Direction)
{
	bDragon = true;
	Aim = Direction.GetSafeNormal();
	Body.Velocity = Aim * Acceleration;
}

void FMcFireball::LaunchSkull(const FVector& Direction)
{
	bSkull = true;
	Aim = Direction.GetSafeNormal();
	Body.Velocity = Aim * 0.1;
	Body.Width = 0.3125f;
	Body.Height = 0.3125f;
}

FMcFireball::FMcFireball()
{
	Body.Width = 1.0f;
	Body.Height = 1.0f;
	Body.StepHeight = 0.0f;
}

void FMcFireball::Launch(const FVector& Direction, bool bInSmall)
{
	bSmall = bInSmall;
	Aim = Direction.GetSafeNormal();
	Body.Velocity = Aim * Acceleration;
	if (bSmall)
	{
		Body.Width = 0.3125f;
		Body.Height = 0.3125f;
	}
}

void FMcFireball::Hit(FMcEntityManager& Manager, const FVector* PlayerFeet)
{
	bRemoved = true;
	if (bDragon)
	{
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (!Entity->bRemoved && Entity->GetType() == FName(TEXT("minecraft:ender_dragon")))
			{
				static_cast<FMcMob&>(*Entity).DragonClouds.Add(FVector4(Body.Position.X, Body.Position.Y, Body.Position.Z, 600.0));
				break;
			}
		}
		return;
	}
	if (bSmall)
	{
		if (PlayerFeet)
		{
			Manager.HurtPlayer(*PlayerFeet, SmallDamage);
		}
		return;
	}
	Manager.Explode(Body.Position, ExplosionPower);
}

void FMcFireball::Tick(FMcEntityManager& Manager)
{
	if (++Age >= LifeTicks)
	{
		bRemoved = true;
		return;
	}
	FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
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
		const FVector3f Local(Next.X - Cell.X, Next.Y - Cell.Y, Next.Z - Cell.Z);
		for (const FBox3f& Box : Shapes.GetCollision(World.GetBlock(Cell)))
		{
			if (Box.IsInside(Local))
			{
				Body.Position = Next;
				Hit(Manager, nullptr);
				return;
			}
		}
		for (const FVector& Player : Manager.PlayerPositions)
		{
			if (FBox(Player + FVector(-0.4, -0.1, -0.4), Player + FVector(0.4, 1.9, 0.4)).IsInside(Next))
			{
				Body.Position = Next;
				Hit(Manager, &Player);
				return;
			}
		}
		Body.Position = Next;
	}
	Body.Velocity = bSkull ? (Body.Velocity + Aim * 0.1) * 0.73 : (Body.Velocity + Aim * Acceleration) * Inertia;
}
