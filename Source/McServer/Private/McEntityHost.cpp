#include "McEntityHost.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEntityProxy.h"
#include "McLoot.h"
#include "McMob.h"
#include "McRandom.h"
#include "McServer.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/Actor.h"

namespace
{
	const TCHAR* Animals[] = { TEXT("minecraft:pig"), TEXT("minecraft:cow"), TEXT("minecraft:sheep"), TEXT("minecraft:chicken") };
	const TCHAR* Hostiles[] = { TEXT("minecraft:zombie"), TEXT("minecraft:skeleton"), TEXT("minecraft:spider") };

	// Hostile mobs stay under this many in the world per player, and are removed past this distance from every player.
	constexpr int32 HostileCapPerPlayer = 20;
	constexpr double DespawnDistance = 100.0;
	constexpr double MinSpawnDistance = 24.0;
	constexpr double MaxSpawnDistance = 48.0;
	// Chunks given their animals per tick.
	constexpr int32 MaxChunkSeedsPerTick = 2;
}

FMcEntityHost::FMcEntityHost(FMcBlockWorld& InWorld, int64 InSeed)
	: World(InWorld)
	, Manager(InWorld)
	, Seed(InSeed)
	, Random(static_cast<int32>(InSeed ^ (InSeed >> 32)))
{
}

FMcEntityHost::~FMcEntityHost()
{
	// Runs when the owning actor is destroyed by garbage collection, when other actors can't be destroyed safely;
	// DestroyProxies has done that by now, at EndPlay. Only the bookkeeping is left.
	Proxies.Empty();
}

void FMcEntityHost::DestroyProxies()
{
	for (const TPair<int32, TWeakObjectPtr<AActor>>& Entry : Proxies)
	{
		// A world being torn down is already destroying its actors.
		if (AActor* Actor = Entry.Value.Get())
		{
			if (!Actor->IsActorBeingDestroyed())
			{
				Actor->Destroy();
			}
		}
	}
	Proxies.Empty();
	ProxyClass = nullptr;
}

void FMcEntityHost::SetProxyClass(UWorld* InGameWorld, TSubclassOf<AActor> Class)
{
	GameWorld = InGameWorld;
	ProxyClass = Class;
}

int32 FMcEntityHost::NumHostile() const
{
	int32 Count = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->IsLiving() && static_cast<const FMcMob&>(*Entity).Type.AttackDamage > 0.0f)
		{
			++Count;
		}
	}
	return Count;
}

TOptional<int32> FMcEntityHost::FindStandingY(int32 X, int32 Z, int32 NearY, int32 Range) const
{
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!World.HasChunk(FIntPoint(X >> 4, Z >> 4)))
	{
		return {};
	}
	const int32 MinY = World.GetDimension().MinY + 1;
	const int32 MaxY = World.GetDimension().MaxY() - 2;
	for (int32 Y = FMath::Min(NearY + Range, MaxY); Y >= FMath::Max(NearY - Range, MinY); --Y)
	{
		const FMcStateId Floor = World.GetBlock(FIntVector(X, Y - 1, Z));
		if (!Shapes.HasCollision(Floor) || Shapes.GetFluid(Floor) != EMcFluid::None)
		{
			continue;
		}
		const FMcStateId Feet = World.GetBlock(FIntVector(X, Y, Z));
		const FMcStateId Head = World.GetBlock(FIntVector(X, Y + 1, Z));
		if (Registry.IsAir(Feet) && Registry.IsAir(Head))
		{
			return Y;
		}
		// Something solid here: this column's ground is above any gap lower down.
		if (Shapes.HasCollision(Feet))
		{
			return {};
		}
	}
	return {};
}

FMcMob* FMcEntityHost::SpawnMob(FName Kind, const FVector& Pos)
{
	const FMcMobType* Type = FMcMobType::Find(Kind);
	if (!Type)
	{
		return nullptr;
	}
	FMcMob* Mob = new FMcMob(*Type);
	Mob->Body.Position = Pos;
	Mob->Yaw = Random.FRandRange(0.0f, 360.0f);
	Mob->OnDeath = [this](FMcMob& Dead) { DropLoot(Dead); };
	if (Type->AttackDamage > 0.0f)
	{
		Mob->OnAttackPlayer = [this, Kind](const FVector& PlayerPos, float Damage)
		{
			if (OnPlayerHit)
			{
				OnPlayerHit(PlayerPos, Damage);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("%s bit a player at %s for %.1f"), *Kind.ToString(), *PlayerPos.ToString(), Damage);
			}
		};
	}
	Manager.Add(TUniquePtr<FMcEntity>(Mob));
	return Mob;
}

