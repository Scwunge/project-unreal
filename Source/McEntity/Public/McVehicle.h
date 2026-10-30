// Minecarts and boats (minecraft.wiki: "Minecart", "Rail", "Boat"): entities a player rides. A minecart runs along
// rails, speeds up on powered ones and rolls on downhill; a boat floats on water and is steered with the keys.
//
// The rider is a player (the pawn that rides it tells the vehicle each tick: that it has a rider, what keys they
// hold and where they look, and takes their seat position back). A vehicle nobody has told for a few ticks has no
// rider. Hitting one breaks it: a minecart at once, a boat after enough hits (each adds ten times the damage, and it
// goes past 40); what it drops is its item, and nothing in creative.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
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
	// How hard one hit lands on it: a boat takes ten times the damage, and breaks past 40 (a minecart goes at once).
	virtual float GetWearPerDamage() const { return 10.0f; }
	// Whether it is put on a rail (a minecart) rather than on water.
	virtual bool RunsOnRails() const { return false; }

	// What a vehicle is made from: "minecraft:minecart", "minecraft:chest_minecart" and the like, or null if the type
	// is not one. A boat made this way is oak.
	static TUniquePtr<FMcVehicle> Create(FName Type);
	// The vehicle an item places ("minecraft:acacia_chest_boat"), set up to match it, or null if the item isn't one.
	static TUniquePtr<FMcVehicle> CreateFromItem(FName Item);
	static bool IsVehicleType(FName Type);
	static bool IsVehicleItem(FName Item);

	// What it carries: a chest cart or boat has 27 slots, a hopper cart 5; the rest none. Made when first asked for.
	virtual int32 GetInventorySlots() const { return 0; }
	const TSharedPtr<FMcContainer>& GetInventory();
	// Whether a player can sit in it (not in a chest, furnace, hopper or TNT cart).
	virtual bool CanRide() const { return true; }
	// A player clicks it holding Item, standing at From: what that does (fuel for a furnace cart, flame for a TNT cart).
	// Returns how it was used: nothing, or the item used up, or used and kept.
	enum class EUse : uint8 { None, Consumed, Kept };
	virtual EUse UseItem(FName Item, const FVector& From) { return EUse::None; }
	// A click opens the inventory rather than getting in: always for a cart with storage, for a chest boat when sneaking.
	virtual bool OpensOnUse(bool bSneaking) const { return GetInventorySlots() > 0; }
	// Throws what it carries out around it (when it breaks).
	void DropInventory();
	// What the window says when a player opens it.
	virtual FString GetInventoryTitle() const { return TEXT("Chest"); }
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
	// How much it has been hit.
	float Wear = 0.0f;
	TSharedPtr<FMcContainer> Inventory;
};

class MCENTITY_API FMcMinecartEntity final : public FMcVehicle
{
public:
	// What it carries: nothing, a chest, a furnace that pushes it, a hopper that picks up what lies in it, or TNT.
	enum class EVariant : uint8 { Plain, Chest, Furnace, Hopper, Tnt };
	explicit FMcMinecartEntity(EVariant InVariant = EVariant::Plain);
	// The plain minecart's type; the others have GetTypeName.
	static const FName& StaticType();
	// "minecraft:minecart", "minecraft:chest_minecart" and so on: the entity type and the item are the same name.
	static FName GetTypeName(EVariant Variant);
	static TOptional<EVariant> ParseType(FName Name);
	virtual FName GetType() const override { return GetTypeName(Variant); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool Serialize(FArchive& Ar) override;
	virtual FName GetItemName() const override { return GetTypeName(Variant); }
	virtual float GetWearPerDamage() const override { return 1000.0f; }
	virtual int32 GetInventorySlots() const override { return Variant == EVariant::Chest ? 27 : Variant == EVariant::Hopper ? 5 : 0; }
	virtual FString GetInventoryTitle() const override { return Variant == EVariant::Hopper ? TEXT("Minecart with Hopper") : TEXT("Minecart with Chest"); }
	virtual bool CanRide() const override { return Variant == EVariant::Plain; }
	virtual EUse UseItem(FName Item, const FVector& From) override;

	const EVariant Variant;
	// A furnace cart: ticks of fuel left, and which way it was pushed (a unit vector in x and z).
	int32 Fuel = 0;
	FVector2D PushDirection = FVector2D::ZeroVector;
	// A TNT cart: ticks until it goes off, or -1 while it isn't lit.
	int32 Fuse = -1;
	// A hopper cart on a powered activator rail picks nothing up.
	bool bHopperLocked = false;
	static constexpr int32 FuseTicks = 80;
	static constexpr int32 FuelPerItem = 3600;
	// How fast a furnace cart drives itself, blocks a tick.
	static constexpr double FurnaceSpeed = 0.2;
	virtual bool RunsOnRails() const override { return true; }
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
	// What the cart carries does each tick: fuel burns, a fuse runs down, a hopper takes in what lies in it.
	void TickVariant(FMcEntityManager& Manager);
	TOptional<FIntVector> LastDetector;
};

class MCENTITY_API FMcBoatEntity final : public FMcVehicle
{
public:
	explicit FMcBoatEntity(bool bInChest = false);
	static const FName& StaticType();
	static const FName& StaticChestType();
	// The bamboo raft is its own kind of entity, with a model of its own: a flat deck, no sides.
	static const FName& StaticRaftType();
	static const FName& StaticChestRaftType();
	static bool IsBoatType(FName Type);
	virtual FName GetType() const override
	{
		const bool bRaft = WoodName == TEXT("minecraft:bamboo");
		return bChest ? (bRaft ? StaticChestRaftType() : StaticChestType()) : (bRaft ? StaticRaftType() : StaticType());
	}
	virtual int32 GetInventorySlots() const override { return bChest ? 27 : 0; }
	virtual FString GetInventoryTitle() const override { return TEXT("Chest Boat"); }
	virtual bool OpensOnUse(bool bSneaking) const override { return bChest && bSneaking; }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool Serialize(FArchive& Ar) override;
	virtual FName GetItemName() const override;
	virtual FVector GetSeat() const override;

	// "minecraft:oak", "minecraft:birch" and so on: oak, spruce, birch, jungle, acacia, dark_oak, mangrove, cherry, pale_oak
	// and bamboo (a raft).
	FName WoodName = TEXT("minecraft:oak");
	// A chest boat carries 27 stacks and has no rider.
	const bool bChest;
	static const TArray<FName>& GetWoods();
	// The way it is facing, degrees; Yaw is the entity's own.
	float TurnRate = 0.0f;

	static constexpr double Gravity = 0.04;
	// How much a key press pushes it a tick.
	static constexpr double Thrust = 0.04;
	static constexpr double ReverseThrust = 0.005;
	// Which paddles row (bit 0 the left, bit 1 the right): both going forward or back, the left turning right and the
	// right turning left. Nothing rows without a rider. (RiderStrafe is positive to the left, as Turn reads it.)
	int32 GetPaddleBits() const
	{
		if (!HasRider())
		{
			return 0;
		}
		if (RiderForward != 0.0f)
		{
			return 3;
		}
		return (RiderStrafe < 0.0f ? 1 : 0) | (RiderStrafe > 0.0f ? 2 : 0);
	}
	// What it takes to break it.
	static constexpr float BreakWear = 40.0f;

private:
	enum class ESurface : uint8 { Air, Water, Underwater, Ground };
	// What the boat floats or rests on; fills the friction its glide keeps (0.9 water, 0.45 land, more on ice).
	ESurface Sense(FMcBlockWorld& World, double& OutSurface, double& OutFriction) const;
};
