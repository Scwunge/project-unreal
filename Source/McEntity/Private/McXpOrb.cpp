#include "McXpOrb.h"
#include "McBlockWorld.h"
#include "Serialization/Archive.h"

FMcXpOrb::FMcXpOrb()
{
	Body.Width = 0.5f;
	Body.Height = 0.5f;
	Body.StepHeight = 0.0f;
}

const FName& FMcXpOrb::StaticType()
{
	static const FName Name(TEXT("minecraft:experience_orb"));
	return Name;
}

bool FMcXpOrb::Serialize(FArchive& Ar)
{
	return FMcEntity::Serialize(Ar) && (Ar << Value, !Ar.IsError()) && (Ar.IsSaving() || Value > 0);
}

void FMcXpOrb::Tick(FMcEntityManager& Manager)
{
	FMcBlockWorld& World = Manager.GetWorld();
	if (++Age >= LifeTicks)
	{
		bRemoved = true;
		return;
	}
	if (!World.HasChunk(FIntPoint(FMath::FloorToInt32(Body.Position.X) >> 4, FMath::FloorToInt32(Body.Position.Z) >> 4)))
	{
		return;
	}
	Body.Velocity.Y -= 0.03;

	// Towards the nearest player within 8 blocks, harder the closer.
	const FVector* Nearest = nullptr;
	double NearestSquared = AttractRange * AttractRange;
	for (const FVector& Player : Manager.PlayerPositions)
	{
		const double Squared = FVector::DistSquared(Player + FVector(0.0, 0.8, 0.0), Body.Position);
		if (Squared < NearestSquared)
		{
			NearestSquared = Squared;
			Nearest = &Player;
		}
	}
	if (Nearest)
	{
		const FVector Offset = (*Nearest + FVector(0.0, 0.8, 0.0)) - Body.Position;
		const double Distance = Offset.Size() / AttractRange;
		const double Pull = 1.0 - Distance;
		if (Pull > 0.0)
		{
			Body.Velocity += Offset.GetSafeNormal() * (Pull * Pull * 0.1);
		}
	}
	McPhysics::Move(World, Body, Body.Velocity, false);
	double Friction = 0.98;
	if (Body.bOnGround)
	{
		Friction = 0.6 * 0.98;
		Body.Velocity.Y *= -0.9;
	}
	Body.Velocity.X *= Friction;
	Body.Velocity.Z *= Friction;
	Body.Velocity.Y *= 0.98;
}

void FMcXpOrb::Split(int32 Points, TArray<int32>& Out)
{
	static const int32 Sizes[] = { 2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1 };
	for (const int32 Size : Sizes)
	{
		while (Points >= Size)
		{
			Out.Add(Size);
			Points -= Size;
		}
	}
}

void FMcXpOrb::Spawn(FMcEntityManager& Manager, const FVector& Pos, int32 Points, FRandomStream& Random)
{
	TArray<int32> Sizes;
	Split(Points, Sizes);
	for (const int32 Size : Sizes)
	{
		TUniquePtr<FMcXpOrb> Orb = MakeUnique<FMcXpOrb>();
		Orb->Value = Size;
		Orb->Body.Position = Pos;
		Orb->Body.Velocity = FVector(Random.FRandRange(-0.1f, 0.1f), Random.FRandRange(0.0f, 0.2f), Random.FRandRange(-0.1f, 0.1f));
		Manager.Add(MoveTemp(Orb));
	}
}