void FMcEntityHost::DropLoot(FMcMob& Mob)
{
	FMcRandom Rng(static_cast<uint64>(Seed) ^ (static_cast<uint64>(Mob.Id) * 0x9E3779B97F4A7C15ull) ^ static_cast<uint64>(Ticks));
	// The loot tables are named after the mob: minecraft:entities/pig.
	const FString Table = TEXT("minecraft:entities/") + Mob.GetType().ToString().RightChop(10);
	TArray<FMcItemStack> Drops;
	if (McLoot::Roll(Table, Rng, Drops))
	{
		for (const FMcItemStack& Stack : Drops)
		{
			if (!Stack.IsEmpty())
			{
				Manager.DropItem(Mob.Body.Position + FVector(0.0, Mob.Type.Height * 0.5, 0.0), Stack);
			}
		}
	}
}

void FMcEntityHost::OnChunkAdded(FIntPoint Chunk)
{
	if (Seeded.Contains(Chunk))
	{
		return;
	}
	Seeded.Add(Chunk);
	SeedQueue.Add(Chunk);
}

void FMcEntityHost::OnChunkRemoved(FIntPoint Chunk)
{
	Seeded.Remove(Chunk);
	SeedQueue.Remove(Chunk);
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->IsLiving() && !Entity->bRemoved
			&& McCoords::BlockToChunk(FMath::FloorToInt32(Entity->Body.Position.X)) == Chunk.X
			&& McCoords::BlockToChunk(FMath::FloorToInt32(Entity->Body.Position.Z)) == Chunk.Y)
		{
			Entity->bRemoved = true;
		}
	}
}

TArray<uint8> FMcEntityHost::CaptureChunk(FIntPoint Chunk)
{
	TArray<uint8> Bytes;
	int32 Count = 0;
	TArray<uint8> Body;
	FMemoryWriter Writer(Body);
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->bRemoved
			|| McCoords::BlockToChunk(FMath::FloorToInt32(Entity->Body.Position.X)) != Chunk.X
			|| McCoords::BlockToChunk(FMath::FloorToInt32(Entity->Body.Position.Z)) != Chunk.Y)
		{
			continue;
		}
		// Length-prefixed, so a kind that a later version drops can be skipped without reading it.
		FString Type = Entity->GetType().ToString();
		TArray<uint8> Data;
		FMemoryWriter DataWriter(Data);
		Entity->Serialize(DataWriter);
		Writer << Type << Data;
		++Count;
	}
	if (Count == 0)
	{
		return Bytes;
	}
	FMemoryWriter Out(Bytes);
	Out << Count;
	Bytes.Append(Body);
	return Bytes;
}

int32 FMcEntityHost::RestoreChunk(TConstArrayView<uint8> Bytes)
{
	FMemoryReaderView Reader(Bytes);
	int32 Count = 0;
	Reader << Count;
	int32 Restored = 0;
	for (int32 Index = 0; Index < Count && !Reader.IsError(); ++Index)
	{
		FString Type;
		TArray<uint8> Data;
		Reader << Type << Data;
		if (Reader.IsError())
		{
			break;
		}
		FMemoryReader DataReader(Data);
		const FName Kind(*Type);
		if (Kind == FMcItemEntity::StaticType())
		{
			TUniquePtr<FMcItemEntity> Item = MakeUnique<FMcItemEntity>();
			if (Item->Serialize(DataReader))
			{
				Manager.Add(MoveTemp(Item));
				++Restored;
			}
		}
		else if (FMcMob* Mob = SpawnMob(Kind, FVector::ZeroVector))
		{
			if (Mob->Serialize(DataReader))
			{
				++Restored;
			}
			else
			{
				Mob->bRemoved = true;
			}
		}
		else
		{
			UE_LOG(LogMcServer, Warning, TEXT("A saved %s isn't a kind of entity this version has; it's dropped."), *Type);
		}
	}
	return Restored;
}

int32 FMcEntityHost::RemoveStrays()
{
	int32 Removed = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (!Entity->bRemoved
			&& !World.HasChunk(FIntPoint(McCoords::BlockToChunk(FMath::FloorToInt32(Entity->Body.Position.X)),
				McCoords::BlockToChunk(FMath::FloorToInt32(Entity->Body.Position.Z)))))
		{
			Entity->bRemoved = true;
			++Removed;
		}
	}
	return Removed;
}

