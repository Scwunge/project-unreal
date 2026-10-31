#include "McEntityManager.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McMob.h"
#include "Serialization/Archive.h"

bool FMcEntity::Serialize(FArchive& Ar)
{
	// Doubles and floats as they are, so a save restores exactly what was there.
	Ar << Body.Position.X << Body.Position.Y << Body.Position.Z;
	Ar << Body.Velocity.X << Body.Velocity.Y << Body.Velocity.Z;
	Ar << Yaw << Pitch << Age;
	uint8 bGround = Body.bOnGround ? 1 : 0;
	Ar << bGround;
	Body.bOnGround = bGround != 0;
	if (Ar.IsLoading())
	{
		return !Ar.IsError() && !Body.Position.ContainsNaN() && !Body.Velocity.ContainsNaN();
	}
	return !Ar.IsError();
}

bool FMcItemEntity::Serialize(FArchive& Ar)
{
	if (!FMcEntity::Serialize(Ar))
	{
		return false;
	}
	Ar << PickupDelay;
	return Stack.Serialize(Ar) && !Ar.IsError() && (Ar.IsSaving() || !Stack.IsEmpty());
}

const FName& FMcItemEntity::StaticType()
{
	static const FName Name(TEXT("minecraft:item"));
	return Name;
}

FMcItemEntity::FMcItemEntity()
{
	Body.Width = 0.25f;
	Body.Height = 0.25f;
	Body.StepHeight = 0.0f;
}

void FMcItemEntity::Tick(FMcEntityManager& Manager)
{
	FMcBlockWorld& World = Manager.GetWorld();
	if (PickupDelay > 0)
	{
		--PickupDelay;
	}
	++Age;
	if (Age >= LifeTicks)
	{
		bRemoved = true;
		return;
	}
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FIntVector Cell(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y + 0.1), FMath::FloorToInt32(Body.Position.Z));
	if (!World.HasChunk(FIntPoint(Cell.X >> 4, Cell.Z >> 4)))
	{
		return;
	}
	const EMcFluid Fluid = Shapes.GetFluid(World.GetBlock(Cell));
	if (Fluid == EMcFluid::Lava)
	{
		bRemoved = true;
		return;
	}

	if (Fluid == EMcFluid::Water)
	{
		// Items float up slowly and are slowed sideways.
		Body.Velocity.Y = FMath::Min(Body.Velocity.Y + 0.0005, 0.06);
		Body.Velocity.X *= 0.99;
		Body.Velocity.Z *= 0.99;
	}
	else
	{
		Body.Velocity.Y -= 0.04;
	}
	McPhysics::Move(World, Body, Body.Velocity, false);

	double Friction = 0.98;
	if (Body.bOnGround)
	{
		const FIntVector Under(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y - 0.5), FMath::FloorToInt32(Body.Position.Z));
		Friction = Shapes.GetSlipperiness(World.GetBlock(Under)) * 0.98;
	}
	Body.Velocity.X *= Friction;
	Body.Velocity.Z *= Friction;
	Body.Velocity.Y *= 0.98;
	if (Body.bOnGround && Body.Velocity.Y < 0.0)
	{
		Body.Velocity.Y *= -0.5;
	}

	// Merge with a like stack close by. Only a stack at rest looks, and only every few ticks (staggered by id so they
	// don't all search on the same one): the search scans every entity.
	if (Body.bOnGround && (Age + Id) % MergeIntervalTicks == 0 && !bRemoved)
	{
		const int32 MaxStack = Manager.StackSizeOf ? Manager.StackSizeOf(Stack.Item) : DefaultStackSize;
		TArray<FMcEntity*> Near;
		Manager.FindInBox(Body.GetBox().ExpandBy(FVector(0.5, 0.0, 0.5)), Near);
		for (FMcEntity* Other : Near)
		{
			FMcItemEntity* Item = Other == this || Other->bRemoved || Other->GetType() != StaticType() ? nullptr : static_cast<FMcItemEntity*>(Other);
			if (!Item || !Item->Stack.IsSameItemSameComponents(Stack) || Item->Stack.Count + Stack.Count > MaxStack || Item->Age < Age)
			{
				continue;
			}
			// The older one absorbs the newer one.
			Item->Stack.Count += Stack.Count;
			Item->PickupDelay = FMath::Max(Item->PickupDelay, PickupDelay);
			bRemoved = true;
			break;
		}
	}
}

FMcEntity& FMcEntityManager::Add(TUniquePtr<FMcEntity> Entity)
{
	Entity->Id = NextId++;
	Entity->Owner = this;
	Entities.Add(MoveTemp(Entity));
	return *Entities.Last();
}

