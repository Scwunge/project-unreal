#include "McVehicle.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McMob.h"
#include "McRails.h"
#include "Serialization/Archive.h"

namespace
{
	bool HasWater(FMcStateId State)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (!Registry.IsValidState(State))
		{
			return false;
		}
		const FName Name = Registry.GetBlockOf(State).Name;
		return Name == TEXT("minecraft:water") || Name == TEXT("minecraft:bubble_column") || Registry.GetPropertyValue(State, TEXT("waterlogged")) == TEXT("true");
	}

	// How high the water in this cell stands, 0 to 1 of the block.
	double WaterHeight(FMcStateId State)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FName Level = Registry.GetPropertyValue(State, TEXT("level"));
		if (Level.IsNone())
		{
			return 0.888889;
		}
		const int32 Value = FCString::Atoi(*Level.ToString());
		return Value >= 8 ? 1.0 : (8 - Value) / 9.0;
	}

	double Friction(FName Block)
	{
		const FString Name = Block.ToString();
		if (Name == TEXT("minecraft:blue_ice"))
		{
			return 0.989;
		}
		if (Name == TEXT("minecraft:ice") || Name == TEXT("minecraft:packed_ice") || Name == TEXT("minecraft:frosted_ice"))
		{
			return 0.98;
		}
		// Stone, from vanilla_log vehicle_log: a boat pushed at 0.3 goes 0.18, 0.108, 0.0648.
		return 0.6;
	}
}

// ---- Any vehicle ----

bool FMcVehicle::OnAttacked(float Damage, bool bCreative)
{
	// A minecart goes at the first blow; a boat takes ten times the damage and goes past 40.
	Wear += Damage * GetWearPerDamage();
	if (!bCreative && Wear <= 40.0f)
	{
		return true;
	}
	bRemoved = true;
	DropInventory();
	if (!bCreative && Owner)
	{
		Owner->DropItem(Body.Position + FVector(0.0, 0.3, 0.0), FMcItemStack(GetItemName(), 1));
	}
	return true;
}

const TSharedPtr<FMcContainer>& FMcVehicle::GetInventory()
{
	if (!Inventory.IsValid() && GetInventorySlots() > 0)
	{
		Inventory = MakeShared<FMcContainer>(GetInventorySlots());
	}
	return Inventory;
}

void FMcVehicle::DropInventory()
{
	if (!Inventory.IsValid() || !Owner)
	{
		return;
	}
	for (FMcItemStack& Stack : Inventory->GetAll())
	{
		if (!Stack.IsEmpty())
		{
			Owner->DropItem(Body.Position + FVector(0.0, 0.3, 0.0), Stack);
			Stack = FMcItemStack();
		}
	}
}

bool FMcVehicle::Serialize(FArchive& Ar)
{
	if (!FMcEntity::Serialize(Ar) || (Ar << Wear, Ar.IsError()))
	{
		return false;
	}
	if (GetInventorySlots() > 0)
	{
		for (FMcItemStack& Stack : GetInventory()->GetAll())
		{
			if (!Stack.Serialize(Ar))
			{
				return false;
			}
		}
	}
	return !Ar.IsError();
}

TUniquePtr<FMcVehicle> FMcVehicle::Create(FName Type)
{
	if (const TOptional<FMcMinecartEntity::EVariant> Variant = FMcMinecartEntity::ParseType(Type))
	{
		return MakeUnique<FMcMinecartEntity>(Variant.GetValue());
	}
	if (Type == FMcBoatEntity::StaticType() || Type == FMcBoatEntity::StaticChestType())
	{
		return MakeUnique<FMcBoatEntity>(Type == FMcBoatEntity::StaticChestType());
	}
	// The game's own names for the boats by wood: "minecraft:cherry_boat", "minecraft:bamboo_chest_raft".
	return CreateFromItem(Type);
}

