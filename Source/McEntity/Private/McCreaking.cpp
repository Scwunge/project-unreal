// The creaking: see McCreaking.h.

#include "McCreaking.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "Serialization/Archive.h"

namespace
{
	// Whether no block with collision stands between the two points.
	bool ClearBetween(const FMcBlockWorld& World, const FVector& From, const FVector& To)
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		const FVector Way = To - From;
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Way.Size() / 0.25));
		for (int32 Step = 1; Step < Steps; ++Step)
		{
			const FVector At = From + Way * (static_cast<double>(Step) / Steps);
			const FIntVector Cell(FMath::FloorToInt32(At.X), FMath::FloorToInt32(At.Y), FMath::FloorToInt32(At.Z));
			if (!World.HasChunk(FIntPoint(Cell.X >> 4, Cell.Z >> 4)) || Shapes.HasCollision(World.GetBlock(Cell)))
			{
				return false;
			}
		}
		return true;
	}
}

float McCreaking::GetBiteDamage(int32 Difficulty)
{
	return Difficulty <= 1 ? 2.5f : (Difficulty >= 3 ? 4.0f : 3.0f);
}

const FName& FMcCreaking::StaticType()
{
	static const FName Name(TEXT("minecraft:creaking"));
	return Name;
}

FMcCreaking::FMcCreaking(const FMcMobType& InType)
	: FMcMob(InType)
{
}

void FMcCreaking::LinkTo(const FIntVector& InHeart)
{
	Heart = InHeart;
	bLinked = true;
	bInvulnerable = true;
	bPersistent = true;
	OnShielded = [this](FMcMob&, const FVector*)
	{
		if (Owner)
		{
			Owner->PlaySound(FName("entity.creaking.sway"), Body.Position, 1.0f, 1.0f);
		}
		if (OnHit)
		{
			OnHit(Heart);
		}
	};
}

double FMcCreaking::GetHeartDistance() const
{
	if (!bLinked)
	{
		return -1.0;
	}
	const FVector Centre(Heart.X + 0.5, Heart.Y + 0.5, Heart.Z + 0.5);
	return FVector::Dist(Body.Position, Centre);
}

bool FMcCreaking::Serialize(FArchive& Ar)
{
	if (!FMcMob::Serialize(Ar))
	{
		return false;
	}
	// Older saves end after the mob's own part.
	if (!Ar.IsLoading() || !Ar.AtEnd())
	{
		Ar << bLinked << Heart.X << Heart.Y << Heart.Z;
		if (Ar.IsLoading() && bLinked)
		{
			LinkTo(Heart);
		}
	}
	return !Ar.IsError();
}

void FMcCreaking::Tick(FMcEntityManager& Manager)
{
	const FMcBlockWorld& World = Manager.GetWorld();
	if (bLinked && GetHeartDistance() > McCreaking::LeashRange)
	{
		// Out of the heart's reach: gone, and the heart makes another.
		bRemoved = true;
		if (OnLost)
		{
			OnLost(Heart);
		}
		return;
	}

	// Who is looking at it, and who is close enough to alert it.
	const FVector Eye = Body.Position + FVector(0.0, Body.Height * 0.9, 0.0);
	bObserved = false;
	bool bAnyoneNear = false;
	bool bSomeoneAlerting = false;
	FVector Nearest = FVector::ZeroVector;
	double NearestDist = TNumericLimits<double>::Max();
	bool bSharing = false;
	for (const FVector& Player : Manager.PlayerPositions)
	{
		const double Distance = FVector::Dist(Player, Body.Position);
		if (Distance < McCreaking::LoseRange)
		{
			bAnyoneNear = true;
			if (Distance < NearestDist)
			{
				NearestDist = Distance;
				Nearest = Player;
			}
		}
		const bool bLooks = Manager.PlayerLooksAt && Distance < McCreaking::LoseRange && Manager.PlayerLooksAt(Player, Eye)
			&& ClearBetween(World, Player + FVector(0.0, 1.62, 0.0), Eye);
		if (bLooks)
		{
			bObserved = true;
			if (Distance <= McCreaking::AlertRange)
			{
				bSomeoneAlerting = true;
			}
		}
		if (FMath::FloorToInt32(Player.X) == FMath::FloorToInt32(Body.Position.X) && FMath::FloorToInt32(Player.Z) == FMath::FloorToInt32(Body.Position.Z)
			&& FMath::Abs(Player.Y - Body.Position.Y) < 1.0)
		{
			bSharing = true;
		}
	}
	if (bSomeoneAlerting && !bAlerted)
	{
		bAlerted = true;
		Manager.PlaySound(FName("entity.creaking.activate"), Body.Position, 1.0f, 1.0f);
	}
	else if (bAlerted && !bAnyoneNear)
	{
		bAlerted = false;
		Manager.PlaySound(FName("entity.creaking.deactivate"), Body.Position, 1.0f, 1.0f);
	}
	SameBlock = bSharing ? SameBlock + 1 : 0;
	if (bLinked && SameBlock > McCreaking::SameBlockTicks)
	{
		bRemoved = true;
		if (OnLost)
		{
			OnLost(Heart);
		}
		return;
	}

	BiteWait = FMath::Max(0, BiteWait - 1);
	Goal.Reset();
	bStayPut = true;
	if (bObserved)
	{
		// Frozen: no walking, no pushing.
		Body.Velocity.X = 0.0;
		Body.Velocity.Z = 0.0;
	}
	else if (bAlerted && bAnyoneNear)
	{
		bStayPut = false;
		Goal = Nearest;
		const FVector Away = Nearest - Body.Position;
		if (FVector2D(Away.X, Away.Z).Size() < 1.6 && FMath::Abs(Away.Y) < 2.0 && BiteWait == 0 && OnAttackPlayer)
		{
			BiteWait = McCreaking::BiteIntervalTicks;
			Manager.PlaySound(FName("entity.creaking.attack"), Body.Position, 1.0f, 1.0f);
			OnAttackPlayer(Nearest, McCreaking::GetBiteDamage(Manager.Difficulty));
		}
	}
	FMcMob::Tick(Manager);
	if (bObserved)
	{
		// (The mob's own tick may have started it walking.)
		Body.Velocity.X = 0.0;
		Body.Velocity.Z = 0.0;
	}
}
