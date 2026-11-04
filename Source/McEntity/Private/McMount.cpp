#include "McMount.h"
#include "McMob.h"
#include "McPhysics.h"

namespace
{
	bool IsHorseKind(const FName& Name)
	{
		return Name == TEXT("minecraft:horse") || Name == TEXT("minecraft:donkey") || Name == TEXT("minecraft:mule")
			|| Name == TEXT("minecraft:skeleton_horse") || Name == TEXT("minecraft:zombie_horse");
	}

	bool IsPackKind(const FName& Name)
	{
		return Name == TEXT("minecraft:donkey") || Name == TEXT("minecraft:mule");
	}

	int32 TemperOf(const FName& Food)
	{
		if (Food == TEXT("minecraft:sugar") || Food == TEXT("minecraft:wheat") || Food == TEXT("minecraft:apple"))
		{
			return 3;
		}
		if (Food == TEXT("minecraft:golden_carrot"))
		{
			return 5;
		}
		if (Food == TEXT("minecraft:golden_apple") || Food == TEXT("minecraft:enchanted_golden_apple"))
		{
			return 10;
		}
		return 0;
	}

	bool IsHorseArmor(const FName& Item)
	{
		return Item.ToString().EndsWith(TEXT("_horse_armor"));
	}
}

bool McMount::IsStickMount(const FMcEntity& Entity)
{
	return Entity.IsLiving() && (Entity.GetType() == TEXT("minecraft:pig") || Entity.GetType() == TEXT("minecraft:strider"));
}

bool McMount::IsRideable(const FMcEntity& Entity)
{
	return Entity.IsLiving() && (IsHorseKind(Entity.GetType()) || IsStickMount(Entity) || Entity.GetType() == TEXT("minecraft:camel"));
}

bool McMount::Boost(FMcEntity& Entity, int32 TotalTicks)
{
	if (!IsStickMount(Entity) || !Entity.Mount.IsValid() || Entity.Mount->RiderId == 0 || Entity.Mount->BoostTotal > 0)
	{
		return false;
	}
	Entity.Mount->BoostTime = 0;
	Entity.Mount->BoostTotal = TotalTicks > 0 ? TotalTicks : FMath::RandRange(140, 980);
	return true;
}

float McMount::BoostFactor(const FMcMountState& State)
{
	if (State.BoostTotal <= 0)
	{
		return 1.0f;
	}
	return 1.0f + 1.15f * FMath::Sin(UE_PI * static_cast<float>(State.BoostTime) / static_cast<float>(State.BoostTotal));
}

namespace FMcMountMenuSlots
{
	constexpr int32 Saddle = 0;
	constexpr int32 Armor = 1;
	constexpr int32 ChestStart = 2;
	constexpr int32 ChestSlots = 15;
}

const TSharedPtr<FMcContainer>& McMount::GetInventory(FMcMountState& State)
{
	const int32 Want = FMcMountMenuSlots::ChestStart + (State.bChest ? FMcMountMenuSlots::ChestSlots : 0);
	if (!State.Inventory.IsValid())
	{
		State.Inventory = MakeShared<FMcContainer>(Want);
		if (State.bSaddled)
		{
			State.Inventory->Set(FMcMountMenuSlots::Saddle, FMcItemStack(FName(TEXT("minecraft:saddle")), 1));
		}
		if (!State.Armor.IsNone())
		{
			State.Inventory->Set(FMcMountMenuSlots::Armor, FMcItemStack(State.Armor, 1));
		}
	}
	else if (State.Inventory->Num() < Want)
	{
		State.Inventory->GetAll().SetNum(Want);
	}
	return State.Inventory;
}

void McMount::Sync(FMcMountState& State)
{
	if (!State.Inventory.IsValid())
	{
		return;
	}
	State.bSaddled = State.Inventory->Get(FMcMountMenuSlots::Saddle).Item == TEXT("minecraft:saddle");
	State.Armor = State.Inventory->Get(FMcMountMenuSlots::Armor).IsEmpty() ? FName() : State.Inventory->Get(FMcMountMenuSlots::Armor).Item;
	State.bChest = State.Inventory->Num() > FMcMountMenuSlots::ChestStart;
}

