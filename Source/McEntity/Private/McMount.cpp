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
	return Entity.IsLiving() && (IsHorseKind(Entity.GetType()) || IsStickMount(Entity));
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
		if (Held.Item == TEXT("minecraft:saddle") && (Mob.bTamed || IsStickMount(Mob)) && !State.bSaddled)
		{
			State.bSaddled = true;
			Take();
			return EUse::Saddled;
		}
		if (IsHorseArmor(Held.Item) && Mob.bTamed && Mob.GetType() == TEXT("minecraft:horse") && State.Armor.IsNone())
		{
			State.Armor = Held.Item;
			Take();
			return EUse::Armored;
		}
		if (Held.Item == TEXT("minecraft:chest") && Mob.bTamed && IsPackKind(Mob.GetType()) && !State.bChest)
		{
			State.bChest = true;
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
	if (bSneaking || State.RiderId != 0 || (IsStickMount(Mob) && !State.bSaddled))
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
	return Entity.Body.Position + FVector(0.0, Entity.Body.Height * 0.6875, 0.0);
}

bool McMount::TickRidden(FMcEntityManager& Manager, FMcMob& Mob)
{
	if (!Mob.Mount.IsValid() || Mob.Mount->RiderId == 0)
	{
		return false;
	}
	FMcMountState& State = *Mob.Mount;
	if (!Mob.bTamed && !IsStickMount(Mob))
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
			Input.Speed = State.Speed * BoostFactor(State);
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
	else if (Mob.bTamed && State.bSaddled)
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
		if (State.PendingJump > 0.0f && Mob.Body.bOnGround)
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
	return true;
}