TUniquePtr<FMcVehicle> FMcVehicle::CreateFromItem(FName Item)
{
	if (const TOptional<FMcMinecartEntity::EVariant> Variant = FMcMinecartEntity::ParseType(Item))
	{
		return MakeUnique<FMcMinecartEntity>(Variant.GetValue());
	}
	FString Name = Item.ToString();
	if (!Name.RemoveFromStart(TEXT("minecraft:")))
	{
		return nullptr;
	}
	const bool bRaft = Name.EndsWith(TEXT("_raft"));
	if (!bRaft && !Name.EndsWith(TEXT("_boat")))
	{
		return nullptr;
	}
	Name.LeftChopInline(5);
	const bool bChest = Name.EndsWith(TEXT("_chest"));
	if (bChest)
	{
		Name.LeftChopInline(6);
	}
	const FName Wood(*(TEXT("minecraft:") + Name));
	if (!FMcBoatEntity::GetWoods().Contains(Wood) || bRaft != (Wood == TEXT("minecraft:bamboo")))
	{
		return nullptr;
	}
	TUniquePtr<FMcBoatEntity> Boat = MakeUnique<FMcBoatEntity>(bChest);
	Boat->WoodName = Wood;
	return Boat;
}

bool FMcVehicle::IsVehicleType(FName Type)
{
	return Create(Type).IsValid();
}

bool FMcVehicle::IsVehicleItem(FName Item)
{
	return CreateFromItem(Item).IsValid();
}

// ---- Minecart ----

const FName& FMcMinecartEntity::StaticType()
{
	static const FName Name(TEXT("minecraft:minecart"));
	return Name;
}

namespace
{
	const TCHAR* const CartNames[] = { TEXT("minecraft:minecart"), TEXT("minecraft:chest_minecart"), TEXT("minecraft:furnace_minecart"),
		TEXT("minecraft:hopper_minecart"), TEXT("minecraft:tnt_minecart") };
}

FName FMcMinecartEntity::GetTypeName(EVariant Variant)
{
	return FName(CartNames[static_cast<int32>(Variant)]);
}

TOptional<FMcMinecartEntity::EVariant> FMcMinecartEntity::ParseType(FName Name)
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CartNames); ++Index)
	{
		if (Name == CartNames[Index])
		{
			return static_cast<EVariant>(Index);
		}
	}
	return {};
}

FMcMinecartEntity::FMcMinecartEntity(EVariant InVariant)
	: Variant(InVariant)
{
	Body.Width = 0.98f;
	Body.Height = 0.7f;
	Body.StepHeight = 0.0f;
}

bool FMcMinecartEntity::Serialize(FArchive& Ar)
{
	return FMcVehicle::Serialize(Ar) && (Ar << Fuel << PushDirection.X << PushDirection.Y << Fuse, !Ar.IsError());
}

FMcVehicle::EUse FMcMinecartEntity::UseItem(FName Item, const FVector& From)
{
	if (Variant == EVariant::Furnace && (Item == TEXT("minecraft:coal") || Item == TEXT("minecraft:charcoal")))
	{
		// Fed, it pushes away from whoever fed it.
		Fuel = FMath::Min(Fuel + FuelPerItem, 32000);
		const FVector2D Away(Body.Position.X - From.X, Body.Position.Z - From.Z);
		if (Away.SizeSquared() > 1e-6)
		{
			PushDirection = Away.GetSafeNormal();
		}
		return EUse::Consumed;
	}
	if (Variant == EVariant::Tnt && Item == TEXT("minecraft:flint_and_steel") && Fuse < 0)
	{
		Fuse = FuseTicks;
		return EUse::Kept;
	}
	return EUse::None;
}

