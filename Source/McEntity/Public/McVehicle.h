// Minecarts and boats (minecraft.wiki: "Minecart", "Rail", "Boat"): entities a player rides. A minecart runs along
// rails, speeds up on powered ones and rolls on downhill; a boat floats on water and is steered with the keys.
//
// The rider is a player (the pawn that rides it tells the vehicle each tick: that it has a rider, what keys they
// hold and where they look, and takes their seat position back). A vehicle nobody has told for a few ticks has no
// rider. Hitting one breaks it: a minecart at once, a boat after enough hits (each adds ten times the damage, and it
// goes past 40); what it drops is its item, and nothing in creative.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class MCENTITY_API FMcVehicle : public FMcEntity
{
public:
	virtual bool IsVehicle() const override { return true; }
	virtual bool IsAttackable() const override { return true; }
	virtual bool OnAttacked(float Damage, bool bCreative) override;
	virtual bool Serialize(FArchive& Ar) override;

	// The item it drops when broken, and what it is placed from.
	virtual FName GetItemName() const = 0;
	// Where the rider's feet are.
	virtual FVector GetSeat() const = 0;
	// Called every tick by the pawn that rides it.
	void SetRider(float Forward, float Strafe, float LookYaw)
	{
		RiderTicks = RiderGraceTicks;
		RiderForward = Forward;
		RiderStrafe = Strafe;
		RiderYaw = LookYaw;
	}
	// The rider got off (or was thrown off).
	void ClearRider() { RiderTicks = 0; }
	bool HasRider() const { return RiderTicks > 0; }
	// Set by a powered activator rail: whoever rides is thrown off. The pawn reads and clears it.
	bool ConsumeEject()
	{
		const bool bWas = bEject;
		bEject = false;
		return bWas;
	}

	static constexpr int32 RiderGraceTicks = 3;

protected:
	// Counts the rider's time down; call at the top of Tick.
	void TickRider()
	{
		RiderTicks = FMath::Max(0, RiderTicks - 1);
	}

	int32 RiderTicks = 0;
	float RiderForward = 0.0f;
	float RiderStrafe = 0.0f;
	float RiderYaw = 0.0f;
	bool bEject = false;
	// Boats: how much it has been hit.
	float Wear = 0.0f;
};

class MCENTITY_API FMcMinecartEntity final : public FMcVehicle
{
public:
	FMcMinecartEntity();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual FName GetItemName() const override { return TEXT("minecraft:minecart"); }
	virtual FVector GetSeat() const override { return Body.Position + FVector(0.0, 0.0, 0.0); }

	// The fastest a cart goes along a rail, blocks a tick (8 a second).
	static constexpr double MaxSpeed = 0.4;
	static constexpr double Gravity = 0.04;
	// Height of the floor above the rail it runs on.
	static constexpr double RailLift = 0.0625;
	// Slopes pull on a cart: the speed gained or lost a tick.
	static constexpr double SlopePull = 0.0078125;
	// Powered rails add this a tick to a cart that is moving; an unpowered one brakes it.
	static constexpr double PoweredBoost = 0.06;

	// The block of the rail it is on this tick, if it is on one (for tests and the detector).
	TOptional<FIntVector> OnRail;

private:
	// One tick of running along a track. Returns false if the cart isn't on one.
	bool TickOnRail(FMcEntityManager& Manager, FMcBlockWorld& World);
	TOptional<FIntVector> LastDetector;
};

class MCENTITY_API FMcBoatEntity final : public FMcVehicle
{
public:
	FMcBoatEntity();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool Serialize(FArchive& Ar) override;
	virtual FName GetItemName() const override { return FName(*(WoodName.ToString() + (WoodName == TEXT("minecraft:bamboo") ? TEXT("_raft") : TEXT("_boat")))); }
	virtual FVector GetSeat() const override;

	// "minecraft:oak", "minecraft:birch" and so on.
	FName WoodName = TEXT("minecraft:oak");
	// The way it is facing, degrees; Yaw is the entity's own.
	float TurnRate = 0.0f;

	static constexpr double Gravity = 0.04;
	// How much a key press pushes it a tick.
	static constexpr double Thrust = 0.04;
	static constexpr double ReverseThrust = 0.005;
	// What it takes to break it.
	static constexpr float BreakWear = 40.0f;

private:
	enum class ESurface : uint8 { Air, Water, Underwater, Ground };
	// What the boat floats or rests on; fills the friction its glide keeps (0.9 water, 0.45 land, more on ice).
	ESurface Sense(FMcBlockWorld& World, double& OutSurface, double& OutFriction) const;
};
