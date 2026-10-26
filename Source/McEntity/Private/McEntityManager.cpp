#include "McEntityManager.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

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

	// Merge with a like stack close by (every other tick, as the game does, to save work).
	if (Age % 2 == 0 && !bRemoved)
	{
		const int32 MaxStack = Manager.StackSizeOf ? Manager.StackSizeOf(Stack.Item) : DefaultStackSize;
		TArray<FMcEntity*> Near;
		Manager.FindInBox(Body.GetBox().ExpandBy(FVector(0.5, 0.0, 0.5)), Near);
		for (FMcEntity* Other : Near)
		{
			FMcItemEntity* Item = Other == this || Other->bRemoved || Other->GetType() != GetType() ? nullptr : static_cast<FMcItemEntity*>(Other);
			if (!Item || Item->Stack.Item != Stack.Item || Item->Stack.Count + Stack.Count > MaxStack || Item->Age < Age)
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
	for (int32 Y = Pos.Y + 1; Y < Top; ++Y)
	{
		if (!World.HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
		{
			return false;
		}
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
		if (Entity->GetType() != TEXT("minecraft:item"))
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