bool FMcMinecartEntity::TickOnRail(FMcEntityManager& Manager, FMcBlockWorld& World)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	// The rail in the cart's own cell, or the one below it (the cart rides a little above the rail).
	FIntVector Cell(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z));
	if (!McRails::IsRail(World.GetBlock(Cell)))
	{
		Cell.Y -= 1;
		if (!McRails::IsRail(World.GetBlock(Cell)))
		{
			return false;
		}
	}
	const FMcStateId State = World.GetBlock(Cell);
	McRails::FTrack Track;
	if (!McRails::GetTrack(State, Track))
	{
		return false;
	}
	const FName Block = Registry.GetBlockOf(State).Name;
	const bool bPowered = Registry.GetPropertyValue(State, TEXT("powered")) == TEXT("true");

	// The track is a line from the middle of one end to the middle of the other, a block higher at a slope's top.
	const FVector Middle(Cell.X + 0.5, Cell.Y + RailLift, Cell.Z + 0.5);
	const FVector EndA = Middle + FVector(Track.EndA.X * 0.5, Track.bRiseA ? 1.0 : 0.0, Track.EndA.Z * 0.5);
	const FVector EndB = Middle + FVector(Track.EndB.X * 0.5, Track.bRiseB ? 1.0 : 0.0, Track.EndB.Z * 0.5);
	const FVector2D Line(EndB.X - EndA.X, EndB.Z - EndA.Z);
	const double Length = Line.Size();
	const FVector2D Along = Line / Length;

	// Onto the line, keeping to where the cart is along it.
	const double Where = FVector2D::DotProduct(FVector2D(Body.Position.X - EndA.X, Body.Position.Z - EndA.Z), Along) / Length;
	const double T = FMath::Clamp(Where, 0.0, 1.0);
	Body.Position = FVector(EndA.X + Line.X * Where, FMath::Lerp(EndA.Y, EndB.Y, T), EndA.Z + Line.Y * Where);
	Body.Velocity.Y = 0.0;
	Body.bOnGround = true;

	// The speed along the track, signed by the way it is going.
	FVector2D Move(Body.Velocity.X, Body.Velocity.Z);
	double Speed = Move.Size();
	const double Sign = FVector2D::DotProduct(Move, Along) >= 0.0 ? 1.0 : -1.0;
	FVector2D Velocity = Along * Sign * Speed;

	// A slope pulls the cart downhill.
	if (Track.IsAscending())
	{
		// Toward the high end.
		const FVector2D High = (Track.bRiseA ? FVector2D(EndA.X - EndB.X, EndA.Z - EndB.Z) : FVector2D(EndB.X - EndA.X, EndB.Z - EndA.Z)).GetSafeNormal();
		Velocity -= High * SlopePull;
	}

	// An unpowered powered rail holds the cart back before it moves: half its speed, or all of it under 0.03
	// (powered_log.txt: a cart at 0.156 moves 0.078, 0.037, 0.018 and stops).
	const bool bPoweredRail = Block == TEXT("minecraft:powered_rail");
	Speed = Velocity.Size();
	if (bPoweredRail && !bPowered)
	{
		Velocity = Speed < 0.03 ? FVector2D::ZeroVector : Velocity * 0.5;
	}
	if (Block == TEXT("minecraft:activator_rail") && bPowered)
	{
		// A powered activator rail throws the rider off, lights a TNT cart and stops a hopper cart picking things up.
		EjectRiders();
		bHopperLocked = true;
		if (Variant == EVariant::Tnt && Fuse < 0)
		{
			Fuse = FuseTicks;
		}
	}

	// A furnace cart with fuel drives itself the way it was pushed.
	const bool bDriving = Variant == EVariant::Furnace && Fuel > 0;
	if (bDriving)
	{
		const double Heading = Velocity.SizeSquared() > 1e-4 ? FVector2D::DotProduct(Velocity, Along) : FVector2D::DotProduct(PushDirection, Along);
		Velocity += Along * (Heading >= 0.0 ? 0.04 : -0.04);
	}

	// The speed limit is on how far it moves in a tick, not on its speed: powered_log.txt's cart on powered rail moves
	// 0.4 a tick while its speed goes on to 1.03.
	const double Fast = Velocity.Size();
	const double Cap = bDriving ? FurnaceSpeed : MaxSpeed;
	const FVector2D Step = Fast > Cap ? Velocity * (Cap / Fast) : Velocity;

	// Along the line to where it gets to, and if something is in the way, stop.
	const double Reach = FVector2D::DotProduct(Step, Along);
	const double NextWhere = Where + Reach / Length;
	const double NextT = FMath::Clamp(NextWhere, 0.0, 1.0);
	const FVector Target(EndA.X + Line.X * NextWhere, FMath::Lerp(EndA.Y, EndB.Y, NextT), EndA.Z + Line.Y * NextWhere);
	const FVector Before = Body.Position;
	Body.Velocity = FVector(Velocity.X, 0.0, Velocity.Y);
	McPhysics::Move(World, Body, Target - Before, false);
	if (Body.bHorizontalCollision)
	{
		Body.Velocity = FVector::ZeroVector;
	}
	else
	{
		// Friction comes after the move, and every block dropped adds a twentieth to the speed (a block climbed takes it
		// away). Fitted from vehicle_log.txt: on flat rail a cart at 0.4 goes to 2.9 then 3.284 with speeds 0.384, 0.36864;
		// down a slope from rest it moves 0.0078125, 0.0157, 0.0237 with speeds 1.01 times those; the tick it leaves the
		// slope, 0.3936 across with 0.1756 of drop, it keeps 0.96 * 0.3936 + 0.05 * 0.1756 = 0.3866.
		// Uphill it keeps 0.91 of its move (powered_log.txt: 0.0918 up the slope leaves 0.0836).
		const double Kept = (HasRider() ? 0.997 : 0.96) * Fast + 0.05 * (Before.Y - Body.Position.Y);
		Velocity = Fast > 1e-9 ? Velocity / Fast * Kept : FVector2D::ZeroVector;
		// A powered rail adds to what is left (powered_log.txt: 0.0792 = 0.02 * 0.96 + 0.06, 0.13603, ...); a cart standing
		// still against something at one end is pushed off from it at 0.02, and moves the tick after.
		if (bPoweredRail && bPowered)
		{
			if (Kept > 0.01)
			{
				Velocity += Velocity / Kept * PoweredBoost;
			}
			else
			{
				const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
				const bool bBlockedA = Shapes.HasCollision(World.GetBlock(Cell + Track.EndA));
				const bool bBlockedB = Shapes.HasCollision(World.GetBlock(Cell + Track.EndB));
				if (bBlockedA != bBlockedB)
				{
					const FVector2D Away = bBlockedA ? FVector2D(Track.EndB.X, Track.EndB.Z) : FVector2D(Track.EndA.X, Track.EndA.Z);
					Velocity = Away.GetSafeNormal() * 0.02;
				}
			}
		}
		// A furnace cart's own drive is held to its speed (not measured against vanilla yet).
		if (bDriving && Velocity.Size() > FurnaceSpeed)
		{
			Velocity *= FurnaceSpeed / Velocity.Size();
		}
		Body.Velocity = FVector(Velocity.X, 0.0, Velocity.Y);
	}
	if (Velocity.SizeSquared() > 1e-4)
	{
		Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Velocity.X, Velocity.Y));
	}
	OnRail = Cell;
	if (Block == TEXT("minecraft:detector_rail"))
	{
		Manager.SetDetector(Cell, true);
		LastDetector = Cell;
	}
	return true;
}

