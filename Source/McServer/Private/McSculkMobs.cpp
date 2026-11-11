#include "McSculkMobs.h"
#include "McEntityHost.h"
#include "McSimulation.h"
#include "McGameEvent.h"
#include "McWarden.h"
#include "McCreaking.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

namespace
{
	FVector Centre(const FIntVector& Cell)
	{
		return FVector(Cell.X + 0.5, Cell.Y, Cell.Z + 0.5);
	}

	bool IsLiquid(const FMcBlockWorld& World, const FIntVector& Cell)
	{
		const FMcStateId State = World.GetBlock(Cell);
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (!Registry.IsValidState(State))
		{
			return false;
		}
		const FString Name = Registry.GetBlockOf(State).Name.ToString();
		return Name == TEXT("minecraft:water") || Name == TEXT("minecraft:lava");
	}

	// A place to stand: Cell is free for Height blocks up, nothing solid-surfaced is missing below, and no liquid in the bottom block.
	bool CanStand(const FMcBlockWorld& World, const FIntVector& Cell, int32 Height, bool bBelowNotLeaves)
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (!World.HasChunk(FIntPoint(Cell.X >> 4, Cell.Z >> 4)))
		{
			return false;
		}
		const FMcStateId Below = World.GetBlock(Cell + FIntVector(0, -1, 0));
		if (!Shapes.HasCollision(Below) || IsLiquid(World, Cell))
		{
			return false;
		}
		if (bBelowNotLeaves && Registry.IsValidState(Below) && Registry.GetBlockOf(Below).Name.ToString().EndsWith(TEXT("_leaves")))
		{
			return false;
		}
		for (int32 Up = 0; Up < Height; ++Up)
		{
			if (Shapes.HasCollision(World.GetBlock(Cell + FIntVector(0, Up, 0))))
			{
				return false;
			}
		}
		return true;
	}

	FMcCreaking* FindCreakingOf(FMcEntityHost& Host, const FIntVector& Heart)
	{
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (!Entity->bRemoved && Entity->GetType() == FMcCreaking::StaticType())
			{
				FMcCreaking& Creaking = static_cast<FMcCreaking&>(*Entity);
				if (Creaking.IsLinked() && Creaking.GetHeart() == Heart)
				{
					return &Creaking;
				}
			}
		}
		return nullptr;
	}
}

