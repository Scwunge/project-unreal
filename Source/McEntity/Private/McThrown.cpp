#include "McThrown.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McMob.h"

FName FMcThrownEntity::GetTypeName(EKind Kind)
{
	static const FName Snowball(TEXT("minecraft:snowball")), Egg(TEXT("minecraft:egg")), Pearl(TEXT("minecraft:ender_pearl"));
	return Kind == EKind::Snowball ? Snowball : Kind == EKind::Egg ? Egg : Pearl;
}

bool FMcThrownEntity::FindKind(FName Item, EKind& Out)
{
	for (const EKind Candidate : { EKind::Snowball, EKind::Egg, EKind::EnderPearl })
	{
		if (GetTypeName(Candidate) == Item)
		{
			Out = Candidate;
			return true;
		}
	}
	return false;
}

int32 FMcThrownEntity::GetChickCount(float Roll, float SecondRoll)
{
	if (Roll >= 1.0f / 8.0f)
	{
		return 0;
	}
	return SecondRoll < 1.0f / 32.0f ? 4 : 1;
}

FMcThrownEntity::FMcThrownEntity(EKind InKind)
	: Kind(InKind)
{
	Body.Width = 0.25f;
	Body.Height = 0.25f;
	Body.StepHeight = 0.0f;
}

void FMcThrownEntity::Break(FMcEntityManager& Manager, const FVector& At, FMcEntity* Hit)
{
	bRemoved = true;
	FMcMob* Mob = Hit && Hit->IsLiving() ? static_cast<FMcMob*>(Hit) : nullptr;
	const FVector From = At - Body.Velocity.GetSafeNormal();
	if (Kind == EKind::EnderPearl)
	{
		if (OnPearlLanded)
		{
			OnPearlLanded(At);
		}
		Manager.PlaySound(FName("entity.player.teleport"), At, 1.0f, 1.0f);
		return;
	}
	if (Mob)
	{
		// A blaze is the one thing a snowball hurts.
		Mob->Damage(Kind == EKind::Snowball && Mob->Type.Name == TEXT("minecraft:blaze") ? 3.0f : 0.0f, &From);
	}
	if (Kind == EKind::Egg)
	{
		const int32 Chicks = GetChickCount(Manager.GetRandom().FRand(), Manager.GetRandom().FRand());
		if (const FMcMobType* Type = FMcMobType::Find(TEXT("minecraft:chicken")))
		{
			for (int32 Index = 0; Index < Chicks; ++Index)
			{
				TUniquePtr<FMcMob> Chick = MakeUnique<FMcMob>(*Type);
				Chick->MakeBaby();
				Chick->Body.Position = At + FVector((Manager.GetRandom().FRand() - 0.5) * 0.3, 0.0, (Manager.GetRandom().FRand() - 0.5) * 0.3);
				Manager.Add(MoveTemp(Chick));
			}
		}
	}
}

void FMcThrownEntity::Tick(FMcEntityManager& Manager)
{
	if (++Age >= LifeTicks)
	{
		bRemoved = true;
		return;
	}
	FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	// In steps of a quarter block, so it can't skip over a thin wall or a mob.
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
				// A pearl comes down on the block's top or beside it: the last place it was in the open.
				Break(Manager, Body.Position, nullptr);
				return;
			}
		}
		for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
		{
			if (Other.Get() != this && Other->IsLiving() && !Other->bRemoved && Other->Body.GetBox().ExpandBy(0.1).IsInside(Next))
			{
				Break(Manager, Next, Other.Get());
				return;
			}
		}
		Body.Position = Next;
	}
	const FIntVector Here(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z));
	const bool bInWater = Shapes.GetFluid(World.GetBlock(Here)) != EMcFluid::None;
	Body.Velocity *= bInWater ? 0.8 : Drag;
	Body.Velocity.Y -= Gravity;
}