void FMcMinecartEntity::Tick(FMcEntityManager& Manager)
{
	TickRider();
	++Age;
	FMcBlockWorld& World = Manager.GetWorld();
	if (!World.HasChunk(FIntPoint(FMath::FloorToInt32(Body.Position.X) >> 4, FMath::FloorToInt32(Body.Position.Z) >> 4)))
	{
		return;
	}
	const TOptional<FIntVector> PreviousDetector = LastDetector;
	LastDetector.Reset();
	OnRail.Reset();
	bHopperLocked = false;
	if (!TickOnRail(Manager, World))
	{
		// Off the rails: it falls, and drags along the ground.
		Body.Velocity.Y -= Gravity;
		McPhysics::Move(World, Body, Body.Velocity, false);
		const double Drag = Body.bOnGround ? 0.5 : 0.95;
		Body.Velocity.X *= Drag;
		Body.Velocity.Z *= Drag;
		Body.Velocity.Y *= 0.95;
	}
	if (PreviousDetector.IsSet() && (!LastDetector.IsSet() || LastDetector.GetValue() != PreviousDetector.GetValue()))
	{
		Manager.SetDetector(PreviousDetector.GetValue(), false);
	}
	TickVariant(Manager);
	TickPassengers(Manager);
	PushMobs(Manager);
}

bool FMcMinecartEntity::CanCarryMob(const FMcMob& Mob) const
{
	return Variant == EVariant::Plain && FVector2D(Body.Velocity.X, Body.Velocity.Z).SizeSquared() > 0.01 * 0.01
		&& Mob.Type.Name != TEXT("minecraft:iron_golem");
}

void FMcMinecartEntity::PushMobs(FMcEntityManager& Manager)
{
	TArray<FMcEntity*> Near;
	Manager.FindInBox(Body.GetBox().ExpandBy(FVector(0.2, 0.0, 0.2)), Near);
	const FVector2D Moving(Body.Velocity.X, Body.Velocity.Z);
	for (FMcEntity* Entity : Near)
	{
		if (!Entity->IsLiving() || Entity->bRemoved || static_cast<FMcMob*>(Entity)->RidingId != 0)
		{
			continue;
		}
		// Away from the cart's middle, or the way it goes when they are on top of each other.
		FVector2D Away(Entity->Body.Position.X - Body.Position.X, Entity->Body.Position.Z - Body.Position.Z);
		const double Distance = Away.Size();
		Away = Distance > 0.01 ? Away / Distance : (Moving.SizeSquared() > 1e-6 ? Moving.GetSafeNormal() : FVector2D(1.0, 0.0));
		const double Push = 0.05 + Moving.Size() * 0.5;
		Entity->Body.Velocity.X += Away.X * Push;
		Entity->Body.Velocity.Z += Away.Y * Push;
	}
}

