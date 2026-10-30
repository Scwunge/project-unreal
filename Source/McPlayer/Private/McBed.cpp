#include "McBed.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McPhysics.h"

namespace McBed
{
	namespace
	{
		// The bed's facing is the way from its foot to its head.
		bool GetDirection(FName Facing, FIntVector& Out)
		{
			if (Facing == TEXT("north")) { Out = FIntVector(0, 0, -1); return true; }
			if (Facing == TEXT("south")) { Out = FIntVector(0, 0, 1); return true; }
			if (Facing == TEXT("west")) { Out = FIntVector(-1, 0, 0); return true; }
			if (Facing == TEXT("east")) { Out = FIntVector(1, 0, 0); return true; }
			return false;
		}

		bool IsBedState(FMcStateId State)
		{
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			return Registry.IsValidState(State) && IsBed(Registry.GetBlockOf(State).Name.ToString());
		}

		// Where a standing player's feet may be: nothing in the way, and something under them.
		bool CanStand(const FMcBlockWorld& World, const FVector& Feet)
		{
			FMcBody Body;
			Body.Position = Feet;
			if (McPhysics::Collides(World, Body.GetBox()))
			{
				return false;
			}
			Body.Position = Feet - FVector(0.0, 0.2, 0.0);
			return McPhysics::Collides(World, Body.GetBox());
		}
	}

	bool IsBed(FStringView BlockName)
	{
		return BlockName.EndsWith(TEXT("_bed"));
	}

	bool CanSleepAt(int64 DayTime, bool bThundering)
	{
		const int64 Time = ((DayTime % 24000) + 24000) % 24000;
		return bThundering || (Time >= NightStart && Time <= NightEnd);
	}

	int64 GetMorning(int64 DayTime)
	{
		return (DayTime / 24000 + 1) * 24000;
	}

	TOptional<FIntVector> GetHead(const FMcBlockWorld& World, const FIntVector& Pos)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId State = World.GetBlock(Pos);
		FIntVector Direction;
		if (!IsBedState(State) || !GetDirection(Registry.GetPropertyValue(State, TEXT("facing")), Direction))
		{
			return {};
		}
		return Registry.GetPropertyValue(State, TEXT("part")) == TEXT("head") ? Pos : Pos + Direction;
	}

	FVector GetSleepPosition(const FIntVector& Head)
	{
		// The mattress is 9/16 high.
		return FVector(Head.X + 0.5, Head.Y + 0.5625, Head.Z + 0.5);
	}

	TOptional<float> GetSleepYaw(const FMcBlockWorld& World, const FIntVector& Head)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId State = World.GetBlock(Head);
		if (!IsBedState(State))
		{
			return {};
		}
		// Minecraft yaw: 0 faces south, 90 west, 180 north, 270 east.
		const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
		if (Facing == TEXT("south")) { return 0.0f; }
		if (Facing == TEXT("west")) { return 90.0f; }
		if (Facing == TEXT("north")) { return 180.0f; }
		if (Facing == TEXT("east")) { return 270.0f; }
		return {};
	}

	TOptional<FVector> FindStandPosition(const FMcBlockWorld& World, const FIntVector& Pos)
	{
		const TOptional<FIntVector> Head = GetHead(World, Pos);
		if (!Head.IsSet())
		{
			return {};
		}
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId HeadState = World.GetBlock(Head.GetValue());
		FIntVector Direction;
		GetDirection(Registry.GetPropertyValue(HeadState, TEXT("facing")), Direction);
		const FIntVector Foot = Head.GetValue() - Direction;

		// Beside the bed first (nearest the pillow), then round its ends and corners; a step up or down counts.
		for (int32 Ring = 1; Ring <= 2; ++Ring)
		{
			for (int32 DX = -Ring; DX <= Ring; ++DX)
			{
				for (int32 DZ = -Ring; DZ <= Ring; ++DZ)
				{
					if (FMath::Max(FMath::Abs(DX), FMath::Abs(DZ)) != Ring)
					{
						continue;
					}
					for (const FIntVector& Centre : { Head.GetValue(), Foot })
					{
						for (int32 DY : { 0, -1, 1 })
						{
							const FIntVector Cell = Centre + FIntVector(DX, DY, DZ);
							const FVector Feet(Cell.X + 0.5, Cell.Y, Cell.Z + 0.5);
							if (CanStand(World, Feet) && !IsBedState(World.GetBlock(Cell)))
							{
								return Feet;
							}
						}
					}
				}
			}
		}
		return {};
	}

	bool HasMonstersNear(const FMcEntityManager& Entities, const FIntVector& Pos)
	{
		const FVector Centre(Pos.X + 0.5, Pos.Y + 0.5, Pos.Z + 0.5);
		for (const TUniquePtr<FMcEntity>& Entity : Entities.GetEntities())
		{
			const FMcMobType* Type = Entity && !Entity->bRemoved ? FMcMobType::Find(Entity->GetType()) : nullptr;
			if (!Type || !Type->IsHostile() || Type->bNeutral)
			{
				continue;
			}
			const FVector Offset = Entity->Body.Position - Centre;
			if (FMath::Abs(Offset.X) <= 8.0 && FMath::Abs(Offset.Z) <= 8.0 && FMath::Abs(Offset.Y) <= 5.0)
			{
				return true;
			}
		}
		return false;
	}
}
