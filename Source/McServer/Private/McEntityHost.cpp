#include "McEntityHost.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBiomeRegistry.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEntityProxy.h"
#include "McGameRules.h"
#include "McItemRegistry.h"
#include "McLoot.h"
#include "McMob.h"
#include "McRandom.h"
#include "McStructureSpawns.h"
#include "McVillager.h"
#include "McMobLook.h"
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
	// Where a chunk has no biome to ask (or the registry isn't loaded): the hostile mobs most Overworld biomes list,
	// by weight (plains in 26.3's data, less the ones we have no mob for).
	const FMcSpawnEntry FallbackMonsters[] = {
		{ TEXT("minecraft:spider"), 100, 4, 4 }, { TEXT("minecraft:zombie"), 95, 4, 4 }, { TEXT("minecraft:skeleton"), 100, 4, 4 },
		{ TEXT("minecraft:creeper"), 100, 4, 4 }, { TEXT("minecraft:slime"), 100, 4, 4 }, { TEXT("minecraft:enderman"), 10, 1, 4 },
	};
	// Slimes spawn below this height in the slime chunks.
	constexpr int32 SlimeMaxY = 40;

	// Whether the chunk is a slime chunk: one in ten, by the game's own formula, a Java Random seeded from the world
	// seed and the chunk (minecraft.wiki, "Slime"). Slimes spawn in them below y 40.
	bool IsSlimeChunk(int64 WorldSeed, int32 ChunkX, int32 ChunkZ)
	{
		const uint32 X = static_cast<uint32>(ChunkX);
		const uint32 Z = static_cast<uint32>(ChunkZ);
		// Java's int arithmetic wraps; the terms are added as longs.
		const int64 Mixed = WorldSeed
			+ static_cast<int32>(X * X * 4987142u)
			+ static_cast<int32>(X * 5947611u)
			+ static_cast<int64>(static_cast<int32>(Z * Z)) * 4392871LL
			+ static_cast<int32>(Z * 389711u);
		constexpr uint64 Mask = (1ull << 48) - 1;
		uint64 State = (static_cast<uint64>(Mixed ^ 987234911LL) ^ 0x5DEECE66Dull) & Mask;
		const auto Next31 = [&State]()
		{
			State = (State * 0x5DEECE66Dull + 0xBull) & Mask;
			return static_cast<int32>(State >> 17);
		};
		int32 Bits = Next31();
		int32 Value = Bits % 10;
		while (static_cast<int32>(static_cast<uint32>(Bits) - static_cast<uint32>(Value) + 9u) < 0)
		{
			Bits = Next31();
			Value = Bits % 10;
		}
		return Value == 0;
	}

	// What a kind spawns on. The first four graze on grass; a turtle comes ashore on sand; the others live in cold,
	// stony and wet places too.
	bool IsSpawnGround(FName Kind, FName Block)
	{
		static const TSet<FName> Wide = { TEXT("minecraft:grass_block"), TEXT("minecraft:snow_block"), TEXT("minecraft:podzol"),
			TEXT("minecraft:mud"), TEXT("minecraft:sand"), TEXT("minecraft:stone"), TEXT("minecraft:gravel"), TEXT("minecraft:coarse_dirt") };
		if (Kind == TEXT("minecraft:turtle"))
		{
			return Block == TEXT("minecraft:sand");
		}
		if (Kind == TEXT("minecraft:pig") || Kind == TEXT("minecraft:cow") || Kind == TEXT("minecraft:sheep") || Kind == TEXT("minecraft:chicken"))
		{
			return Block == TEXT("minecraft:grass_block");
		}
		return Wide.Contains(Block);
	}

	// The mobs peaceful removes: the ones that go after players, not the wolf and bee that only fight back.
	bool IsMonster(const FMcMobType& Type)
	{
		return Type.IsHostile() && Type.Name != TEXT("minecraft:wolf") && Type.Name != TEXT("minecraft:bee");
	}

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
	// Bites, arrows and blasts all reach the player through here.
	Manager.OnPlayerHurt = [this](const FVector& PlayerPos, float Damage)
	{
		if (OnPlayerHit)
		{
			OnPlayerHit(PlayerPos, Damage);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("A player at %s was hit for %.1f"), *PlayerPos.ToString(), Damage);
		}
	};
	Manager.PlayerWearsGold = [this](const FVector& PlayerPos)
	{
		return PlayerWearsGold && PlayerWearsGold(PlayerPos);
	};
	Manager.OnPlayerEffect = [this](const FVector& PlayerPos, FName Effect, int32 Amplifier, int32 Duration)
	{
		if (OnPlayerEffect)
		{
			OnPlayerEffect(PlayerPos, Effect, Amplifier, Duration);
		}
	};
	// A villager a zombie killed may rise as a zombie villager: never on Easy, half the time on Normal, always on Hard.
	Manager.OnVillagerSlain = [this](FMcMob& Victim)
	{
		const int32 Difficulty = Rules.Difficulty;
		if (Victim.GetType() == FMcVillager::StaticType() && (Difficulty >= 3 || (Difficulty == 2 && Random.FRand() < 0.5f)))
		{
			Infect(static_cast<FMcVillager&>(Victim));
		}
	};
	Manager.OnSpawnMob = [this](FName Kind, const FVector& Pos) { return SpawnMob(Kind, Pos); };
	// A baby villager takes the look of the parent that called.
	Manager.OnVillagerBred = [this](const FVector& Where, FMcMob& Parent)
	{
		if (FMcMob* Made = SpawnMob(FMcVillager::StaticType(), Where))
		{
			FMcVillager& Baby = static_cast<FMcVillager&>(*Made);
			Baby.SetVariant(static_cast<FMcVillager&>(Parent).GetVariant());
			Baby.SetBaby(true);
		}
	};
	// A village that wants a golem gets one on open ground within 3 to 6 blocks of the villager that called.
	Manager.OnGolemWanted = [this](const FVector& Around)
	{
		for (int32 Try = 0; Try < 12; ++Try)
		{
			const float Angle = Random.FRand() * 2.0f * PI;
			const float Radius = 3.0f + Random.FRand() * 3.0f;
			const int32 X = FMath::FloorToInt32(Around.X + FMath::Cos(Angle) * Radius);
			const int32 Z = FMath::FloorToInt32(Around.Z + FMath::Sin(Angle) * Radius);
			const TOptional<int32> Y = FindStandingY(X, Z, FMath::FloorToInt32(Around.Y), 4);
			if (Y.IsSet())
			{
				SpawnMob(TEXT("minecraft:iron_golem"), FVector(X + 0.5, Y.GetValue(), Z + 0.5));
				return;
			}
		}
	};
	// What an explosion leaves behind: each block it destroyed rolls its own loot table (blocks/<name>), as when
	// broken with no tool and blown up with this power: survives_explosion and explosion_decay keep each item with a
	// chance of one in the power, so stone gives cobblestone and a leaf block its saplings, never itself.
	Manager.OnBlocksDestroyed = [this](const TArray<FMcDestroyedBlock>& Blocks, float Power)
	{
		const FMcItemRegistry& Items = FMcItemRegistry::Get();
		if (!Items.IsLoaded() || !Rules.bDoTileDrops)
		{
			return;
		}
		FMcRandom LootRandom((static_cast<uint64>(Random.GetUnsignedInt()) << 32) | Random.GetUnsignedInt());
		for (const FMcDestroyedBlock& Block : Blocks)
		{
			McLoot::FBlockContext Context;
			Context.State = Block.State;
			Context.ExplosionRadius = Power;
			TArray<FMcItemStack> Drops;
			if (!McLoot::RollBlock(Context, LootRandom, Drops))
			{
				continue;
			}
			for (const FMcItemStack& Drop : Drops)
			{
				if (Items.Find(Drop.Item))
				{
					Manager.DropItem(FVector(Block.Pos.X + 0.5, Block.Pos.Y + 0.5, Block.Pos.Z + 0.5), Drop);
				}
			}
		}
	};
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