void FMcMinecartEntity::TickVariant(FMcEntityManager& Manager)
{
	switch (Variant)
	{
	case EVariant::Furnace:
		Fuel = FMath::Max(0, Fuel - 1);
		break;
	case EVariant::Tnt:
		if (Fuse >= 0 && --Fuse < 0)
		{
			// It goes off: stronger the faster it was going (4 to 5.5).
			const double Speed = FVector2D(Body.Velocity.X, Body.Velocity.Z).Size();
			Manager.Explode(Body.Position + FVector(0.0, 0.5, 0.0), 4.0f + static_cast<float>(FMath::Min(Speed / MaxSpeed, 1.0)) * 1.5f);
			bRemoved = true;
		}
		break;
	case EVariant::Hopper:
		if (!bHopperLocked && Age % 4 == 0)
		{
			// It takes in the items lying in it or just over it.
			TArray<FMcEntity*> Near;
			Manager.FindInBox(FBox(Body.Position + FVector(-0.49, 0.0, -0.49), Body.Position + FVector(0.49, 1.2, 0.49)), Near);
			for (FMcEntity* Other : Near)
			{
				if (Other->bRemoved || Other->GetType() != FMcItemEntity::StaticType())
				{
					continue;
				}
				FMcItemEntity& Item = static_cast<FMcItemEntity&>(*Other);
				const FMcItemStack Left = GetInventory()->AddItem(Item.Stack);
				if (Left.IsEmpty())
				{
					Item.bRemoved = true;
				}
				else
				{
					Item.Stack = Left;
				}
			}
		}
		break;
	default:
		break;
	}
}

// ---- Boat ----

const FName& FMcBoatEntity::StaticType()
{
	static const FName Name(TEXT("minecraft:boat"));
	return Name;
}

const FName& FMcBoatEntity::StaticChestType()
{
	static const FName Name(TEXT("minecraft:chest_boat"));
	return Name;
}

const FName& FMcBoatEntity::StaticRaftType()
{
	static const FName Name(TEXT("minecraft:bamboo_raft"));
	return Name;
}

const FName& FMcBoatEntity::StaticChestRaftType()
{
	static const FName Name(TEXT("minecraft:bamboo_chest_raft"));
	return Name;
}

bool FMcBoatEntity::IsBoatType(FName Type)
{
	return Type == StaticType() || Type == StaticChestType() || Type == StaticRaftType() || Type == StaticChestRaftType();
}

const TArray<FName>& FMcBoatEntity::GetWoods()
{
	static const TArray<FName> Woods = { TEXT("minecraft:oak"), TEXT("minecraft:spruce"), TEXT("minecraft:birch"), TEXT("minecraft:jungle"),
		TEXT("minecraft:acacia"), TEXT("minecraft:dark_oak"), TEXT("minecraft:mangrove"), TEXT("minecraft:cherry"), TEXT("minecraft:pale_oak"),
		TEXT("minecraft:bamboo") };
	return Woods;
}

FName FMcBoatEntity::GetItemName() const
{
	const bool bRaft = WoodName == TEXT("minecraft:bamboo");
	return FName(*(WoodName.ToString() + (bChest ? TEXT("_chest") : TEXT("")) + (bRaft ? TEXT("_raft") : TEXT("_boat"))));
}

FMcBoatEntity::FMcBoatEntity(bool bInChest)
	: bChest(bInChest)
{
	Body.Width = 1.375f;
	Body.Height = 0.5625f;
	Body.StepHeight = 0.0f;
}

bool FMcBoatEntity::Serialize(FArchive& Ar)
{
	FString Wood = WoodName.ToString();
	if (!FMcVehicle::Serialize(Ar) || (Ar << Wood, Ar.IsError()))
	{
		return false;
	}
	WoodName = FName(*Wood);
	return true;
}

