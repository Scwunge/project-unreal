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
#include "McContainer.h"
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
	// The window's slots: the saddle (0), the armour (1) and the chest's 15 from 2. bSaddled, Armor and bChest follow it (McMount::Sync).
	TSharedPtr<FMcContainer> Inventory;
	// How used to people it is, 0..100; a rider tames it when a roll under the max beats it.
	int32 Temper = 0;
	// The mob's own max health, rolled at spawn for a horse, donkey or mule (15 + a roll of 0..7 + a roll of 0..8, vehicle_log43); 0 for the type's. Saved.
	float MaxHealth = 0.0f;
	// The player riding it (the pawn's unique id), 0 for none.
	uint32 RiderId = 0;
	// Camels carry a second rider behind the first, who steers.
	uint32 PassengerId = 0;
	// The rider's keys this tick.
	float Forward = 0.0f;
	float Strafe = 0.0f;
	float RiderYaw = 0.0f;
	bool bJumpHeld = false;
	// Ticks the jump key has been held, and the jump waiting for the ground (0 for none).
	int32 JumpHoldTicks = 0;
	float PendingJump = 0.0f;
	// Llamas (McLlama.h): the llama ahead in a caravan, the hurts seen, ticks left to spit and until the next spit.
	int32 CaravanHead = 0;
	int32 LastHurtEvents = 0;
	int32 SpitMemory = 0;
	int32 SpitCooldown = 0;
	// The rear a horse makes on a jump, 0..1 (vehicle_log34): the rider sits 0.7 x it back along the facing and 0.15 x it higher. Rises by 0.45 x 0.6^n a tick
	// from the jump (RearStep n, -1 for none) until the landing, then falls away over five ticks (RearFall, -1 for none).
	float Rear = 0.0f;
	int32 RearStep = -1;
	int32 RearFall = -1;
	// Camels: ticks until it may dash again.
	int32 DashCooldown = 0;
	// Camels: lying down (not saved), and ticks since it last sat or stood (reference: it may change after 400, then sits at about 1 in 190 a tick and stands at 1 in 39).
	bool bSitting = false;
	int32 PoseTicks = 0;
	// It has sat or stood at least once (so the stand-up or sit-down is drawn for the first 52 ticks).
	bool bPoseChanged = false;
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
	// What a mob's health tops out at: its own roll when it has one, else the type's.
	MCENTITY_API float MaxHealthOf(const FMcMob& Mob);
	// Rolls a horse, donkey or mule's max health, speed and jump when it spawns (the rest are made when first used).
	MCENTITY_API void RollAtSpawn(FMcMob& Mob);
	// The scale a charge of this many held ticks reaches, 0..1 (0.1 a tick, then slowing to a full charge at 10).
	MCENTITY_API float ChargeScale(int32 HeldTicks);
	// The part of the jump strength a charge of HeldTicks makes: 0.4 + 0.4 x scale/0.9, 1 from 0.9 up.
	MCENTITY_API float JumpFraction(int32 HeldTicks);

	enum class EUse : uint8 { None, Mounted, Tamed, Saddled, Armored, Chested, Fed, Bucked, Inventory };
	// A right click on it with Held in the hand (the player Rider, sneaking or not). Takes what it uses from Held (unless creative).
	// Returns what happened; Mounted means the player now rides it (the caller sets the pawn's RidingId).
	MCENTITY_API EUse Use(FMcEntityManager& Manager, FMcMob& Mob, FMcItemStack& Held, uint32 PlayerId, bool bCreative, bool bSneaking);
	// The rider's keys for the coming tick (forward, strafe, yaw in the game's degrees, the jump key down).
	MCENTITY_API void SetRiderInput(FMcEntity& Entity, float Forward, float Strafe, float Yaw, bool bJumpHeld, FName Held = NAME_None);
	// Takes the rider off; true if there was one.
	MCENTITY_API bool ClearRider(FMcEntity& Entity);
	// Takes that player off, rider or passenger; the passenger moves up to steer when the rider goes.
	MCENTITY_API bool ClearPlayer(FMcEntity& Entity, uint32 PlayerId);
	// Whether that player sits on it, and whether they are the one who steers.
	MCENTITY_API bool HasPlayer(const FMcEntity& Entity, uint32 PlayerId);
	// Where a player riding it sits (a camel's first rider sits 0.5 ahead of its centre and the passenger 0.7 behind it).
	MCENTITY_API FVector GetSeatFor(const FMcEntity& Entity, uint32 PlayerId);
	// Where the rider's feet sit.
	MCENTITY_API FVector GetSeat(const FMcEntity& Entity);
	// Armour points of a horse armour item (leather 3, copper 4, iron 5, golden 7, diamond 11, netherite 19; the game's tables), 0 for none.
	MCENTITY_API int32 ArmorPoints(FName Item);
	// The damage left after the armour: the game's formula, a cut of min(20, max(armor/5, armor - damage/2))/25 (no toughness).
	MCENTITY_API float ReduceDamage(const FMcMountState& State, float Damage);
	// The window's container, made on first use (2 slots, 17 with a chest).
	MCENTITY_API const TSharedPtr<FMcContainer>& GetInventory(FMcMountState& State);
	// Reads the saddle, armour and chest back from the container after a player moved things in the window.
	MCENTITY_API void Sync(FMcMountState& State);
	// Saves or loads the lasting part (tamed, saddle, armour, chest, temper, stats); a rider and the boost start afresh. Older saves end before it.
	MCENTITY_API void Serialize(FArchive& Ar, FMcMob& Mob);
	// Runs a mount that has a rider for a tick. False if it has none and the mob's own routine should run.
	// A camel's random lying down and getting up; true while it lies still (the mob's own movement is skipped).
	MCENTITY_API bool TickSitting(FMcMob& Mob);
	MCENTITY_API bool TickRidden(FMcEntityManager& Manager, FMcMob& Mob);
}