void FMcEntityHost::SeedChunk(FIntPoint Chunk)
{
	// The same chunk gets the same animals for a seed, whenever it arrives. Unsigned, so the mixing wraps rather than
	// overflowing.
	const uint64 Mixed = static_cast<uint64>(Seed) * 31ull
		+ static_cast<uint64>(static_cast<int64>(Chunk.X)) * 341873128712ull
		+ static_cast<uint64>(static_cast<int64>(Chunk.Y)) * 132897987541ull;
	FRandomStream Local(static_cast<int32>(static_cast<uint32>(Mixed)));
	if (Local.FRand() > 0.10f)
	{
		return;
	}
	const FName Kind = Animals[Local.RandHelper(UE_ARRAY_COUNT(Animals))];
	const int32 Herd = Local.RandRange(2, 4);
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId Grass = Registry.FindDefaultState(TEXT("minecraft:grass_block")).Get(0);
	int32 Placed = 0;
	int32 CentreX = Chunk.X * 16 + Local.RandRange(2, 13);
	int32 CentreZ = Chunk.Y * 16 + Local.RandRange(2, 13);
	// The first column is searched over the whole height; the ground a few blocks on is near the last one found.
	int32 NearY = 90;
	int32 Range = 200;
	for (int32 Attempt = 0; Attempt < 24 && Placed < Herd; ++Attempt)
	{
		const int32 X = CentreX + Local.RandRange(-4, 4);
		const int32 Z = CentreZ + Local.RandRange(-4, 4);
		const TOptional<int32> Y = FindStandingY(X, Z, NearY, Range);
		if (Y)
		{
			NearY = Y.GetValue();
			Range = 24;
		}
		if (Y && World.GetBlock(FIntVector(X, Y.GetValue() - 1, Z)) == Grass)
		{
			SpawnMob(Kind, FVector(X + 0.5, Y.GetValue(), Z + 0.5));
			++Placed;
		}
	}
}

void FMcEntityHost::SpawnHostiles(const TArray<FVector>& Players)
{
	if (Players.IsEmpty() || NumHostile() >= HostileCapPerPlayer * Players.Num())
	{
		return;
	}
	const FVector& Player = Players[Random.RandHelper(Players.Num())];
	const double Angle = Random.FRandRange(0.0, 2.0 * PI);
	const double Distance = Random.FRandRange(MinSpawnDistance, MaxSpawnDistance);
	const int32 X = FMath::FloorToInt32(Player.X + FMath::Cos(Angle) * Distance);
	const int32 Z = FMath::FloorToInt32(Player.Z + FMath::Sin(Angle) * Distance);
	const TOptional<int32> Y = FindStandingY(X, Z, FMath::FloorToInt32(Player.Y), 24);
	if (!Y)
	{
		return;
	}
	// Dark enough: night in the open, or anywhere the sky can't reach (a cave, a roofed room).
	const bool bExposed = Manager.IsSkyExposed(FIntVector(X, Y.GetValue(), Z));
	if (bExposed && Manager.bDaylight)
	{
		return;
	}
	SpawnMob(Hostiles[Random.RandHelper(UE_ARRAY_COUNT(Hostiles))], FVector(X + 0.5, Y.GetValue(), Z + 0.5));
}

void FMcEntityHost::Tick(const TArray<FVector>& Players, double DayTicks)
{
	++Ticks;
	const int32 TimeOfDay = static_cast<int32>(FMath::Fmod(DayTicks, 24000.0));
	// Sunrise is 0; the sky is dark from 13000 to 23000.
	Manager.bDaylight = TimeOfDay < 13000 || TimeOfDay >= 23000;

	Manager.PlayerPositions = Players;

	// A few chunks a tick get their animals.
	for (int32 Budget = MaxChunkSeedsPerTick; Budget > 0 && !SeedQueue.IsEmpty(); --Budget)
	{
		const FIntPoint Chunk = SeedQueue[0];
		SeedQueue.RemoveAt(0);
		if (World.HasChunk(Chunk))
		{
			SeedChunk(Chunk);
		}
	}

	// Hostile mobs far from everyone are removed. With nobody in the world (a listen server before its player has a
	// pawn, the moments after a death) there is nobody to be far from, so they stay.
	if (!Players.IsEmpty() && Ticks % 40 == 0)
	{
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (!Entity->IsLiving())
			{
				continue;
			}
			FMcMob& Mob = static_cast<FMcMob&>(*Entity);
			if (Mob.Type.AttackDamage <= 0.0f)
			{
				continue;
			}
			bool bNear = false;
			for (const FVector& Player : Players)
			{
				bNear |= FVector::DistSquared(Player, Mob.Body.Position) < DespawnDistance * DespawnDistance;
			}
			if (!bNear)
			{
				Mob.bRemoved = true;
			}
		}
	}
	if (Ticks % 20 == 0)
	{
		SpawnHostiles(Players);
	}