void FMcVehicle::TickPassengers(FMcEntityManager& Manager)
{
	const auto FindMob = [&Manager](int32 MobId) -> FMcMob*
	{
		FMcEntity* Found = Manager.Find(MobId);
		return Found && !Found->bRemoved && Found->IsLiving() ? static_cast<FMcMob*>(Found) : nullptr;
	};
	MobPassengers.RemoveAll([&](int32 MobId)
	{
		const FMcMob* Mob = FindMob(MobId);
		return !Mob || Mob->RidingId != Id;
	});
	if (bEjectMobs)
	{
		bEjectMobs = false;
		BoardCooldown = 10;
		for (const int32 MobId : MobPassengers)
		{
			if (FMcMob* Mob = FindMob(MobId))
			{
				// Back up out of the hull, onto its floor.
				Mob->RidingId = 0;
				Mob->Body.Position.Y += Mob->Type.Height * 0.4;
			}
		}
		MobPassengers.Reset();
	}
	if (BoardCooldown > 0)
	{
		--BoardCooldown;
	}
	else if (HasFreeSeat())
	{
		TArray<FMcEntity*> Near;
		Manager.FindInBox(Body.GetBox().ExpandBy(FVector(0.2, 0.0, 0.2)), Near);
		for (FMcEntity* Entity : Near)
		{
			if (!HasFreeSeat())
			{
				break;
			}
			if (Entity->IsLiving() && !Entity->bRemoved)
			{
				FMcMob* Mob = static_cast<FMcMob*>(Entity);
				if (Mob->RidingId == 0 && CanCarryMob(*Mob))
				{
					Mob->RidingId = Id;
					MobPassengers.Add(Mob->Id);
				}
			}
		}
	}
	for (int32 Index = 0; Index < MobPassengers.Num(); ++Index)
	{
		if (FMcMob* Mob = FindMob(MobPassengers[Index]))
		{
			// It sits down into the hull, its legs through the floor, rather than standing on it: lowered by four tenths of its height,
			// about the length of a pig's, cow's or chicken's legs.
			Mob->Body.Position = GetSeat(Seats.Num() + Index) - FVector(0.0, Mob->Type.Height * 0.4, 0.0);
			Mob->Body.Velocity = FVector::ZeroVector;
			// Animals sit across the boat, facing its side; the rest face the way it goes.
			Mob->Yaw = Mob->Type.bPanics ? Yaw + 90.0f : Yaw;
		}
	}
}

bool FMcBoatEntity::CanCarryMob(const FMcMob& Mob) const
{
	// Anything smaller than the boat that comes up against it (and a rider who is a player takes a seat by asking).
	return Mob.Type.Width < Body.Width;
}

FVector FMcBoatEntity::GetSeat(int32 Index) const
{
	// Two aboard sit one behind the other: the first a fifth of a block forward, the second six tenths back.
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Yaw));
	const double Along = GetPassengerCount() > 1 ? (Index == 0 ? 0.2 : -0.6) : 0.0;
	return Body.Position + FVector(0.0, 0.1, 0.0) + FVector(-FMath::Sin(Radians), 0.0, FMath::Cos(Radians)) * Along;
}

