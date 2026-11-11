#include "McLeash.h"
#include "McMob.h"
#include "McVehicle.h"

const FName& FMcLeashKnot::StaticType()
{
	static const FName Name(TEXT("minecraft:leash_knot"));
	return Name;
}

namespace
{
	FVector GetKnotPosition(const FIntVector& Cell)
	{
		return FVector(Cell.X + 0.5, Cell.Y + 0.375, Cell.Z + 0.5);
	}
}

bool McLeash::CanLeash(const FMcEntity& Entity)
{
	if (Entity.IsLiving())
	{
		return !static_cast<const FMcMob&>(Entity).Type.IsHostile();
	}
	return Entity.IsVehicle() && !static_cast<const FMcVehicle&>(Entity).RunsOnRails();
}

bool McLeash::Attach(FMcEntity& Entity, const FVector& PlayerFeet)
{
	if (Entity.bLeashed || !CanLeash(Entity))
	{
		return false;
	}
	Entity.bLeashed = true;
	Entity.LeashKnot = 0;
	Entity.LeashPlayerPos = PlayerFeet;
	return true;
}

void McLeash::Detach(FMcEntityManager& Manager, FMcEntity& Entity, bool bDrop)
{
	if (!Entity.bLeashed)
	{
		return;
	}
	Entity.bLeashed = false;
	Entity.LeashKnot = 0;
	if (bDrop)
	{
		Manager.DropItem(Entity.Body.Position + FVector(0.0, 0.5, 0.0), FMcItemStack(FName(TEXT("minecraft:lead")), 1));
	}
}

void McLeash::Tick(FMcEntityManager& Manager)
{
	// A knot nothing hangs from goes.
	TSet<int32> Used;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->bLeashed && !Entity->bRemoved && Entity->LeashKnot != 0)
		{
			Used.Add(Entity->LeashKnot);
		}
	}
	// Entities that are gone take their lead with them (dropped by whoever removed them).
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (!Entity->bRemoved && Entity->GetType() == FMcLeashKnot::StaticType() && !Used.Contains(Entity->Id))
		{
			Entity->bRemoved = true;
		}
	}
	// Snapped leads drop an item, which adds to the list: walk a copy of the pointers.
	TArray<FMcEntity*> Tied;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->bLeashed && !Entity->bRemoved)
		{
			Tied.Add(Entity.Get());
		}
	}
	for (FMcEntity* Entity : Tied)
	{
		FVector Holder = Entity->LeashPlayerPos;
		bool bFound = false;
		if (Entity->LeashKnot != 0)
		{
			if (const FMcEntity* Knot = Manager.Find(Entity->LeashKnot))
			{
				Holder = Knot->Body.Position;
				bFound = !Knot->bRemoved;
			}
		}
		else
		{
			// The player: whoever is nearest to where they were, a few blocks at most.
			double Best = 16.0;
			for (const FVector& Player : Manager.PlayerPositions)
			{
				const double D = FVector::DistSquared(Player, Entity->LeashPlayerPos);
				if (D < Best)
				{
					Best = D;
					Holder = Player;
					bFound = true;
				}
			}
			Entity->LeashPlayerPos = Holder;
		}
		if (!bFound)
		{
			Detach(Manager, *Entity);
			continue;
		}
		const FVector To = Holder - Entity->Body.Position;
		const double Distance = To.Size();
		if (Distance > SnapDistance)
		{
			Detach(Manager, *Entity);
			continue;
		}
		if (Distance < 1e-6)
		{
			continue;
		}
		const FVector Direction = To / Distance;
		double Push = 0.0;
		if (Entity->IsLiving() && Distance > FollowDistance)
		{
			Push += FollowPush;
		}
		if (Distance > PullDistance)
		{
			Push += FollowPush + PullPush * (Distance - PullDistance);
		}
		Entity->Body.Velocity.X += Direction.X * Push;
		Entity->Body.Velocity.Z += Direction.Z * Push;
	}
}

bool McLeash::UseFence(FMcEntityManager& Manager, const FIntVector& Cell, const FVector& PlayerFeet, bool bHoldingLead)
{
	const FVector KnotPos = GetKnotPosition(Cell);
	FMcEntity* Knot = nullptr;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (!Entity->bRemoved && Entity->GetType() == FMcLeashKnot::StaticType() && FVector::DistSquared(Entity->Body.Position, KnotPos) < 0.01)
		{
			Knot = Entity.Get();
		}
	}
	TArray<FMcEntity*> Led;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->bLeashed && !Entity->bRemoved && Entity->LeashKnot == 0 && FVector::DistSquared(Entity->Body.Position, PlayerFeet) <= TieRadius * TieRadius)
		{
			Led.Add(Entity.Get());
		}
	}
	if (Led.IsEmpty())
	{
		// With nothing led, the knot gives its mobs back to the player.
		if (!Knot)
		{
			return false;
		}
		bool bAny = false;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (Entity->bLeashed && Entity->LeashKnot == Knot->Id)
			{
				Entity->LeashKnot = 0;
				Entity->LeashPlayerPos = PlayerFeet;
				bAny = true;
			}
		}
		Knot->bRemoved = true;
		return bAny;
	}
	if (!bHoldingLead)
	{
		return false;
	}
	if (!Knot)
	{
		FMcLeashKnot* Made = new FMcLeashKnot();
		Made->Body.Position = KnotPos;
		Knot = &Manager.Add(TUniquePtr<FMcEntity>(Made));
	}
	for (FMcEntity* Entity : Led)
	{
		Entity->LeashKnot = Knot->Id;
	}
	return true;
}
