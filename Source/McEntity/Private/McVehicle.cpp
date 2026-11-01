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
		// Slime, from vehicle_log6.txt: a boat pushed at 0.5 moves 0.4, then 0.0512 from 0.064 (with SpeedFactor's 0.16).
		if (Name == TEXT("minecraft:slime_block"))
		{
			return 0.8;
		}
		// Stone, from vanilla_log vehicle_log: a boat pushed at 0.3 goes 0.18, 0.108, 0.0648.
		return 0.6;
	}

	// What is left of a boat's speed after it moves, from the block under it (vehicle_log6.txt, every tick): soul sand
	// keeps 0.4 (0.45 moved leaves 0.18, 0.05832 leaves 0.023328), even with the boat still dropping onto it; slime 0.16
	// (0.4 leaves 0.064, 0.0512 leaves 0.008192); honey 0.4 too, on stone's 0.6 friction (vehicle_log9.txt: 0.45 moved
	// leaves 0.18, 0.03888 leaves 0.015552). Frosted ice, mud and soul soil change nothing (vehicle_log9.txt).
	double SpeedFactor(FName Block)
	{
		const FString Name = Block.ToString();
		return Name == TEXT("minecraft:soul_sand") || Name == TEXT("minecraft:honey_block") ? 0.4 : Name == TEXT("minecraft:slime_block") ? 0.16 : 1.0;
	}
}