FMcMountState& McMount::Ensure(FMcEntity& Entity)
{
	if (!Entity.Mount.IsValid())
	{
		Entity.Mount = MakeShared<FMcMountState>();
		FMcMountState& State = *Entity.Mount;
		const FName Kind = Entity.GetType();
		const FMcMobType* Type = FMcMobType::Find(Kind);
		State.Speed = Type ? Type->Speed : 0.225f;
		if (IsPackKind(Kind))
		{
			State.JumpStrength = 0.5f;
		}
		else if (Kind == TEXT("minecraft:pig"))
		{
			State.Speed = (Type ? Type->Speed : 0.25f) * 0.2296f;
		}
		else if (Kind == TEXT("minecraft:strider"))
		{
			State.Speed = (Type ? Type->Speed : 0.175f) * 0.2357f;
		}
		else if (Kind == TEXT("minecraft:horse"))
		{
			// A foal rolls its stats from three dice each: jump 0.4 to 1.0, speed 0.1125 to 0.3375.
			State.JumpStrength = 0.4f + FMath::FRand() * 0.2f + FMath::FRand() * 0.2f + FMath::FRand() * 0.2f;
			State.Speed = (0.45f + FMath::FRand() * 0.3f + FMath::FRand() * 0.3f + FMath::FRand() * 0.3f) * 0.25f;
		}
	}
	return *Entity.Mount;
}

float McMount::ChargeScale(int32 HeldTicks)
{
	const int32 T = FMath::Max(0, HeldTicks - 1);
	return T < 10 ? 0.1f * T : 0.8f + 0.2f / static_cast<float>(T - 9);
}

float McMount::JumpFraction(int32 HeldTicks)
{
	const float Scale = ChargeScale(HeldTicks);
	return Scale >= 0.9f - 1e-4f ? 1.0f : 0.4f + 0.4f * Scale / 0.9f;
}

McMount::EUse McMount::Use(FMcEntityManager& Manager, FMcMob& Mob, FMcItemStack& Held, uint32 PlayerId, bool bCreative, bool bSneaking)
{
	if (!IsRideable(Mob) || Mob.IsYoung())
	{
		return EUse::None;
	}
	FMcMountState& State = Ensure(Mob);
	const auto Take = [&]()
	{
		if (!bCreative && --Held.Count <= 0)
		{
			Held.Clear();
		}
	};
	if (!Held.IsEmpty())
	{
		if (Held.Item == TEXT("minecraft:saddle") && (Mob.bTamed || IsStickMount(Mob) || Mob.GetType() == TEXT("minecraft:camel")) && !State.bSaddled)
		{
			GetInventory(State)->Set(FMcMountMenuSlots::Saddle, FMcItemStack(FName(TEXT("minecraft:saddle")), 1));
			Sync(State);
			Take();
			return EUse::Saddled;
		}
		if (IsHorseArmor(Held.Item) && Mob.bTamed && Mob.GetType() == TEXT("minecraft:horse") && State.Armor.IsNone())
		{
			GetInventory(State)->Set(FMcMountMenuSlots::Armor, FMcItemStack(Held.Item, 1));
			Sync(State);
			Take();
			return EUse::Armored;
		}
		if (Held.Item == TEXT("minecraft:chest") && Mob.bTamed && IsPackKind(Mob.GetType()) && !State.bChest)
		{
			State.bChest = true;
			GetInventory(State);
			Take();
			return EUse::Chested;
		}
		if (const int32 Temper = TemperOf(Held.Item))
		{
			State.Temper = FMath::Min(MaxTemper, State.Temper + Temper);
			Take();
			return EUse::Fed;
		}
	}
	if (State.RiderId != 0 && State.PassengerId == 0 && Mob.GetType() == TEXT("minecraft:camel") && !bSneaking && State.RiderId != PlayerId)
	{
		State.PassengerId = PlayerId;
		return EUse::Mounted;
	}
	if (bSneaking && Mob.bTamed)
	{
		return EUse::Inventory;
	}
	if (bSneaking || State.RiderId != 0 || ((IsStickMount(Mob) || Mob.GetType() == TEXT("minecraft:camel")) && !State.bSaddled))
	{
		return EUse::None;
	}
	State.RiderId = PlayerId;
	State.JumpHoldTicks = 0;
	State.PendingJump = 0.0f;
	Mob.Goal.Reset();
	return EUse::Mounted;
}

void McMount::SetRiderInput(FMcEntity& Entity, float Forward, float Strafe, float Yaw, bool bJumpHeld, FName Held)
{
	if (Entity.Mount.IsValid())
	{
		FMcMountState& State = *Entity.Mount;
		State.Forward = Forward;
		State.Strafe = Strafe;
		State.RiderYaw = Yaw;
		State.bJumpHeld = bJumpHeld;
		State.RiderHeld = Held;
	}
}

bool McMount::ClearRider(FMcEntity& Entity)
{
	if (!Entity.Mount.IsValid() || Entity.Mount->RiderId == 0)
	{
		return false;
	}
	Entity.Mount->RiderId = 0;
	Entity.Mount->JumpHoldTicks = 0;
	Entity.Mount->PendingJump = 0.0f;
	return true;
}