FMcItemEntity& FMcEntityManager::DropItem(const FVector& Pos, const FMcItemStack& Stack, int32 PickupDelay)
{
	TUniquePtr<FMcItemEntity> Item = MakeUnique<FMcItemEntity>();
	Item->Stack = Stack;
	Item->PickupDelay = PickupDelay;
	Item->Body.Position = Pos;
	Item->Body.Velocity = FVector(Random.FRandRange(-0.1, 0.1), 0.2, Random.FRandRange(-0.1, 0.1));
	FMcItemEntity* Raw = Item.Get();
	Add(MoveTemp(Item));
	return *Raw;
}

void FMcEntityManager::Tick()
{
	// Entities added while ticking (a mob dropping an item) join the next tick.
	const int32 Count = Entities.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (!Entities[Index]->bRemoved)
		{
			Entities[Index]->Tick(*this);
		}
	}
	Entities.RemoveAll([](const TUniquePtr<FMcEntity>& Entity) { return Entity->bRemoved; });
}

bool FMcEntityManager::IsSkyExposed(const FIntVector& Pos) const
{
	const int32 Top = World.GetDimension().MinY + World.GetDimension().Height;
	// The column is in one chunk: look once, not at every block.
	if (!World.HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
	{
		return false;
	}
	for (int32 Y = Pos.Y + 1; Y < Top; ++Y)
	{
		const FMcStateId State = World.GetBlock(FIntVector(Pos.X, Y, Pos.Z));
		if (FMcBlockShapes::Get().HasCollision(State) || FMcBlockShapes::Get().GetFluid(State) != EMcFluid::None)
		{
			return false;
		}
	}
	return true;
}

FMcEntity* FMcEntityManager::Find(int32 Id)
{
	for (const TUniquePtr<FMcEntity>& Entity : Entities)
	{
		if (Entity->Id == Id)
		{
			return Entity.Get();
		}
	}
	return nullptr;
}

void FMcEntityManager::FindInBox(const FBox& Box, TArray<FMcEntity*>& Out)
{
	for (const TUniquePtr<FMcEntity>& Entity : Entities)
	{
		if (!Entity->bRemoved && Entity->Body.GetBox().Intersect(Box))
		{
			Out.Add(Entity.Get());
		}
	}
}

TArray<FMcItemStack> FMcEntityManager::CollectItems(const FBox& PlayerBox)
{
	TArray<FMcItemStack> Taken;
	TArray<FMcEntity*> Near;
	FindInBox(PlayerBox.ExpandBy(FVector(1.0, 0.5, 1.0)), Near);
	for (FMcEntity* Entity : Near)
	{
		if (Entity->GetType() != FMcItemEntity::StaticType())
		{
			continue;
		}
		FMcItemEntity* Item = static_cast<FMcItemEntity*>(Entity);
		if (Item->PickupDelay <= 0)
		{
			Taken.Add(Item->Stack);
			Item->bRemoved = true;
		}
	}
	return Taken;
}

int32 FMcEntityManager::EstimateSkyLight(const FIntVector& Cell) const
{
	const int32 Darken = bDaylight ? 0 : 11;
	int32 Light = 0;
	if (IsSkyExposed(Cell))
	{
		Light = 15;
	}
	else
	{
		// Light creeping in from an opening to the side (a cave mouth, a window): one less for each block.
		static const FIntVector Sides[4] = { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		for (const FIntVector& Side : Sides)
		{
			for (int32 Distance = 1; Distance <= 7; ++Distance)
			{
				const FIntVector At = Cell + Side * Distance;
				if (!World.HasChunk(FIntPoint(At.X >> 4, At.Z >> 4)) || Shapes.HasCollision(World.GetBlock(At)))
				{
					break;
				}
				if (IsSkyExposed(At))
				{
					Light = FMath::Max(Light, 15 - Distance);
					break;
				}
			}
		}
	}
	return FMath::Max(0, Light - Darken);
}

int32 FMcEntityManager::EstimateBlockLight(const FIntVector& Cell) const
{
	FMcBlockLight& Emitters = FMcBlockLight::Get();
	if (!Emitters.EnsureLoaded())
	{
		return 0;
	}
	const int32 MinY = World.GetDimension().MinY;
	const int32 MaxY = World.GetDimension().MaxY();
	int32 Light = 0;
	for (int32 DX = -14; DX <= 14; ++DX)
	{
		for (int32 DZ = -14; DZ <= 14; ++DZ)
		{
			const int32 Flat = FMath::Abs(DX) + FMath::Abs(DZ);
			if (Flat > 14 || !World.HasChunk(FIntPoint((Cell.X + DX) >> 4, (Cell.Z + DZ) >> 4)))
			{
				continue;
			}
			for (int32 DY = -(14 - Flat); DY <= 14 - Flat; ++DY)
			{
				const int32 Y = Cell.Y + DY;
				if (Y < MinY || Y > MaxY)
				{
					continue;
				}
				const int32 Level = Emitters.GetEmission(World.GetBlock(FIntVector(Cell.X + DX, Y, Cell.Z + DZ))) - (Flat + FMath::Abs(DY));
				Light = FMath::Max(Light, Level);
			}
		}
	}
	return Light;
}

int32 FMcEntityManager::EstimateLightLevel(const FIntVector& Cell) const
{
	return FMath::Max(EstimateSkyLight(Cell), EstimateBlockLight(Cell));
}

bool FMcEntityManager::IsDarkEnoughToSpawn(const FIntVector& Cell) const
{
	return EstimateSkyLight(Cell) <= 7 && EstimateBlockLight(Cell) == 0;
}

void FMcEntityManager::Explode(const FVector& Center, float Power)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	TArray<FIntVector> Cells;
	McExplosion::Collect(World, Center, Power, Random, Cells);
	// Loud (carries 64 blocks) and low, a little different each time.
	PlaySound(FName("entity.generic.explode"), Center, 4.0f, McSound::GetVoicePitch(Random.FRand(), Random.FRand()) * 0.7f);

	// Everything standing near is hurt first, by the blocks as they were: the ones about to go still shield.
	const double Radius = Power * 2.0;
	TArray<FMcEntity*> Near;
	FindInBox(FBox(Center - FVector(Radius), Center + FVector(Radius)), Near);
	for (FMcEntity* Entity : Near)
	{
		if (Entity->bRemoved)
		{
			continue;
		}
		if (Entity->GetType() == FMcItemEntity::StaticType())
		{
			// Lying items are pushed like anything else, and a stack a blast hurts by 5 or more is destroyed (an item has
			// 5 health); the ones that resist explosions (netherite and the nether star) are only pushed.
			FMcItemEntity& Item = static_cast<FMcItemEntity&>(*Entity);
			const double Distance = FVector::Dist(Item.Body.Position, Center);
			const float Exposure = McExplosion::GetExposure(World, Center, Item.Body.GetBox());
			const float Damage = McExplosion::GetDamage(Power, Distance, Exposure);
			if (Damage > 0.0f)
			{
				const FString Name = Item.Stack.Item.ToString();
				const bool bResists = Name.Contains(TEXT("netherite")) || Name == TEXT("minecraft:ancient_debris") || Name == TEXT("minecraft:nether_star");
				if (Damage >= FMcItemEntity::Health && !bResists)
				{
					Item.bRemoved = true;
				}
				else
				{
					const double Push = (1.0 - Distance / Radius) * Exposure;
					Item.Body.Velocity += (Item.Body.Position + FVector(0.0, Item.Body.Height * 0.5, 0.0) - Center).GetSafeNormal() * Push;
				}
			}
			continue;
		}
		if (!Entity->IsLiving())
		{
			continue;
		}
		FMcMob& Mob = static_cast<FMcMob&>(*Entity);
		const double Distance = FVector::Dist(Mob.Body.Position, Center);
		const float Exposure = McExplosion::GetExposure(World, Center, Mob.Body.GetBox());
		const float Damage = McExplosion::GetDamage(Power, Distance, Exposure);
		if (Damage > 0.0f)
		{
			Mob.HurtTime = 0;
			Mob.Damage(Damage, &Center);
			if (!Mob.bRemoved)
			{
				// Shoved away from the blast, harder the closer and more open to it.
				const double Push = (1.0 - Distance / Radius) * Exposure;
				const FVector Away = (Mob.Body.Position + FVector(0.0, Mob.Body.Height * 0.5, 0.0) - Center).GetSafeNormal();
				Mob.Body.Velocity += Away * Push;
			}
		}
	}
	for (const FVector& Player : PlayerPositions)
	{
		const FBox Box(Player + FVector(-0.3, 0.0, -0.3), Player + FVector(0.3, 1.8, 0.3));
		const float Damage = McExplosion::GetDamage(Power, FVector::Dist(Player, Center), McExplosion::GetExposure(World, Center, Box));
		if (Damage > 0.0f)
		{
			HurtPlayer(Player, Damage);
		}
	}

	const FMcStateId Air = Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
	TArray<FMcDestroyedBlock> Destroyed;
	Destroyed.Reserve(Cells.Num());
	for (const FIntVector& Cell : Cells)
	{
		FMcDestroyedBlock Block;
		Block.Pos = Cell;
		Block.State = World.SetBlock(Cell, Air);
		if (Block.State != Air)
		{
			Destroyed.Add(Block);
		}
	}
	if (OnBlocksDestroyed && !Destroyed.IsEmpty())
	{
		OnBlocksDestroyed(Destroyed, Power);
	}
}
