// Rideable mobs (minecraft.wiki: "Horse", "Donkey", "Mule"; numbers from the reference client, vehicle_log24.txt and vehicle_log25.txt).
//
// A saddled, tamed horse, donkey or mule moves with the player's own physics: the movement speed attribute is the walking input
// (first step 0.98 x speed, terminal about 2.16 x speed, friction 0.546, all as the player's), along the rider's yaw. Backwards is a
// quarter of that and sideways a half. The jump is charged by holding the key: a hold of H ticks charges H-1, a scale of 0.1 a tick
// until 10 (then 0.8 + 0.2/(t-9)), and the horse jumps on release at 0.4 + 0.4 x scale/0.9 of its jump strength, a full jump from
// 0.9 on (log25: holds 1..30 ticks gave 0.400, 0.444, 0.489, 0.533, 0.578, 0.622, 0.711 (8), 1.0 (10), 0.773 (15), 0.760 (30) of
// the 0.7 strength). The jump's rise is the usual v' = (v - 0.08) x 0.98.
//
// The mount's state hangs off the entity (FMcEntity::Mount) so the mob classes stay as they are; FMcMob::Tick hands a ridden mount to
// McMount::TickRidden. Not done: the chest's window, armour's protection, saddled pigs and striders, llamas and camels (below, as they land).

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class FMcMob;

struct MCENTITY_API FMcMountState
{
	// The jump strength and movement speed attributes (horses roll them at birth; donkeys and mules have fixed ones).
	float JumpStrength = 0.7f;
	float Speed = 0.225f;
	bool bSaddled = false;
	// Horse armour in the body slot ("minecraft:iron_horse_armor"), None for none.
	FName Armor;
	// Donkeys and mules: carrying a chest (the 15 slots are the inventory's to open).
	bool bChest = false;
	// How used to people it is, 0..100; a rider tames it when a roll under the max beats it.
	int32 Temper = 0;
	// The player riding it (the pawn's unique id), 0 for none.
	uint32 RiderId = 0;
	// The rider's keys this tick.
	float Forward = 0.0f;
	float Strafe = 0.0f;
	float RiderYaw = 0.0f;
	bool bJumpHeld = false;
	// Ticks the jump key has been held, and the jump waiting for the ground (0 for none).
	int32 JumpHoldTicks = 0;
	float PendingJump = 0.0f;
	// What the rider holds (a pig or strider goes only for its stick), and the boost a stick's use gave: ticks gone and the total.
	FName RiderHeld;
	int32 BoostTime = 0;
	int32 BoostTotal = 0;
};

namespace McMount
{
	static constexpr int32 MaxTemper = 100;
	// A horse that is not tamed tries to buck its rider about once in 50 ticks.
	static constexpr int32 BuckOneIn = 50;

	// Pigs and striders: saddled, steered with a stick in the rider's hand (carrot, or warped fungus for the strider). The ridden speed is
	// 0.2296 x the attribute for a pig and 0.2357 for a strider (vehicle_log26.txt: 0.1239 and 0.089 a tick on the player's physics), and the
	// stick's boost (log26) lasts 140 to 980 ticks and scales the speed by 1 + 1.15 x sin(pi x ticks/total).
	MCENTITY_API bool IsStickMount(const FMcEntity& Entity);
	// The stick's use while riding: starts a boost if there is none running. True if it did.
	MCENTITY_API bool Boost(FMcEntity& Entity, int32 TotalTicks = 0);
	MCENTITY_API float BoostFactor(const FMcMountState& State);
	// Whether a player can ride it (the horse family, and the stick mounts).
	MCENTITY_API bool IsRideable(const FMcEntity& Entity);
	// The mount state, made with the kind's stats if there is none yet.
	MCENTITY_API FMcMountState& Ensure(FMcEntity& Entity);
	// The scale a charge of this many held ticks reaches, 0..1 (0.1 a tick, then slowing to a full charge at 10).
	MCENTITY_API float ChargeScale(int32 HeldTicks);
	// The part of the jump strength a charge of HeldTicks makes: 0.4 + 0.4 x scale/0.9, 1 from 0.9 up.
	MCENTITY_API float JumpFraction(int32 HeldTicks);

	enum class EUse : uint8 { None, Mounted, Tamed, Saddled, Armored, Chested, Fed, Bucked };
	// A right click on it with Held in the hand (the player Rider, sneaking or not). Takes what it uses from Held (unless creative).
	// Returns what happened; Mounted means the player now rides it (the caller sets the pawn's RidingId).
	MCENTITY_API EUse Use(FMcEntityManager& Manager, FMcMob& Mob, FMcItemStack& Held, uint32 PlayerId, bool bCreative, bool bSneaking);
	// The rider's keys for the coming tick (forward, strafe, yaw in the game's degrees, the jump key down).
	MCENTITY_API void SetRiderInput(FMcEntity& Entity, float Forward, float Strafe, float Yaw, bool bJumpHeld, FName Held = NAME_None);
	// Takes the rider off; true if there was one.
	MCENTITY_API bool ClearRider(FMcEntity& Entity);
	// Where the rider's feet sit.
	MCENTITY_API FVector GetSeat(const FMcEntity& Entity);
	// Runs a mount that has a rider for a tick. False if it has none and the mob's own routine should run.
	MCENTITY_API bool TickRidden(FMcEntityManager& Manager, FMcMob& Mob);
}
