#include "McPotion.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McMob.h"

const FName& FMcThrownPotion::StaticType()
{
	static const FName Name(TEXT("minecraft:potion"));
	return Name;
}

FMcThrownPotion::FMcThrownPotion()
{
	Body.Width = 0.25f;
	Body.Height = 0.25f;
	Body.StepHeight = 0.0f;
}

void FMcThrownPotion::Break(FMcEntityManager& Manager)
{
	bRemoved = true;
	Manager.PlaySound(FName("entity.splash_potion.break"), Body.Position, 1.0f, 1.0f);
	const bool bHarming = Effect == FName(TEXT("harming"));
	for (const FVector& Player : Manager.PlayerPositions)
	{
		// The box the splash reaches is the player's grown by 4 across and 2 up; the nearer the middle, the stronger.
		const FVector Middle = Player + FVector(0.0, 0.9, 0.0);
		const double Distance = FVector::Dist(Middle, Body.Position);
		if (Distance >= SplashRadius)
		{
			continue;
		}
		const float Potency = static_cast<float>(1.0 - Distance / SplashRadius);
		if (bHarming)
		{
			Manager.HurtPlayer(Player, 6.0f * (Amplifier + 1) * Potency);
		}
		else if (Manager.OnPlayerEffect)
		{
			Manager.OnPlayerEffect(Player, Effect, Amplifier, FMath::Max(1, FMath::RoundToInt32(Duration * Potency)));
		}
	}
	if (!bHarming)
	{
		return;
	}
	for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
	{
		if (Other->bRemoved || !Other->IsLiving())
		{
			continue;
		}
		FMcMob& Mob = static_cast<FMcMob&>(*Other);
		if (Mob.Type.bIsRaider)
		{
			continue;
		}
		const double Distance = FVector::Dist(Mob.Body.Position + FVector(0.0, Mob.Type.Height * 0.5, 0.0), Body.Position);
		if (Distance < SplashRadius)
		{
			Mob.Damage(6.0f * (Amplifier + 1) * static_cast<float>(1.0 - Distance / SplashRadius), &Body.Position);
		}
	}
}

void FMcThrownPotion::Tick(FMcEntityManager& Manager)
{
	if (++Age >= LifeTicks)
	{
		Break(Manager);
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
				Break(Manager);
				return;
			}
		}
		for (const FVector& Player : Manager.PlayerPositions)
		{
			if (FBox(Player + FVector(-0.4, -0.1, -0.4), Player + FVector(0.4, 1.9, 0.4)).IsInside(Next))
			{
				Body.Position = Next;
				Break(Manager);
				return;
			}
		}
		Body.Position = Next;
	}
	Body.Velocity *= Drag;
	Body.Velocity.Y -= Gravity;
}