void McSculkMobs::Connect(FMcSimulation& Simulation, FMcEntityHost& Host, FMcBlockWorld& World)
{
	FMcSimulation* Sim = &Simulation;
	FMcEntityHost* H = &Host;
	FMcBlockWorld* W = &World;

	// Wardens hear what the simulation hears.
	Simulation.AddGameEventListener([Sim, H](const FMcGameEvent& Event)
	{
		if (Event.Frequency <= 0)
		{
			return;
		}
		for (const TUniquePtr<FMcEntity>& Entity : H->GetManager().GetEntities())
		{
			if (Entity->bRemoved || Entity->GetType() != FMcWarden::StaticType())
			{
				continue;
			}
			const FIntVector Cell(FMath::FloorToInt32(Entity->Body.Position.X), FMath::FloorToInt32(Entity->Body.Position.Y), FMath::FloorToInt32(Entity->Body.Position.Z));
			if (McGameEvents::WardenCanHear(Event, Cell) && !Sim->IsVibrationOccluded(Event.Pos, Cell))
			{
				static_cast<FMcWarden&>(*Entity).HearVibration(Event.Source.bPlayer ? Event.Source.PlayerId : 0, Centre(Event.Pos), Event.Source.bProjectile, Centre(Event.Pos));
			}
		}
	});

	// A shrieker that has shrieked at warning level 4 calls a warden: 20 tries in 11 x 13 x 11 around it, none if another warden is within 24.
	Simulation.OnWardenSummon = [H, W](const FIntVector& Pos, int32 PlayerId)
	{
		if (!H->GetRules().bDoWardenSpawning)
		{
			return;
		}
		for (const TUniquePtr<FMcEntity>& Entity : H->GetManager().GetEntities())
		{
			if (!Entity->bRemoved && Entity->GetType() == FMcWarden::StaticType() && FVector::DistSquared(Entity->Body.Position, Centre(Pos)) < 24.0 * 24.0)
			{
				return;
			}
		}
		FRandomStream Random(Pos.X * 73856093 ^ Pos.Y * 19349663 ^ Pos.Z * 83492791 ^ static_cast<int32>(H->GetManager().GetEntities().Num()));
		for (int32 Try = 0; Try < 20; ++Try)
		{
			const int32 DX = Random.RandRange(-5, 5), DZ = Random.RandRange(-5, 5);
			// The highest valid block in the column.
			for (int32 DY = 6; DY >= -6; --DY)
			{
				const FIntVector Cell = Pos + FIntVector(DX, DY, DZ);
				if (CanStand(*W, Cell, 3, false))
				{
					if (FMcMob* Mob = H->SpawnMob(FMcWarden::StaticType(), Centre(Cell)))
					{
						static_cast<FMcWarden*>(Mob)->StartEmerging();
					}
					return;
				}
			}
		}
	};

	// A shrieker's warning: Darkness for 12 s to the players within 40 blocks.
	Simulation.OnShriek = [H](const FIntVector& Pos, int32 PlayerId, int32 WarningLevel)
	{
		FMcEntityManager& Manager = H->GetManager();
		if (!Manager.OnPlayerEffect)
		{
			return;
		}
		for (const FVector& Player : Manager.PlayerPositions)
		{
			if (FVector::DistSquared(Player, Centre(Pos)) <= 40.0 * 40.0)
			{
				Manager.OnPlayerEffect(Player, TEXT("darkness"), 0, 240);
			}
		}
	};

	// The creaking heart: a creaking within 16 across and 8 up and down, tied to the heart.
	Simulation.OnCreakingSpawn = [Sim, H, W](const FIntVector& Heart)
	{
		FRandomStream Random(Heart.X * 73856093 ^ Heart.Y * 19349663 ^ Heart.Z * 83492791 ^ static_cast<int32>(Sim->GetGameTime()));
		for (int32 Try = 0; Try < 30; ++Try)
		{
			const int32 DX = Random.RandRange(-McCreaking::SpawnAcross, McCreaking::SpawnAcross);
			const int32 DZ = Random.RandRange(-McCreaking::SpawnAcross, McCreaking::SpawnAcross);
			const int32 DY = Random.RandRange(-McCreaking::SpawnUpDown, McCreaking::SpawnUpDown);
			const FIntVector Cell = Heart + FIntVector(DX, DY, DZ);
			if (!CanStand(*W, Cell, 2, true))
			{
				continue;
			}
			if (FMcMob* Mob = H->SpawnMob(FMcCreaking::StaticType(), Centre(Cell)))
			{
				FMcCreaking& Creaking = static_cast<FMcCreaking&>(*Mob);
				Creaking.LinkTo(Heart);
				Creaking.OnHit = [Sim](const FIntVector& Where) { Sim->CreakingHit(Where); };
				H->GetManager().PlaySound(FName("entity.creaking.spawn"), Centre(Cell), 1.0f, 1.0f);
				return true;
			}
		}
		return false;
	};
	Simulation.GetCreakingDistance = [H](const FIntVector& Heart)
	{
		const FMcCreaking* Creaking = FindCreakingOf(*H, Heart);
		return Creaking ? Creaking->GetHeartDistance() : -1.0;
	};
	Simulation.OnCreakingRelease = [H](const FIntVector& Heart, bool bBroken)
	{
		if (FMcCreaking* Creaking = FindCreakingOf(*H, Heart))
		{
			H->GetManager().PlaySound(FName(bBroken ? "entity.creaking.twitch" : "entity.creaking.deactivate"), Creaking->Body.Position, 1.0f, 1.0f);
			Creaking->bRemoved = true;
		}
	};
}