void FMcEntityHost::SetRules(const FMcGameRules& InRules)
{
	Rules = InRules;
	if (Rules.IsPeaceful())
	{
		RemoveHostiles();
	}
}

int32 FMcEntityHost::RemoveHostiles()
{
	int32 Removed = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->IsLiving() && !Entity->bRemoved && IsMonster(static_cast<const FMcMob&>(*Entity).Type))
		{
			Entity->bRemoved = true;
			++Removed;
		}
	}
	return Removed;
}

int32 FMcEntityHost::NumHostile() const
{
	int32 Count = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->IsLiving() && !Entity->bRemoved && static_cast<const FMcMob&>(*Entity).Type.IsHostile())
		{
			++Count;
		}
	}
	return Count;
}

TOptional<int32> FMcEntityHost::FindSwimmingY(int32 X, int32 Z, int32 NearY, int32 Range) const
{
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	if (!World.HasChunk(FIntPoint(X >> 4, Z >> 4)))
	{
		return {};
	}
	const int32 MinY = World.GetDimension().MinY + 1;
	const int32 MaxY = World.GetDimension().MaxY() - 2;
	for (int32 Y = FMath::Min(NearY + Range, MaxY); Y >= FMath::Max(NearY - Range, MinY); --Y)
	{
		if (Shapes.GetFluid(World.GetBlock(FIntVector(X, Y, Z))) == EMcFluid::Water)
		{
			// The surface block; the feet go one lower so the head is in water too.
			return Shapes.GetFluid(World.GetBlock(FIntVector(X, Y - 1, Z))) == EMcFluid::Water ? TOptional<int32>(Y - 1) : TOptional<int32>();
		}
	}
	return {};
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
	FMcMob* Mob = nullptr;
	if (Kind == FMcVillager::StaticType())
	{
		FMcVillager* Villager = new FMcVillager(*Type);
		Villager->SetVariant(GetVillagerVariant(FIntVector(FMath::FloorToInt32(Pos.X), FMath::FloorToInt32(Pos.Y), FMath::FloorToInt32(Pos.Z))));
		Mob = Villager;
	}
	else if (Kind == FMcZombieVillager::StaticType())
	{
		FMcZombieVillager* Zombie = new FMcZombieVillager(*Type);
		Zombie->SetVariant(GetVillagerVariant(FIntVector(FMath::FloorToInt32(Pos.X), FMath::FloorToInt32(Pos.Y), FMath::FloorToInt32(Pos.Z))));
		Mob = Zombie;
	}
	else if (Kind == FMcWanderingTrader::StaticType())
	{
		Mob = new FMcWanderingTrader(*Type);
	}
	else
	{
		Mob = new FMcMob(*Type);
	}
	Mob->Body.Position = Pos;
	Mob->Yaw = Random.FRandRange(0.0f, 360.0f);
	// (A husk: 2 of 100 spawned at Normal held an iron sword, the zombie's rate within the count's noise; shared.)
	if (Kind == FName(TEXT("minecraft:zombie")) || Kind == FName(TEXT("minecraft:husk")) || Kind == FMcZombieVillager::StaticType())
	{
		// Armed at spawn, from counts of 2000 spawned zombies per difficulty on the reference (night, drops cleared between batches):
		//   Easy 19 armed (0.95%), Normal 20 (1.0%), Hard 108 (5.4%); every armed one held an iron sword, shovel or spear.
		//   Over all 147 armed: 29 swords, 94 shovels, 24 spears. Rates are good to about a quarter of their size (Easy and Normal
		//   to about a third); the item mix to a few points either way. Easy and Normal read the same, so they share one rate.
		if (Random.FRand() < (Rules.Difficulty >= 3 ? 0.054f : 0.01f))
		{
			const int32 Pick = Random.RandHelper(147);
			Mob->HeldItem = Pick < 29 ? FName(TEXT("minecraft:iron_sword")) : Pick < 29 + 94 ? FName(TEXT("minecraft:iron_shovel")) : FName(TEXT("minecraft:iron_spear"));
		}
	}
	if (Kind == FName(TEXT("minecraft:piglin")))
	{
		// 100 spawned (Normal): golden sword 38, crossbow 42, nothing 20.
		const int32 Pick = Random.RandHelper(100);
		Mob->HeldItem = Pick < 38 ? FName(TEXT("minecraft:golden_sword")) : Pick < 80 ? FName(TEXT("minecraft:crossbow")) : FName();
	}
	else if (Kind == FName(TEXT("minecraft:zombified_piglin")))
	{
		// 93 of 100 held a golden sword.
		Mob->HeldItem = Random.RandHelper(100) < 93 ? FName(TEXT("minecraft:golden_sword")) : FName();
	}
	else if (Kind == FName(TEXT("minecraft:piglin_brute")))
	{
		Mob->HeldItem = TEXT("minecraft:golden_axe");
	}
	if (Kind == FName(TEXT("minecraft:drowned")))
	{
		// Armed at spawn, from 1000 drowned spawned on each difficulty on the reference (night): trident 69 / 57 / 58 on Easy / Normal / Hard
		// (6.1% over all 3000, good to about 0.45 points), fishing rod 36 / 23 / 42 (3.4%, about 0.35 points); no difference by difficulty
		// beyond that noise, so one rate each. (3.2% also carry a nautilus shell in the other hand: not modelled.)
		const float Roll = Random.FRand();
		if (Roll < 0.061f)
		{
			Mob->HeldItem = FName(TEXT("minecraft:trident"));
		}
		else if (Roll < 0.061f + 0.034f)
		{
			Mob->HeldItem = FName(TEXT("minecraft:fishing_rod"));
		}
	}
	if (McMobLook::GetCount(Kind) > 0)
	{
		Mob->LookSeed = McMobLook::Pick(Kind, GetBiomeName(FIntVector(FMath::FloorToInt32(Pos.X), FMath::FloorToInt32(Pos.Y), FMath::FloorToInt32(Pos.Z))), Random);
	}
	Mob->OnDeath = [this](FMcMob& Dead)
	{
		if (Dead.Type.bHops && Dead.Size > 1)
		{
			SplitSlime(Dead);
		}
		else
		{
			DropLoot(Dead);
		}
	};
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

FName FMcEntityHost::GetBiomeName(const FIntVector& Cell) const
{
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	const FMcChunk* Chunk = World.FindChunk(FIntPoint(McCoords::BlockToChunk(Cell.X), McCoords::BlockToChunk(Cell.Z)));
	if (!Biomes.IsLoaded() || !Chunk)
	{
		return NAME_None;
	}
	return Biomes.GetName(Chunk->GetBiome(Cell.X & 15, Cell.Y, Cell.Z & 15));
}

FMcMob* FMcEntityHost::Infect(FMcVillager& Villager)
{
	FMcMob* Made = SpawnMob(FMcZombieVillager::StaticType(), Villager.Body.Position);
	if (!Made)
	{
		return nullptr;
	}
	FMcZombieVillager& Zombie = static_cast<FMcZombieVillager&>(*Made);
	Zombie.Become(Villager);
	Zombie.Body.Velocity = Villager.Body.Velocity;
	Zombie.Yaw = Villager.Yaw;
	Villager.bRemoved = true;
	return Made;
}

void FMcEntityHost::FinishCures()
{
	TArray<FMcZombieVillager*> Done;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (!Entity->bRemoved && Entity->GetType() == FMcZombieVillager::StaticType() && static_cast<FMcZombieVillager&>(*Entity).IsCured())
		{
			Done.Add(static_cast<FMcZombieVillager*>(Entity.Get()));
		}
	}
	for (FMcZombieVillager* Zombie : Done)
	{
		FMcMob* Made = SpawnMob(FMcVillager::StaticType(), Zombie->Body.Position);
		if (!Made)
		{
			continue;
		}
		FMcVillager& Villager = static_cast<FMcVillager&>(*Made);
		if (!Zombie->GetVillagerData().IsEmpty())
		{
			FMemoryReaderView Reader(Zombie->GetVillagerData());
			if (!Villager.Serialize(Reader))
			{
				// A damaged record: the villager it was is lost, so a plain one in its look comes back.
				Villager.SetVariant(Zombie->GetVariant());
			}
		}
		else
		{
			Villager.SetVariant(Zombie->GetVariant());
		}
		Villager.Body.Position = Zombie->Body.Position;
		Villager.Body.Velocity = Zombie->Body.Velocity;
		Villager.Yaw = Zombie->Yaw;
		// The player who cured it is thanked with cheaper trades.
		if (!Zombie->GetCurer().IsEmpty())
		{
			Villager.AddReputation(Zombie->GetCurer(), McZombieVillager::CureReputation);
		}
		Zombie->bRemoved = true;
	}
}