#if !UE_BUILD_SHIPPING
	// Development: -McSpawnMobs=<kind>,<n> puts n of a kind in a ring 5 blocks from the first player, 5 seconds in
	// (or when the first player turns up, if later).
	if (!bDevSpawnDone && Ticks >= 100 && !Players.IsEmpty())
	{
		bDevSpawnDone = true;
		FString Spec;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McSpawnMobs="), Spec, false))
		{
			FString Kind, Count;
			if (Spec.Split(TEXT(","), &Kind, &Count))
			{
				const int32 N = FCString::Atoi(*Count);
				for (int32 Index = 0; Index < N; ++Index)
				{
					const double Angle = 2.0 * PI * Index / FMath::Max(N, 1);
					const int32 X = FMath::FloorToInt32(Players[0].X + FMath::Cos(Angle) * 5.0);
					const int32 Z = FMath::FloorToInt32(Players[0].Z + FMath::Sin(Angle) * 5.0);
					if (const TOptional<int32> Y = FindStandingY(X, Z, FMath::FloorToInt32(Players[0].Y), 24))
					{
						SpawnMob(FName(*(TEXT("minecraft:") + Kind)), FVector(X + 0.5, Y.GetValue(), Z + 0.5));
					}
				}
			}
		}
	}
	if (Ticks % 400 == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Entities: %d, hostile %d"), Manager.GetEntities().Num(), NumHostile());
	}
#endif
	Manager.Tick();
	SyncProxies();
}

void FMcEntityHost::SyncProxies()
{
	UWorld* GameWorldPtr = GameWorld.Get();
	if (!GameWorldPtr || !ProxyClass)
	{
		return;
	}
	TSet<int32> Alive;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		Alive.Add(Entity->Id);
		TWeakObjectPtr<AActor>& Slot = Proxies.FindOrAdd(Entity->Id);
		AActor* Actor = Slot.Get();
		if (!Actor)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Actor = GameWorldPtr->SpawnActor<AActor>(ProxyClass, McCoords::BlockToUnreal(Entity->Body.Position), FRotator::ZeroRotator, Params);
			Slot = Actor;
		}
		if (IMcEntityProxy* Proxy = Cast<IMcEntityProxy>(Actor))
		{
			Proxy->SyncFromEntity(*Entity);
		}
	}
	for (auto It = Proxies.CreateIterator(); It; ++It)
	{
		if (!Alive.Contains(It.Key()))
		{
			if (AActor* Actor = It.Value().Get())
			{
				Actor->Destroy();
			}
			It.RemoveCurrent();
		}
	}
}

bool FMcEntityHost::Attack(const FVector& Eye, const FVector& Direction, float Reach, float Damage)
{
	// The nearest living entity whose box the ray enters within Reach.
	// The direction comes from a client: a NaN or empty one hits nothing.
	if (Eye.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero())
	{
		return false;
	}
	FMcMob* Best = nullptr;
	double BestDistance = Reach;
	const FVector Dir = Direction.GetSafeNormal();
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (!Entity->IsLiving() || Entity->bRemoved)
		{
			continue;
		}
		const FBox Box = Entity->Body.GetBox();
		FVector HitPoint, HitNormal;
		float Time = 0.0f;
		if (FMath::LineExtentBoxIntersection(Box, Eye, Eye + Dir * Reach, FVector::ZeroVector, HitPoint, HitNormal, Time))
		{
			const double Distance = Time * Reach;
			if (Distance <= BestDistance)
			{
				BestDistance = Distance;
				Best = static_cast<FMcMob*>(Entity.Get());
			}
		}
	}
	if (!Best)
	{
		return false;
	}
	Best->Damage(Damage, &Eye);
	return true;
}