FMcBoatEntity::ESurface FMcBoatEntity::Sense(FMcBlockWorld& World, double& OutSurface, double& OutFriction) const
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	OutSurface = 0.0;
	OutFriction = 0.9;
	const int32 X = FMath::FloorToInt32(Body.Position.X);
	const int32 Z = FMath::FloorToInt32(Body.Position.Z);
	const int32 Base = FMath::FloorToInt32(Body.Position.Y);
	// The highest cell near the boat with water in it, then up through the water above it.
	int32 Top = INT_MIN;
	for (int32 Y = Base + 1; Y >= Base - 1; --Y)
	{
		if (HasWater(World.GetBlock(FIntVector(X, Y, Z))))
		{
			Top = Y;
			break;
		}
	}
	if (Top != INT_MIN)
	{
		while (HasWater(World.GetBlock(FIntVector(X, Top + 1, Z))))
		{
			++Top;
		}
		const double Surface = Top + WaterHeight(World.GetBlock(FIntVector(X, Top, Z)));
		OutSurface = Surface;
		// Over water but not yet down in it, it is still falling: vehicle_log's dropped boat falls on past -59.64, the
		// water's surface at -60.11.
		if (Body.Position.Y >= Surface)
		{
			if (!Body.bOnGround)
			{
				return ESurface::Air;
			}
			OutFriction = Friction(Registry.GetBlockOf(World.GetBlock(FIntVector(X, FMath::FloorToInt32(Body.Position.Y - 0.05), Z))).Name);
			return ESurface::Ground;
		}
		// Water over its hull lifts it, unless it is flowing water (a waterfall), which holds it down; the top of the hull
		// just in it floats.
		if (Body.Position.Y + Body.Height >= Surface)
		{
			return ESurface::Water;
		}
		const FMcStateId Over = World.GetBlock(FIntVector(X, FMath::FloorToInt32(Body.Position.Y + Body.Height), Z));
		const FName Level = Registry.GetPropertyValue(Over, TEXT("level"));
		return HasWater(Over) && !Level.IsNone() && Level != FName(TEXT("0")) ? ESurface::UnderFlowing : ESurface::Underwater;
	}
	const FIntVector Below(X, FMath::FloorToInt32(Body.Position.Y - 0.05), Z);
	if (Body.bOnGround)
	{
		OutFriction = Friction(Registry.GetBlockOf(World.GetBlock(Below)).Name);
		return ESurface::Ground;
	}
	return ESurface::Air;
}

void FMcBoatEntity::Tick(FMcEntityManager& Manager)
{
	TickRider();
	++Age;
	FMcBlockWorld& World = Manager.GetWorld();
	if (!World.HasChunk(FIntPoint(FMath::FloorToInt32(Body.Position.X) >> 4, FMath::FloorToInt32(Body.Position.Z) >> 4)))
	{
		return;
	}
	double Surface = 0.0, Slip = 0.9;
	const ESurface Where = Sense(World, Surface, Slip);

	// Steering: left and right turn it, forward and back push along where it faces.
	const bool bRiding = HasRider();
	const float Turn = bRiding ? -RiderStrafe : 0.0f;
	// A degree more each tick a key is held, and nine tenths of it kept (paddle_log.txt: the yaw goes -1, -1.9, -2.71,
	// -3.439 a tick turning left, and 7.46, 6.72, 6.04 coasting after).
	TurnRate = TurnRate * 0.9f + Turn;
	Yaw += TurnRate;
	double Push = 0.0;
	if (bRiding)
	{
		Push = RiderForward > 0.0f ? Thrust : RiderForward < 0.0f ? -ReverseThrust : 0.0;
		// Turning alone still nudges it forward.
		if (RiderForward == 0.0f && RiderStrafe != 0.0f)
		{
			Push = 0.005;
		}
	}
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Yaw));
	Body.Velocity.X += -FMath::Sin(Radians) * Push;
	Body.Velocity.Z += FMath::Cos(Radians) * Push;
	Body.Velocity.X *= Slip;
	Body.Velocity.Z *= Slip;

	// Up and down: it floats at the surface, rises through water over it and falls in the air.
	switch (Where)
	{
	case ESurface::Water:
		// Weight against how deep it sits, the depth in from the surface counted in 0.65s of its height, damped by a
		// quarter each tick. Fitted from vehicle_log.txt's boat on water (to 1e-6 over 120 ticks), which settles at
		// 0.65 of its height under the surface.
		Body.Velocity.Y = 0.75 * (Body.Velocity.Y - Gravity + Gravity * (Surface - Body.Position.Y) / (0.65 * Body.Height));
		break;
	case ESurface::Underwater:
		Body.Velocity.Y = FMath::Min(Body.Velocity.Y + 0.04, 0.15);
		break;
	case ESurface::UnderFlowing:
		Body.Velocity.Y = FMath::Max(Body.Velocity.Y * 0.9 - 0.002, -0.05);
		break;
	default:
		// No drag in the air: vehicle_log's dropped boat falls 0.04, 0.08, 0.12, ...
		Body.Velocity.Y -= Gravity;
		break;
	}
	McPhysics::Move(World, Body, Body.Velocity, false);
	if (bWasInAir && Where == ESurface::Water)
	{
		// Coming down onto water it sinks in once and stops: vehicle_log's dropped boat, at -0.48 a tick, goes from -60.12
		// to -60.46 and has no speed after (this moves it to -60.51).
		Body.Velocity.Y = 0.0;
	}
	bWasInAir = Where == ESurface::Air;
	TickPassengers(Manager);
}
