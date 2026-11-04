#include "McShulkerBullet.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

const FName& FMcShulkerBullet::StaticType()
{
	static const FName Name(TEXT("minecraft:shulker_bullet"));
	return Name;
}

FMcShulkerBullet::FMcShulkerBullet()
{
	Body.Width = 0.3125f;
	Body.Height = 0.3125f;
	Body.StepHeight = 0.0f;
}

float FMcShulkerBullet::SpeedAt(int32 LegTick)
{
	// From atk1.txt (bullet 203, speed at ticks 0 to 39).
	static const float Table[] = {
		0.0000f, 0.0307f, 0.0561f, 0.0772f, 0.0949f, 0.1098f, 0.1227f, 0.1338f, 0.1436f, 0.1523f, 0.1603f, 0.1676f, 0.1744f, 0.1809f, 0.1871f, 0.1931f,
		0.1990f, 0.2049f, 0.2107f, 0.2165f, 0.2224f, 0.2283f, 0.2343f, 0.2404f, 0.2465f, 0.2529f, 0.2593f, 0.2659f, 0.2726f, 0.2795f, 0.2865f, 0.2937f,
		0.3011f, 0.3086f, 0.3164f, 0.3243f, 0.3324f, 0.3407f, 0.3492f, 0.3580f };
	return Table[FMath::Clamp(LegTick, 0, UE_ARRAY_COUNT(Table) - 1)];
}

void FMcShulkerBullet::Tick(FMcEntityManager& Manager)
{
	if (++Age >= LifeTicks || Manager.PlayerPositions.Num() == 0)
	{
		bRemoved = true;
		return;
	}
	const FVector* Nearest = nullptr;
	double Best = 0.0;
	for (const FVector& Player : Manager.PlayerPositions)
	{
		const double Distance = FVector::DistSquared(Player, Body.Position);
		if (!Nearest || Distance < Best)
		{
			Nearest = &Player;
			Best = Distance;
		}
	}
	if (++LegTick >= LegLength)
	{
		LegTick = ResetTick;
		LegLength = LegTick + FMath::RandRange(24, 40);
	}
	const FVector Aim = *Nearest + FVector(0.0, 1.0, 0.0);
	Body.Velocity = (Aim - Body.Position).GetSafeNormal() * SpeedAt(LegTick);
	FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FVector Next = Body.Position + Body.Velocity;
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
			bRemoved = true;
			return;
		}
	}
	Body.Position = Next;
	for (const FVector& Player : Manager.PlayerPositions)
	{
		if (FBox(Player + FVector(-0.4, -0.1, -0.4), Player + FVector(0.4, 1.9, 0.4)).IsInside(Next))
		{
			Manager.HurtPlayer(Player, Damage);
			bRemoved = true;
			return;
		}
	}
}
