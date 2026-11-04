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

bool McMount::IsRideable(const FMcEntity& Entity)
{
	return Entity.IsLiving() && IsHorseKind(Entity.GetType());
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
		if (Held.Item == TEXT("minecraft:saddle") && Mob.bTamed && !State.bSaddled)
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
	if (bSneaking || State.RiderId != 0)
	{
		return EUse::None;
	}
	State.RiderId = PlayerId;
	State.JumpHoldTicks = 0;
	State.PendingJump = 0.0f;
	Mob.Goal.Reset();
	return EUse::Mounted;
}

void McMount::SetRiderInput(FMcEntity& Entity, float Forward, float Strafe, float Yaw, bool bJumpHeld)
{
	if (Entity.Mount.IsValid())
	{
		FMcMountState& State = *Entity.Mount;
		State.Forward = Forward;
		State.Strafe = Strafe;
		State.RiderYaw = Yaw;
		State.bJumpHeld = bJumpHeld;
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
	if (!Mob.bTamed)
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
	if (Mob.bTamed && State.bSaddled)
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