FVector McMount::GetSeat(const FMcEntity& Entity)
{
	// The rider's feet above the mount's: horse from vehicle_log30 (player y -60.15625 on a floor at -61), the rest from vehicle_log31.
	struct FSeat { const TCHAR* Type; double Height; };
	static const FSeat Seats[] = {
		{TEXT("minecraft:horse"), 0.84375}, {TEXT("minecraft:donkey"), 0.5125}, {TEXT("minecraft:mule"), 0.6125},
		{TEXT("minecraft:pig"), 0.26875}, {TEXT("minecraft:strider"), 1.1}, {TEXT("minecraft:camel"), 1.4},
	};
	for (const FSeat& Seat : Seats)
	{
		if (Entity.GetType() == Seat.Type)
		{
			return Entity.Body.Position + FVector(0.0, Seat.Height, 0.0);
		}
	}
	return Entity.Body.Position + FVector(0.0, Entity.Body.Height * 0.6875, 0.0);
}

bool McMount::TickRidden(FMcEntityManager& Manager, FMcMob& Mob)
{
	if (!Mob.Mount.IsValid() || Mob.Mount->RiderId == 0)
	{
		return false;
	}
	FMcMountState& State = *Mob.Mount;
	const bool bCamel = Mob.GetType() == TEXT("minecraft:camel");
	if (!Mob.bTamed && !IsStickMount(Mob) && !bCamel)
	{
		// A wild one bucks about once in fifty ticks: a roll under its temper tames it, otherwise it grows 5 used to people and throws the rider.
		if (FMath::RandRange(0, BuckOneIn - 1) == 0)
		{
			if (FMath::RandRange(0, MaxTemper - 1) < State.Temper)
			{
				Mob.bTamed = true;
			}
			else
			{
				State.Temper = FMath::Min(MaxTemper, State.Temper + 5);
				ClearRider(Mob);
				return false;
			}
		}
	}
	FMcMoveInput Input;
	Input.Speed = State.Speed;
	if (IsStickMount(Mob))
	{
		// Goes straight where the rider looks while the stick is in their hand, faster for a boost.
		const bool bStick = State.RiderHeld == (Mob.GetType() == TEXT("minecraft:pig") ? FName(TEXT("minecraft:carrot_on_a_stick")) : FName(TEXT("minecraft:warped_fungus_on_a_stick")));
		if (bStick && State.bSaddled)
		{
			Input.Forward = 1.0f;
			// A strider on lava walks 2.4 times as fast as on land (vehicle_log29: 0.212 a tick against 0.089).
			const float Base = Mob.GetType() == TEXT("minecraft:strider") && Mob.Body.bInLava ? Mob.Type.Speed * 0.5614f : State.Speed;
			Input.Speed = Base * BoostFactor(State);
			Input.Yaw = State.RiderYaw;
			Mob.Yaw = State.RiderYaw;
		}
		else
		{
			Input.Speed = 0.0f;
		}
		if (State.BoostTotal > 0 && ++State.BoostTime >= State.BoostTotal)
		{
			State.BoostTime = 0;
			State.BoostTotal = 0;
		}
	}
	else if ((Mob.bTamed || bCamel) && State.bSaddled)
	{
		Input.Forward = State.Forward >= 0.0f ? State.Forward : State.Forward * 0.25f;
		Input.Strafe = State.Strafe * 0.5f;
		Input.Yaw = State.RiderYaw;
		Mob.Yaw = State.RiderYaw;
		if (State.bJumpHeld)
		{
			++State.JumpHoldTicks;
		}
		else if (State.JumpHoldTicks > 0)
		{
			State.PendingJump = JumpFraction(State.JumpHoldTicks);
			State.JumpHoldTicks = 0;
		}
		if (State.DashCooldown > 0)
		{
			--State.DashCooldown;
		}
		if (bCamel)
		{
			// A camel's charge is a dash: forward along the rider's yaw and a little up, then a pause of 55 ticks.
			if (State.PendingJump > 0.0f && Mob.Body.bOnGround && State.DashCooldown == 0)
			{
				const double Amount = State.PendingJump;
				const double Radians = FMath::DegreesToRadians(static_cast<double>(Input.Yaw));
				const double Strength = 22.2222 * Amount * State.Speed;
				Mob.Body.Velocity.X -= FMath::Sin(Radians) * Strength;
				Mob.Body.Velocity.Z += FMath::Cos(Radians) * Strength;
				Mob.Body.Velocity.Y = 0.162 + 0.6 * (Amount - 0.4);
				Mob.Body.bOnGround = false;
				State.DashCooldown = 55;
				State.PendingJump = 0.0f;
			}
			else if (State.PendingJump > 0.0f && State.DashCooldown > 0)
			{
				State.PendingJump = 0.0f;
			}
		}
		else if (State.PendingJump > 0.0f && Mob.Body.bOnGround)
		{
			Mob.Body.Velocity.Y = State.PendingJump * State.JumpStrength;
			Mob.Body.bOnGround = false;
			if (Input.Forward > 0.0f)
			{
				// A running jump carries on: 0.4 of the charge along the way it faces.
				const double Radians = FMath::DegreesToRadians(static_cast<double>(Input.Yaw));
				Mob.Body.Velocity.X -= FMath::Sin(Radians) * 0.4 * State.PendingJump;
				Mob.Body.Velocity.Z += FMath::Cos(Radians) * 0.4 * State.PendingJump;
			}
			State.PendingJump = 0.0f;
		}
	}
	else
	{
		Input.Speed = 0.0f;
	}
	Input.bJump = (Mob.Body.bInWater || Mob.Body.bInLava);
	McPhysics::TickPlayer(Manager.GetWorld(), Mob.Body, Input);
	if (Mob.GetType() == TEXT("minecraft:strider") && Mob.Body.bInLava)
	{
		// It walks on the lava: half a block into the source it stands on, not sinking.
		Mob.Body.Position.Y = FMath::FloorToDouble(Mob.Body.Position.Y) + 0.5;
		Mob.Body.Velocity.Y = 0.0;
		Mob.Body.bOnGround = true;
	}
	return true;
}