bool FMcEntityHost::SpawnWanderingTrader(const FVector& Player)
{
	for (int32 Try = 0; Try < 20; ++Try)
	{
		const float Angle = Random.FRand() * 2.0f * PI;
		const float Radius = 24.0f + Random.FRand() * 24.0f;
		const int32 X = FMath::FloorToInt32(Player.X + FMath::Cos(Angle) * Radius);
		const int32 Z = FMath::FloorToInt32(Player.Z + FMath::Sin(Angle) * Radius);
		const TOptional<int32> Y = FindStandingY(X, Z, FMath::FloorToInt32(Player.Y), 16);
		if (!Y.IsSet())
		{
			continue;
		}
		FMcMob* Trader = SpawnMob(FMcWanderingTrader::StaticType(), FVector(X + 0.5, Y.GetValue(), Z + 0.5));
		if (!Trader)
		{
			return false;
		}
		const int32 TraderId = Trader->Id;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			if (FMcMob* Llama = SpawnMob(TEXT("minecraft:trader_llama"), FVector(X + 0.5 + (Index ? 1.5 : -1.5), Y.GetValue(), Z + 0.5)))
			{
				Llama->LeashHolderId = TraderId;
			}
		}
		return true;
	}
	return false;
}

bool FMcEntityHost::MatchGolem(const FIntVector& Pumpkin, TArray<FIntVector>& OutBlocks, FVector& OutFeet) const
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const auto Is = [&](const FIntVector& Cell, std::initializer_list<const TCHAR*> Names)
	{
		const FMcStateId State = World.GetBlock(Cell);
		if (!Registry.IsValidState(State))
		{
			return false;
		}
		const FName Name = Registry.GetBlockOf(State).Name;
		for (const TCHAR* Wanted : Names)
		{
			if (Name == FName(Wanted))
			{
				return true;
			}
		}
		return false;
	};
	if (!Is(Pumpkin, { TEXT("minecraft:carved_pumpkin"), TEXT("minecraft:jack_o_lantern") }))
	{
		return false;
	}
	const FIntVector Chest = Pumpkin - FIntVector(0, 1, 0);
	const FIntVector Base = Pumpkin - FIntVector(0, 2, 0);
	static const TCHAR* Iron = TEXT("minecraft:iron_block");
	if (!Is(Chest, { Iron }) || !Is(Base, { Iron }))
	{
		return false;
	}
	for (const FIntVector& Side : { FIntVector(1, 0, 0), FIntVector(0, 0, 1) })
	{
		if (Is(Chest + Side, { Iron }) && Is(Chest - Side, { Iron }))
		{
			OutBlocks = { Pumpkin, Chest, Base, Chest + Side, Chest - Side };
			OutFeet = FVector(Base.X + 0.5, Base.Y, Base.Z + 0.5);
			return true;
		}
	}
	return false;
}