double FMcBoatEntity::GroundFriction(FMcBlockWorld& World) const
{
	// The mean over the blocks under the hull (vehicle_log5.txt: whole hulls on blue ice keep 0.989, on ice and packed
	// ice 0.98; one over a row of packed ice between stone keeps 0.72667 = (0.6 + 0.98 + 0.6) / 3). Cells with nothing
	// in them are left out (vehicle_log9.txt: one over ice, an empty row and ice keeps 0.98).
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const double Half = Body.Width * 0.5;
	const int32 Y = FMath::FloorToInt32(Body.Position.Y - 0.05);
	double Sum = 0.0;
	int32 Count = 0;
	for (int32 X = FMath::FloorToInt32(Body.Position.X - Half); X <= FMath::FloorToInt32(Body.Position.X + Half); ++X)
	{
		for (int32 Z = FMath::FloorToInt32(Body.Position.Z - Half); Z <= FMath::FloorToInt32(Body.Position.Z + Half); ++Z)
		{
			const FMcStateId Block = World.GetBlock(FIntVector(X, Y, Z));
			if (Registry.GetBlockOf(Block).Name != FName(TEXT("minecraft:air")))
			{
				Sum += Friction(Registry.GetBlockOf(Block).Name);
				++Count;
			}
		}
	}
	return Count > 0 ? Sum / Count : 0.6;
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

// Whether two boxes share some volume; touching faces don't count.
static bool Overlaps(const FBox& A, const FBox& B)
{
	return A.Min.X < B.Max.X && B.Min.X < A.Max.X && A.Min.Y < B.Max.Y && B.Min.Y < A.Max.Y && A.Min.Z < B.Max.Z && B.Min.Z < A.Max.Z;
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

	RailAlong = Along;
	// The speed along the track, signed by the way it is going. No more than 2 either way: a cart a driving furnace cart
	// threw at 3.5056 moves 0.4 and is left at 1.92 = 2 * 0.96 (vehicle_log7.txt).
	FVector2D Move(FMath::Clamp(Body.Velocity.X, -2.0, 2.0), FMath::Clamp(Body.Velocity.Z, -2.0, 2.0));
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
	// Fuel alone doesn't drive it: it needs a push from whoever fed it (vehicle_log2.txt: summoned with fuel and moving,
	// it only coasts). Fed from rest it goes at its full 0.2 a tick from the next tick on, with no run-up
	// (vehicle_log3.txt: 3.5, 3.7, 3.9, ... while vanilla's own speed reads 1.92, 3.39, then 3.46, which only the move cap
	// holds back; ours is kept at the 0.2 it moves).
	const bool bDriving = Variant == EVariant::Furnace && Fuel > 0 && !PushDirection.IsNearlyZero();
	if (bDriving)
	{
		const double Heading = Velocity.SizeSquared() > 1e-4 ? FVector2D::DotProduct(Velocity, Along) : FVector2D::DotProduct(PushDirection, Along);
		Velocity = Along * (Heading >= 0.0 ? FurnaceSpeed : -FurnaceSpeed);
	}
	// The tick its fuel runs out it is left going at 2, which the cap holds to 0.2 a tick until coasting wears it below
	// (vehicle_log6.txt: fuel cut to 40 ticks, its speed drops from 3.455 to 1.8816 = 2 * 0.9408 as the fuel ends, it
	// moves 0.2 for 38 ticks, then 0.19676, and keeps 0.9408 a tick).
	else if (Variant == EVariant::Furnace && !PushDirection.IsNearlyZero())
	{
		const FVector2D Heading = Velocity.SizeSquared() > 1e-8 ? Velocity.GetSafeNormal() : Along * (FVector2D::DotProduct(PushDirection, Along) >= 0.0 ? 1.0 : -1.0);
		Velocity = Heading * 2.0;
		PushDirection = FVector2D::ZeroVector;
	}

	// The speed limit is on how far it moves in a tick, not on its speed: powered_log.txt's cart on powered rail moves
	// 0.4 a tick while its speed goes on to 1.03.
	const double Fast = Velocity.Size();
	// A furnace cart's is 0.2, driving or not.
	const double Cap = Variant == EVariant::Furnace ? FurnaceSpeed : MaxSpeed;
	// With a rider it goes three quarters of that (vehicle_log2.txt: a ridden cart at 0.4 moves 0.3, at 0.3988 0.2991).
	const FVector2D Step = (Fast > Cap ? Velocity * (Cap / Fast) : Velocity) * (HasRider() ? 0.75 : 1.0);

	// Along the line to where it gets to, and if something is in the way, stop.
	const double Reach = FVector2D::DotProduct(Step, Along);
	const double NextWhere = Where + Reach / Length;
	const double NextT = FMath::Clamp(NextWhere, 0.0, 1.0);
	const FVector Target(EndA.X + Line.X * NextWhere, FMath::Lerp(EndA.Y, EndB.Y, NextT), EndA.Z + Line.Y * NextWhere);
	const FVector Before = Body.Position;
	Body.Velocity = FVector(Velocity.X, 0.0, Velocity.Y);
	// A mob it can't take in stops it dead where their boxes meet (vehicle_log8.txt: a cart at 0.158 stops at 7.31 against
	// an iron golem at 8.5, a cart with a pig in it at 7.56 against a cow, both left at speed 0).
	FVector Motion = Target - Before;
	bool bHitMob = false;
	{
		const FBox Box = Body.GetBox();
		TArray<FMcEntity*> Near;
		Manager.FindInBox(Box.ExpandBy(FVector(FMath::Abs(Motion.X), FMath::Abs(Motion.Y), FMath::Abs(Motion.Z))), Near);
		for (FMcEntity* Entity : Near)
		{
			if (!Entity->IsLiving() || Entity->bRemoved || static_cast<FMcMob*>(Entity)->RidingId != 0
				|| (HasFreeSeat() && CanCarryMob(*static_cast<FMcMob*>(Entity))))
			{
				continue;
			}
			// How far along the move the cart's box first touches the mob's: the slab test against the mob's box grown by
			// the cart's.
			const FBox Other = Entity->Body.GetBox();
			if (Overlaps(Box, Other))
			{
				continue;
			}
			const FVector Half = Box.GetExtent();
			const FVector Lo = Other.Min - Half;
			const FVector Hi = Other.Max + Half;
			const FVector From = Box.GetCenter();
			double Enter = 0.0;
			double Leave = 1.0;
			for (int32 Axis = 0; Axis < 3 && Enter <= Leave; ++Axis)
			{
				if (FMath::Abs(Motion[Axis]) < 1e-12)
				{
					if (From[Axis] <= Lo[Axis] || From[Axis] >= Hi[Axis])
					{
						Leave = -1.0;
					}
					continue;
				}
				double A = (Lo[Axis] - From[Axis]) / Motion[Axis];
				double B = (Hi[Axis] - From[Axis]) / Motion[Axis];
				if (A > B)
				{
					Swap(A, B);
				}
				Enter = FMath::Max(Enter, A);
				Leave = FMath::Min(Leave, B);
			}
			if (Enter < Leave && Enter < 1.0)
			{
				Motion *= Enter;
				bHitMob = true;
			}
		}
	}
	McPhysics::Move(World, Body, Motion, false);
	if (Body.bHorizontalCollision || bHitMob)
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
		// A ridden cart keeps 0.997 (vehicle_log2.txt, 50 ticks), a furnace cart not driving 0.96 * 0.98 (0.9408 there,
		// fuelled but never pushed).
		const double Friction = HasRider() ? 0.997 : Variant == EVariant::Furnace && !bDriving ? 0.96 * 0.98 : GetFriction();
		const double Kept = Friction * Fast + 0.05 * (Before.Y - Body.Position.Y);
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
		// A furnace cart's own drive is held to its speed.
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
	return Variant == EVariant::Plain && FVector2D(Body.Velocity.X, Body.Velocity.Z).SizeSquared() > 0.1 * 0.1
		&& Mob.Type.Name != TEXT("minecraft:iron_golem");
}

double FMcMinecartEntity::GetFriction()
{
	if (Variant != EVariant::Chest && Variant != EVariant::Hopper)
	{
		return 0.96;
	}
	// A thousandth less for each step of the signal a comparator would read off it (vehicle_log7.txt: chest carts from 0.4
	// keep 0.995 empty, 0.994 with one stack of 64 stone, 0.98 full; vehicle_log8.txt: an empty hopper cart 0.995).
	double Fullness = 0.0;
	bool bAny = false;
	const TSharedPtr<FMcContainer>& Items = GetInventory();
	for (const FMcItemStack& Stack : Items->GetAll())
	{
		if (!Stack.IsEmpty())
		{
			Fullness += static_cast<double>(Stack.Count) / Stack.GetMaxStackSize();
			bAny = true;
		}
	}
	const int32 Signal = bAny ? FMath::FloorToInt32(1.0 + Fullness / Items->Num() * 14.0) : 0;
	return 0.98 + (15 - Signal) * 0.001;
}

FVector2D FMcMinecartEntity::GetPushVelocity() const
{
	const FVector2D Velocity(Body.Velocity.X, Body.Velocity.Z);
	return IsDriving() ? Velocity.GetSafeNormal() * FurnaceDriveSpeed : Velocity;
}

void FMcMinecartEntity::SetPushVelocity(const FVector2D& Velocity)
{
	// A driving furnace cart sets its own speed again next tick.
	if (!IsDriving())
	{
		Body.Velocity = FVector(Velocity.X, Body.Velocity.Y, Velocity.Y);
	}
}

void FMcMinecartEntity::PushedBy(FMcMinecartEntity& Pusher)
{
	FVector2D Away(Body.Position.X - Pusher.Body.Position.X, Body.Position.Z - Pusher.Body.Position.Z);
	const double Distance = Away.Size();
	if (Distance < 0.01)
	{
		return;
	}
	Away /= Distance;
	// Only along its own rail (or the pusher's, if this one hasn't been on one yet): one passing on the next rail over
	// leaves it be (vehicle_log8.txt).
	const FVector2D& Rail = RailAlong.IsZero() ? Pusher.RailAlong : RailAlong;
	if (!Rail.IsZero() && FMath::Abs(FVector2D::DotProduct(Away, Rail)) < 0.8)
	{
		return;
	}
	// Each keeps a fifth of its own speed and takes half of the two together, and they are shoved 0.05 apart, less past a
	// block (vehicle_log7.txt: a cart at 0.199835 into one at rest leaves 0.089884 and 0.149917; head on at 0.081 and
	// -0.085, 1.147 apart, they leave -0.029 and 0.025). A furnace cart, fuelled or not, isn't slowed so: the other keeps
	// its fifth and takes all of the furnace cart's speed, and the furnace cart keeps 0.95 (vehicle_log7.txt: a driving one
	// throws the cart ahead at 3.455616 + 0.05 and reads 3.282835; vehicle_log8.txt: a cart at 0.2 into an unfuelled one at
	// rest bounces back at -0.052, the furnace cart unmoved).
	const FVector2D Shove = Away * FMath::Min(1.0 / Distance, 1.0) * 0.05;
	FVector2D Mine = GetPushVelocity();
	FVector2D Theirs = Pusher.GetPushVelocity();
	const bool bMineFurnace = Variant == EVariant::Furnace;
	const bool bTheirsFurnace = Pusher.Variant == EVariant::Furnace;
	if (bTheirsFurnace && !bMineFurnace)
	{
		Mine = Mine * 0.2 + Theirs + Shove;
		Theirs *= 0.95;
	}
	else if (bMineFurnace && !bTheirsFurnace)
	{
		Theirs = Theirs * 0.2 + Mine - Shove;
		Mine *= 0.95;
	}
	else
	{
		const FVector2D Half = (Mine + Theirs) * 0.5;
		Mine = Mine * 0.2 + Half + Shove;
		Theirs = Theirs * 0.2 + Half - Shove;
	}
	SetPushVelocity(Mine);
	Pusher.SetPushVelocity(Theirs);
}

void FMcMinecartEntity::PushMobs(FMcEntityManager& Manager)
{
	// After it has moved, everything within 0.2 of it. Fitted from vehicle_log7.txt and vehicle_log8.txt:
	// - it shoves every cart it reaches, moving or not (PushedBy);
	// - going faster than 0.1, it and a mob it can't take in are shoved 0.05 apart, it not at all with someone in it (an
	//   iron golem it reaches at 0.208 gets 0.05 and it keeps 0.158; a cow a cart with a pig in it reaches gets 0.05);
	// - a mob standing in it (their boxes overlapping) shoves it off at 0.05 and itself a quarter of that, every tick (a cow
	//   0.7 into a cart at rest: the cart goes -0.05, -0.098, -0.144, the cow 0.0125, 0.025, 0.0375).
	TArray<FMcEntity*> Near;
	const FBox Box = Body.GetBox();
	Manager.FindInBox(Box.ExpandBy(FVector(0.2, 0.0, 0.2)), Near);
	const bool bFast = FVector2D(Body.Velocity.X, Body.Velocity.Z).SizeSquared() > 0.01;
	for (FMcEntity* Entity : Near)
	{
		if (Entity == this || Entity->bRemoved)
		{
			continue;
		}
		if (ParseType(Entity->GetType()).IsSet())
		{
			static_cast<FMcMinecartEntity*>(Entity)->PushedBy(*this);
			continue;
		}
		if (!Entity->IsLiving() || static_cast<FMcMob*>(Entity)->RidingId != 0)
		{
			continue;
		}
		const FVector2D Between(Entity->Body.Position.X - Body.Position.X, Entity->Body.Position.Z - Body.Position.Z);
		if (bFast)
		{
			// The way any two things are shoved: by the bigger of the two gaps, 0.05 at a block or more.
			const double Gap = FMath::Max(FMath::Abs(Between.X), FMath::Abs(Between.Y));
			if (Gap >= 0.01)
			{
				const double Root = FMath::Sqrt(Gap);
				const FVector2D Shove = Between / Root * FMath::Min(1.0 / Root, 1.0) * 0.05;
				Entity->Body.Velocity += FVector(Shove.X, 0.0, Shove.Y);
				if (GetPassengerCount() == 0)
				{
					Body.Velocity -= FVector(Shove.X, 0.0, Shove.Y);
				}
			}
		}
		const double Distance = Between.Size();
		if (Distance >= 0.01 && Overlaps(Box, Entity->Body.GetBox()))
		{
			const FVector2D Shove = Between / Distance * FMath::Min(1.0 / Distance, 1.0) * 0.05;
			Body.Velocity -= FVector(Shove.X, 0.0, Shove.Y);
			Entity->Body.Velocity += FVector(Shove.X, 0.0, Shove.Y) * 0.25;
		}
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
		if (!bHopperLocked)
		{
			// Every tick it takes in the items lying in it or over it, up to a little under two blocks above it
			// (vehicle_log8.txt: items floating 0.8, 1.2 and 1.6 up are taken the tick after they appear, 2.0 up and more not;
			// 0.6 to the side taken, 0.8 not; vehicle_log7.txt: a stack 1.94 up taken; five items five ticks apart each taken
			// the tick after it appears).
			TArray<FMcEntity*> Near;
			Manager.FindInBox(FBox(Body.Position + FVector(-0.49, 0.0, -0.49), Body.Position + FVector(0.49, 1.96875, 0.49)), Near);
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
			OutFriction = GroundFriction(World);
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
	if (Body.bOnGround)
	{
		OutFriction = GroundFriction(World);
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
		if (bWasInAir)
		{
			// Coming down onto water it is set 0.3504 under the surface, still: vehicle_log.txt and vehicle_log2.txt's four
			// dropped boats, falling in at 0.28 to 0.88 a tick, all sit at -60.4615 the tick after, the surface at -60.1111.
			Body.Position.Y = Surface - 0.3504;
			Body.Velocity.Y = 0.0;
			break;
		}
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
	const FIntVector Under(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y - 0.5), FMath::FloorToInt32(Body.Position.Z));
	const double Factor = SpeedFactor(FMcBlockRegistry::Get().GetBlockOf(World.GetBlock(Under)).Name);
	Body.Velocity.X *= Factor;
	Body.Velocity.Z *= Factor;
	bWasInAir = Where == ESurface::Air;
	TickPassengers(Manager);
}