void McMount::Serialize(FArchive& Ar, FMcMob& Mob)
{
	if (Ar.IsLoading() && Ar.AtEnd())
	{
		return;
	}
	FMcMountState& State = Ensure(Mob);
	if (Ar.IsSaving() && !State.Inventory.IsValid())
	{
		State.Inventory = MakeShared<FMcContainer>(2);
	}
	Ar << Mob.bTamed << State.Temper << State.JumpStrength << State.Speed;
	int32 Count = State.Inventory.IsValid() ? State.Inventory->Num() : 2;
	Ar << Count;
	if (Ar.IsLoading())
	{
		Count = FMath::Clamp(Count, 2, FMcMountMenuSlots::ChestStart + FMcMountMenuSlots::ChestSlots);
		State.Inventory = MakeShared<FMcContainer>(Count);
		State.Temper = FMath::Clamp(State.Temper, 0, MaxTemper);
	}
	for (int32 Slot = 0; Slot < Count; ++Slot)
	{
		State.Inventory->GetMutable(Slot).Serialize(Ar);
	}
	if (Ar.IsLoading())
	{
		Sync(State);
	}
}

int32 McMount::ArmorPoints(FName Item)
{
	const FString Name = Item.ToString();
	if (Name.Contains(TEXT("leather"))) { return 3; }
	if (Name.Contains(TEXT("copper"))) { return 4; }
	if (Name.Contains(TEXT("iron"))) { return 5; }
	if (Name.Contains(TEXT("golden"))) { return 7; }
	if (Name.Contains(TEXT("diamond"))) { return 11; }
	if (Name.Contains(TEXT("netherite"))) { return 19; }
	return 0;
}

float McMount::ReduceDamage(const FMcMountState& State, float Damage)
{
	const float Armor = static_cast<float>(ArmorPoints(State.Armor));
	// Diamond armour carries toughness 2 and netherite 3 (vehicle_log35: a 10 hit left a horse 22.8 and 26.15, a 4 hit 27.5 and 28.81); the rest none.
	const FString Name = State.Armor.ToString();
	const float Toughness = Name.Contains(TEXT("netherite")) ? 3.0f : Name.Contains(TEXT("diamond")) ? 2.0f : 0.0f;
	const float Cut = FMath::Clamp(FMath::Max(Armor / 5.0f, Armor - Damage / (2.0f + Toughness / 4.0f)), 0.0f, 20.0f);
	return Damage * (1.0f - Cut / 25.0f);
}

bool McMount::ClearPlayer(FMcEntity& Entity, uint32 PlayerId)
{
	if (!Entity.Mount.IsValid())
	{
		return false;
	}
	FMcMountState& State = *Entity.Mount;
	if (State.PassengerId == PlayerId)
	{
		State.PassengerId = 0;
		return true;
	}
	if (State.RiderId == PlayerId)
	{
		ClearRider(Entity);
		State.RiderId = State.PassengerId;
		State.PassengerId = 0;
		return true;
	}
	return false;
}

bool McMount::HasPlayer(const FMcEntity& Entity, uint32 PlayerId)
{
	return Entity.Mount.IsValid() && PlayerId != 0 && (Entity.Mount->RiderId == PlayerId || Entity.Mount->PassengerId == PlayerId);
}

FVector McMount::GetSeatFor(const FMcEntity& Entity, uint32 PlayerId)
{
	FVector Seat = GetSeat(Entity);
	if (Entity.Mount.IsValid() && Entity.Mount->PassengerId == PlayerId)
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(Entity.Yaw));
		Seat.X += FMath::Sin(Radians) * 0.9;
		Seat.Z -= FMath::Cos(Radians) * 0.9;
	}
	return Seat;
}