FName FMcEntityHost::GetVillagerVariant(const FIntVector& Cell) const
{
	const FName Biome = GetBiomeName(Cell);
	return Biome.IsNone() ? FName(TEXT("plains")) : McVillager::GetVariantOfBiome(Biome);
}

void FMcEntityHost::SplitSlime(FMcMob& Dead)
{
	// Two to four of half the size, spread round where it died. Only the smallest drop loot.
	const int32 Count = 2 + Random.RandHelper(3);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double X = (Index % 2 - 0.5) * Dead.Size / 4.0;
		const double Z = (Index / 2 - 0.5) * Dead.Size / 4.0;
		if (FMcMob* Child = SpawnMob(Dead.GetType(), Dead.Body.Position + FVector(X, 0.0, Z)))
		{
			Child->SetSize(Dead.Size / 2);
			Child->Yaw = Random.FRandRange(0.0f, 360.0f);
			Child->Body.Velocity = FVector(X * 0.2, 0.2, Z * 0.2);
		}
	}
}

void FMcEntityHost::DropLoot(FMcMob& Mob)
{
	if (!Rules.bDoMobLoot)
	{
		return;
	}
	FMcRandom Rng(static_cast<uint64>(Seed) ^ (static_cast<uint64>(Mob.Id) * 0x9E3779B97F4A7C15ull) ^ static_cast<uint64>(Ticks));
	// The loot tables are named after the mob: minecraft:entities/pig.
	const FString Table = TEXT("minecraft:entities/") + Mob.GetType().ToString().RightChop(10);
	TArray<FMcItemStack> Drops;
	if (McLoot::RollEntity(Table, Mob.bCaptain, Rng, Drops))
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
		if (Entity->bRemoved || !Entity->IsPersistent()
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
		if (Entity->IsLiving())
		{
			// After what the kind itself keeps, so an older save without it still reads.
			int32 Look = static_cast<FMcMob&>(*Entity).LookSeed;
			DataWriter << Look;
			int32 Young = static_cast<FMcMob&>(*Entity).YoungTicks;
			DataWriter << Young;
		}
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
				if (!DataReader.AtEnd())
				{
					int32 Look = -1;
					DataReader << Look;
					if (!DataReader.IsError() && Look >= 0 && Look < McMobLook::GetCount(Kind))
					{
						Mob->LookSeed = Look;
					}
					int32 Young = 0;
					if (!DataReader.AtEnd())
					{
						DataReader << Young;
						if (!DataReader.IsError() && Young > 0 && FMcMob::CanBeYoung(Kind))
						{
							Mob->SetYoung(Young);
						}
					}
				}
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
	if (!Rules.bDoMobSpawning)
	{
		return;
	}
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
	FName Kind = Animals[Local.RandHelper(UE_ARRAY_COUNT(Animals))];
	int32 Herd = Local.RandRange(2, 4);
	// Where the chunk's biome lists creatures we have mobs for, one of those by weight, in the group size it lists.
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	if (const FMcChunk* BiomeChunk = Biomes.IsLoaded() ? World.FindChunk(Chunk) : nullptr)
	{
		TArray<FMcSpawnEntry> Known;
		int32 TotalWeight = 0;
		for (const FMcSpawnEntry& Entry : Biomes.GetSpawns(BiomeChunk->GetBiome(8, 64, 8), TEXT("creature")))
		{
			if (FMcMobType::Find(Entry.Kind))
			{
				Known.Add(Entry);
				TotalWeight += Entry.Weight;
			}
		}
		if (TotalWeight > 0)
		{
			const int32 Roll = Local.RandHelper(TotalWeight);
			Kind = PickSpawn(Known, Roll);
			for (const FMcSpawnEntry& Entry : Known)
			{
				if (Entry.Kind == Kind)
				{
					Herd = Local.RandRange(FMath::Max(1, Entry.MinCount), FMath::Max(Entry.MinCount, Entry.MaxCount));
				}
			}
		}
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
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
		if (Y && IsSpawnGround(Kind, Registry.GetBlockOf(World.GetBlock(FIntVector(X, Y.GetValue() - 1, Z))).Name))
		{
			if (FMcMob* Born = SpawnMob(Kind, FVector(X + 0.5, Y.GetValue(), Z + 0.5)))
			{
				// About one in twenty of a natural herd is young.
				if (FMcMob::CanBeYoung(Kind) && Local.FRand() < 0.05f)
				{
					Born->SetYoung(Local.RandRange(FMcMob::YoungSpan / 4, FMcMob::YoungSpan));
				}
			}
			++Placed;
		}
	}
}

void FMcEntityHost::SpawnHostiles(const TArray<FVector>& Players)
{
	if (Rules.IsPeaceful() || !Rules.bDoMobSpawning || Players.IsEmpty() || NumHostile() >= HostileCapPerPlayer * Players.Num())
	{
		return;
	}
	const FVector& Player = Players[Random.RandHelper(Players.Num())];
	const double Angle = Random.FRandRange(0.0, 2.0 * PI);
	const double Distance = Random.FRandRange(MinSpawnDistance, MaxSpawnDistance);
	const int32 X = FMath::FloorToInt32(Player.X + FMath::Cos(Angle) * Distance);
	const int32 Z = FMath::FloorToInt32(Player.Z + FMath::Sin(Angle) * Distance);
	TOptional<int32> Y = FindStandingY(X, Z, FMath::FloorToInt32(Player.Y), 24);
	// Where there is water over the spot instead of a floor to stand on, a drowned can come: its feet and head in the water just
	// under the surface (the rest of the monsters only spawn on ground, so a water spot is for the drowned alone).
	bool bInWater = false;
	if (!Y)
	{
		Y = FindSwimmingY(X, Z, FMath::FloorToInt32(Player.Y), 24);
		bInWater = Y.IsSet();
	}
	if (!Y)
	{
		return;
	}
	const FIntVector Cell(X, Y.GetValue(), Z);
	const FVector Pos(X + 0.5, Y.GetValue(), Z + 0.5);

	// What the biome there spawns, or the list of a structure with spawn_overrides where the spot is in its box. A kind we have no mob for yet (a
	// ghast, a piglin) uses up the roll and nothing comes, so the others keep the share they have in the game.
	FMcStructureZone Here;
	const bool bStructure = FMcStructureSpawns::Get().FindMonsterZone(Cell, Here);
	const TConstArrayView<FMcSpawnEntry> Monsters = bStructure ? TConstArrayView<FMcSpawnEntry>(Here.Monsters) : GetMonsterSpawns(Cell);
	int32 TotalWeight = 0;
	for (const FMcSpawnEntry& Entry : Monsters)
	{
		TotalWeight += Entry.Weight;
	}
	if (TotalWeight <= 0)
	{
		return;
	}
	const FName Kind = PickSpawn(Monsters, Random.RandHelper(TotalWeight));
	if (bInWater != (Kind == TEXT("minecraft:drowned")))
	{
		return;
	}
	// Dark enough: no block light, and sky light of 7 or less (night in the open, or anywhere the sky can't reach). A
	// pillager only minds block light, 8 or less (minecraft.wiki, "Pillager").
	if (Kind == TEXT("minecraft:pillager") ? Manager.EstimateBlockLight(Cell) > 8 : !Manager.IsDarkEnoughToSpawn(Cell))
	{
		return;
	}
	if (Kind == TEXT("minecraft:slime") || Kind == TEXT("minecraft:magma_cube"))
	{
		// A slime comes only low down in a slime chunk; both are of a size that fits the room there. (Magma cube sizes 1, 2 and 4
		// are the game's; their shares were not measured.)
		if (Kind == TEXT("minecraft:slime") && (Y.GetValue() >= SlimeMaxY || !IsSlimeChunk(Seed, McCoords::BlockToChunk(X), McCoords::BlockToChunk(Z))))
		{
			return;
		}
		if (FMcMob* Slime = SpawnMob(Kind, Pos))
		{
			static const int32 Sizes[] = { 1, 2, 4 };
			for (int32 Size = Sizes[Random.RandHelper(UE_ARRAY_COUNT(Sizes))]; Size >= 1; Size /= 2)
			{
				Slime->SetSize(Size);
				if (!McPhysics::Collides(World, Slime->Body.GetBox()))
				{
					return;
				}
			}
			Slime->bRemoved = true;
		}
		return;
	}
	SpawnMob(Kind, Pos);
}

FName FMcEntityHost::PickSpawn(TConstArrayView<FMcSpawnEntry> Entries, int32 Roll)
{
	for (const FMcSpawnEntry& Entry : Entries)
	{
		if (Roll < Entry.Weight)
		{
			return Entry.Kind;
		}
		Roll -= Entry.Weight;
	}
	return NAME_None;
}

TConstArrayView<FMcSpawnEntry> FMcEntityHost::GetMonsterSpawns(const FIntVector& Cell) const
{
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	const FMcChunk* Chunk = World.FindChunk(FIntPoint(McCoords::BlockToChunk(Cell.X), McCoords::BlockToChunk(Cell.Z)));
	if (!Biomes.IsLoaded() || !Chunk)
	{
		return FallbackMonsters;
	}
	return Biomes.GetSpawns(Chunk->GetBiome(Cell.X & 15, Cell.Y, Cell.Z & 15), TEXT("monster"));
}

void FMcEntityHost::Tick(const TArray<FVector>& Players, double DayTicks)
{
	++Ticks;
	const int32 TimeOfDay = static_cast<int32>(FMath::Fmod(DayTicks, 24000.0));
	// Sunrise is 0; the sky is dark from 13000 to 23000.
	Manager.bDaylight = TimeOfDay < 13000 || TimeOfDay >= 23000;
	Manager.Difficulty = Rules.Difficulty;
	Manager.TimeOfDay = TimeOfDay;

	Manager.PlayerPositions = Players;

	// Every 10 minutes or so a raid party may come to a player, from the fifth day on and in daylight.
	if (NextPatrolTick == 0)
	{
		NextPatrolTick = PatrolIntervalTicks + Random.RandHelper(1200);
	}
	if (Ticks >= NextPatrolTick)
	{
		NextPatrolTick = Ticks + PatrolIntervalTicks + Random.RandHelper(1200);
		if (!Players.IsEmpty() && Rules.bDoMobSpawning && !Rules.IsPeaceful() && DayTicks / 24000.0 >= PatrolFirstDay && TimeOfDay < 12000 && Random.RandHelper(5) == 0)
		{
			SpawnPatrol(Players[Random.RandHelper(Players.Num())]);
		}
	}
	TickRaids();

	// Every game day a trader may come to a player: a 25% chance, up from there by 25% each day it doesn't, to 75%, and
	// none while one is about.
	if (Ticks % WanderingTraderIntervalTicks == 0 && !Players.IsEmpty() && Rules.bDoMobSpawning)
	{
		bool bAbout = false;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			bAbout |= !Entity->bRemoved && Entity->GetType() == FMcWanderingTrader::StaticType();
		}
		if (!bAbout)
		{
			if (Random.FRand() * 100.0f < TraderChance && SpawnWanderingTrader(Players[Random.RandHelper(Players.Num())]))
			{
				TraderChance = 25;
			}
			else
			{
				TraderChance = FMath::Min(75, TraderChance + 25);
			}
		}
	}

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

	// The mobs structures came with (a swamp hut's witch) are born once their chunk is here.
	if (Ticks % 20 == 0)
	{
		FMcStructureSpawns::Get().TakeMobs([this](const FMcStructureMob& Born)
		{
			const FMcMobType* Type = FMcMobType::Find(Born.Kind);
			if (!Type)
			{
				return true;
			}
			if (!World.HasChunk(FIntPoint(McCoords::BlockToChunk(FMath::FloorToInt32(Born.Position.X)), McCoords::BlockToChunk(FMath::FloorToInt32(Born.Position.Z)))) || (Type->IsHostile() && Rules.IsPeaceful()))
			{
				return false;
			}
			if (FMcMob* Mob = SpawnMob(Born.Kind, Born.Position))
			{
				Mob->bPersistent = true;
			}
			return true;
		});
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
			if (!Mob.Type.IsHostile() || Mob.RaidId != 0 || Mob.bPersistent)
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
		// Mobs that came in some other way (a save, a summon) are cleared too.
		if (Rules.IsPeaceful())
		{
			RemoveHostiles();
		}
		SpawnHostiles(Players);
	}
#if !UE_BUILD_SHIPPING
	// Development: -McSpawnMobs=<kind>,<n> puts n of a kind in a ring 5 blocks from the first player, 5 seconds in
	// (or when the first player turns up, if later).
	if (!bDevSpawnDone && Ticks >= 100 && !Players.IsEmpty())
	{
		bDevSpawnDone = true;
		// -McMobRow=cow,pig,... puts one of each frozen in a row 5.5 blocks south of the player, facing east (yaw 90), 2 apart;
		// -McMobRowUp=<blocks> lifts them off the ground to be seen from underneath.
		FString Row;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McMobRow="), Row, false))
		{
			TArray<FString> Kinds;
			Row.ParseIntoArray(Kinds, TEXT(","));
			double Up = 0.0;
			FParse::Value(FCommandLine::Get(), TEXT("-McMobRowUp="), Up);
			float RowYaw = 90.0f;
			FParse::Value(FCommandLine::Get(), TEXT("-McMobRowYaw="), RowYaw);
				// -McMobRowAt=x,z puts the row's middle at that block column instead of in front of the player.
				FVector2D RowAt(Players[0].X, Players[0].Z + 5.5);
				FString At, AtX, AtZ;
				if (FParse::Value(FCommandLine::Get(), TEXT("-McMobRowAt="), At, false) && At.Split(TEXT(","), &AtX, &AtZ))
				{
					RowAt = FVector2D(FCString::Atod(*AtX), FCString::Atod(*AtZ));
				}
			for (int32 Index = 0; Index < Kinds.Num(); ++Index)
			{
				const double X = RowAt.X + (Index - (Kinds.Num() - 1) * 0.5) * 2.0;
				const int32 Z = FMath::FloorToInt32(RowAt.Y);
				if (const TOptional<int32> Y = FindStandingY(FMath::FloorToInt32(X), Z, FMath::FloorToInt32(Players[0].Y), 24))
				{
					if (FMcMob* Spawned = SpawnMob(FName(*(TEXT("minecraft:") + Kinds[Index])), FVector(FMath::FloorToInt32(X) + 0.5, Y.GetValue() + Up, Z + 0.5)))
					{
						Spawned->bFrozen = true;
						UE_LOG(LogMcServer, Display, TEXT("McMobRow: %s spawned at %.1f %d %d, id %d"), *Kinds[Index], X, Y.GetValue(), Z, Spawned->Id);
						if (FParse::Param(FCommandLine::Get(), TEXT("McMobRowBaby")) && FMcMob::CanBeYoung(Spawned->GetType()))
						{
							Spawned->SetYoung(FMcMob::YoungSpan);
						}
						Spawned->Yaw = RowYaw;
						// -McMobRowPose=<n> holds a raider's arms in that pose (McMob ArmPose).
						FParse::Value(FCommandLine::Get(), TEXT("-McMobRowPose="), Spawned->ArmPose);
						// -McMobRowHeld=<item> puts that item in its right hand.
						FString Held;
						if (FParse::Value(FCommandLine::Get(), TEXT("-McMobRowHeld="), Held))
						{
							Spawned->HeldItem = FName(*(TEXT("minecraft:") + Held));
						}
						// -McMobRowTame, -McMobRowSit, -McMobRowAngry and -McMobRowCollar=<dye 0..15> set a wolf's state.
						Spawned->bTamed = FParse::Param(FCommandLine::Get(), TEXT("McMobRowTame"));
						Spawned->bSitting = FParse::Param(FCommandLine::Get(), TEXT("McMobRowSit"));
						Spawned->AngerTicks = FParse::Param(FCommandLine::Get(), TEXT("McMobRowAngry")) ? 1200 : 0;
						FParse::Value(FCommandLine::Get(), TEXT("-McMobRowCollar="), Spawned->CollarColor);
						// -McMobRowLook=<n> picks the coat or breed (a wolf's 3 is the pale one a vanilla summon gets on a flat plain).
						FParse::Value(FCommandLine::Get(), TEXT("-McMobRowLook="), Spawned->LookSeed);
						if (Spawned->Type.Name == FName(TEXT("minecraft:wolf")))
						{
							Spawned->UpdateWolfPose();
						}
					}
				}
			}
		}
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
					int32 X = FMath::FloorToInt32(Players[0].X + FMath::Cos(Angle) * 5.0);
					int32 Z = FMath::FloorToInt32(Players[0].Z + FMath::Sin(Angle) * 5.0);
					if (Kind == TEXT("villager"))
					{
						// Villagers stand in a row in front of the camera (facing south, +Z), 6.5 blocks away, for a picture
						// of each one with its job site.
						X = FMath::FloorToInt32(Players[0].X + (Index - (N - 1) * 0.5) * 1.8);
						Z = FMath::FloorToInt32(Players[0].Z + (FParse::Param(FCommandLine::Get(), TEXT("McTrade")) ? 2.0 : 6.5));
					}
					if (const TOptional<int32> Y = FindStandingY(X, Z, FMath::FloorToInt32(Players[0].Y), 24))
					{
						FMcMob* Spawned = SpawnMob(FName(*(TEXT("minecraft:") + Kind)), FVector(X + 0.5, Y.GetValue(), Z + 0.5));
						if (Spawned && Spawned->GetType() == FMcVillager::StaticType())
						{
							// A ring of villagers shows every biome type, and every fifth one is a baby.
							FMcVillager& Villager = static_cast<FMcVillager&>(*Spawned);
							Villager.SetVariant(McVillager::GetVariants()[Index % McVillager::GetVariants().Num()]);
							Villager.SetBaby(Index % 5 == 4);
							if (FParse::Param(FCommandLine::Get(), TEXT("McTrade")))
							{
								// -McTrade: a frozen librarian two blocks south, for -McTrade to open its trades.
								Villager.SetProfession(TEXT("minecraft:librarian"));
								Villager.SetLevelAndXp(2, 30);
								Villager.bFrozen = true;
							}
						}
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
	FinishCures();
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

FMcMob* FMcEntityHost::FindMob(const FVector& Eye, const FVector& Direction, float Reach)
{
	// The nearest living entity whose box the ray enters within Reach.
	// The direction comes from a client: a NaN or empty one hits nothing.
	if (Eye.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero())
	{
		return nullptr;
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
	return Best;
}

bool FMcEntityHost::Attack(const FVector& Eye, const FVector& Direction, float Reach, float Damage)
{
	FMcMob* Best = FindMob(Eye, Direction, Reach);
	if (!Best)
	{
		return false;
	}
	Best->Damage(Damage, &Eye);
	return true;
}
