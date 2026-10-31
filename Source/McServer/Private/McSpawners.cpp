#include "McSpawners.h"
#include "McBlockEntity.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "McPhysics.h"

namespace
{
	const FMcBlockEntity* FindSpawner(const FMcBlockWorld& World, const FIntVector& Pos)
	{
		const FMcChunk* Chunk = World.FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
		const FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15) : nullptr;
		return Entity && Entity->Kind == EMcBlockEntityKind::Spawner ? Entity : nullptr;
	}
}

void FMcSpawners::OnChunkAdded(FIntPoint ChunkPos)
{
	if (const FMcChunk* Chunk = World.FindChunk(ChunkPos))
	{
		for (const TPair<FIntVector, FMcBlockEntity>& Entity : Chunk->GetBlockEntities())
		{
			if (Entity.Value.Kind == EMcBlockEntityKind::Spawner)
			{
				Spawners.FindOrAdd(FIntVector(ChunkPos.X * 16 + Entity.Key.X, Entity.Key.Y, ChunkPos.Y * 16 + Entity.Key.Z), FirstDelay);
			}
		}
	}
}

void FMcSpawners::OnBlockEntityChanged(const FIntVector& Pos)
{
	if (FindSpawner(World, Pos))
	{
		Spawners.FindOrAdd(Pos, FirstDelay);
	}
}

void FMcSpawners::Tick(FMcEntityHost& Host, const TArray<FVector>& Players, FRandomStream& Random)
{
	for (auto It = Spawners.CreateIterator(); It; ++It)
	{
		const FIntVector Pos = It.Key();
		const FMcBlockEntity* Entity = FindSpawner(World, Pos);
		if (!Entity)
		{
			// Broken, or its chunk gone.
			It.RemoveCurrent();
			continue;
		}
		const FVector Middle = FVector(Pos) + FVector(0.5);
		bool bNear = false;
		for (const FVector& Player : Players)
		{
			bNear |= FVector::DistSquared(Player, Middle) <= FMath::Square(PlayerRange);
		}
		if (!bNear)
		{
			continue;
		}
		if (It.Value() > 0)
		{
			--It.Value();
		}
		else if (Spawn(Host, Pos, Entity->EntityId, Random))
		{
			It.Value() = Random.RandRange(MinDelay, MaxDelay - 1);
			if (OnCycle)
			{
				OnCycle(Pos);
			}
		}
	}
}

bool FMcSpawners::Spawn(FMcEntityHost& Host, const FIntVector& Pos, FName Kind, FRandomStream& Random)
{
	const FMcMobType* Type = FMcMobType::Find(Kind);
	if (!Type)
	{
		// A kind we have no mob for yet: the cycle runs on, empty.
		return true;
	}
	if (Type->IsHostile() && Host.GetRules().IsPeaceful())
	{
		return true;
	}
	FMcEntityManager& Manager = Host.GetManager();
	const FBox Crowd(FVector(Pos) - FVector(SpawnRange), FVector(Pos) + FVector(SpawnRange + 1.0));
	int32 Spawned = 0;
	for (int32 Try = 0; Try < Tries; ++Try)
	{
		TArray<FMcEntity*> Near;
		Manager.FindInBox(Crowd, Near);
		int32 Same = 0;
		for (const FMcEntity* Other : Near)
		{
			Same += Other->GetType() == Kind ? 1 : 0;
		}
		if (Same >= MaxNearby)
		{
			return true;
		}
		const FVector Feet(Pos.X + 0.5 + Random.FRandRange(-SpawnRange, SpawnRange), Pos.Y + Random.RandRange(-1, 1), Pos.Z + 0.5 + Random.FRandRange(-SpawnRange, SpawnRange));
		const FIntVector Cell(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z));
		if (Type->IsHostile() && !Manager.IsDarkEnoughToSpawn(Cell))
		{
			continue;
		}
		FMcMob* Mob = Host.SpawnMob(Kind, Feet);
		if (Mob && McPhysics::Collides(World, Mob->Body.GetBox()))
		{
			Mob->bRemoved = true;
			continue;
		}
		Spawned += Mob ? 1 : 0;
	}
	return Spawned > 0;
}
