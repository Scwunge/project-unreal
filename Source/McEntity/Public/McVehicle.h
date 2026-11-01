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

class FMcMob;

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
	// How many it carries: the player who drives it, and whoever else sits in (a boat takes two).
	virtual int32 GetCapacity() const { return 1; }
	// Where the feet of the one in seat Index are (0 the driver's).
	virtual FVector GetSeat(int32 Index = 0) const = 0;
	// Called every tick by each pawn that rides it, Key naming the pawn: it takes a seat if there is one free and the
	// first in is the driver, whose keys and look steer. False if it was full. The key of a lone rider can be left 0.
	bool SetRider(float Forward, float Strafe, float LookYaw, int32 Key = 0)
	{
		FSeat* Seat = Seats.FindByPredicate([Key](const FSeat& S) { return S.Key == Key; });
		if (!Seat)
		{
			if (!HasFreeSeat())
			{
				return false;
			}
			Seat = &Seats.Add_GetRef(FSeat{ Key });
		}
		Seat->Ticks = RiderGraceTicks;
		Seat->Forward = Forward;
		Seat->Strafe = Strafe;
		Seat->Yaw = LookYaw;
		UpdateDriver();
		return true;
	}
	// The rider got off (or was thrown off).
	void ClearRider(int32 Key = 0)
	{
		Seats.RemoveAll([Key](const FSeat& S) { return S.Key == Key; });
		UpdateDriver();
	}
	// Whether a player sits in it (the driver is the first in).
	bool HasRider() const { return Seats.Num() > 0; }
	// Everyone aboard, players and mobs.
	int32 GetPassengerCount() const { return Seats.Num() + MobPassengers.Num(); }
	bool HasFreeSeat() const { return GetPassengerCount() < GetCapacity(); }
	// Which seat a pawn has, or -1 if none.
	int32 GetSeatIndex(int32 Key) const { return Seats.IndexOfByPredicate([Key](const FSeat& S) { return S.Key == Key; }); }
	// Set by a powered activator rail: whoever rides is thrown off. Each pawn reads and clears its own; mobs get out
	// at the next TickPassengers, and none get in for half a second after.
	void EjectRiders()
	{
		for (FSeat& Seat : Seats)
		{
			Seat.bEject = true;
		}
		bEjectMobs = true;
	}
	bool ConsumeEject(int32 Key = 0)
	{
		FSeat* Seat = Seats.FindByPredicate([Key](const FSeat& S) { return S.Key == Key; });
		const bool bWas = Seat && Seat->bEject;
		if (Seat)
		{
			Seat->bEject = false;
		}
		return bWas;
	}

	static constexpr int32 RiderGraceTicks = 3;

protected:
	// Counts the riders' time down and drops those nobody has told it of for a few ticks; call at the top of Tick.
	void TickRider()
	{
		for (FSeat& Seat : Seats)
		{
			--Seat.Ticks;
		}
		Seats.RemoveAll([](const FSeat& S) { return S.Ticks <= 0; });
		UpdateDriver();
	}
	// Whose keys steer: the first seat's, or nobody's.
	void UpdateDriver()
	{
		const FSeat* Driver = Seats.Num() > 0 ? &Seats[0] : nullptr;
		RiderForward = Driver ? Driver->Forward : 0.0f;
		RiderStrafe = Driver ? Driver->Strafe : 0.0f;
		RiderYaw = Driver ? Driver->Yaw : 0.0f;
	}

	// Whether it takes this mob in when they touch (a boat does one smaller than itself; nothing else does yet).
	virtual bool CanCarryMob(const FMcMob& Mob) const { return false; }
	// Once a tick, after it has moved: drops mobs that got out, takes in those it touches while it has room and sets the
	// seated ones where they sit.
	void TickPassengers(FMcEntityManager& Manager);

	struct FSeat
	{
		int32 Key = 0;
		int32 Ticks = 0;
		float Forward = 0.0f;
		float Strafe = 0.0f;
		float Yaw = 0.0f;
		bool bEject = false;
	};
	TArray<FSeat> Seats;
	// Mobs sitting in it (see McMob), by entity id, after the players in the order of seats.
	TArray<int32> MobPassengers;
	bool bEjectMobs = false;
	int32 BoardCooldown = 0;
	float RiderForward = 0.0f;
	float RiderStrafe = 0.0f;
	float RiderYaw = 0.0f;
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
	// A plain cart going faster than 0.1 takes in a mob it runs into (not a player, who gets in by asking, nor an iron
	// golem). vehicle_log8.txt: one shoved off at 0.05, 0.098 by a cow standing in it takes the cow in at 0.144.
	virtual bool CanCarryMob(const FMcMob& Mob) const override;

	const EVariant Variant;
	// A furnace cart: ticks of fuel left, and which way it was pushed (a unit vector in x and z).
	int32 Fuel = 0;
	FVector2D PushDirection = FVector2D::ZeroVector;
	// A TNT cart: ticks until it goes off, or -1 while it isn't lit.
	int32 Fuse = -1;
	// A hopper cart on a powered activator rail picks nothing up.
	bool bHopperLocked = false;
	// Lit, it shows 79 that tick, counts down to 0, waits at 0 a tick and goes off the tick after (vehicle_log8.txt: lit
	// at 10.37, fuse 79 ... 0, 0, then gone), so 81 ticks from the one it was lit in.
	static constexpr int32 FuseTicks = 81;
	static constexpr int32 FuelPerItem = 3600;
	// How fast a furnace cart drives itself, blocks a tick.
	static constexpr double FurnaceSpeed = 0.2;
	// What vanilla's speed reads while one drives (it only ever moves 0.2 of it), which it hands on to a cart it runs
	// into (vehicle_log7.txt: steady at 3.455616, the cart it hits goes to 3.455616 + 0.05).
	static constexpr double FurnaceDriveSpeed = 3.455616;
	// Whether it is a furnace cart driving itself: fuel and a push from whoever fed it.
	bool IsDriving() const { return Variant == EVariant::Furnace && Fuel > 0 && !PushDirection.IsNearlyZero(); }
	// Another cart has run into this one (or this one into it, standing still): see PushMobs.
	void PushedBy(FMcMinecartEntity& Pusher);
	virtual bool RunsOnRails() const override { return true; }
	virtual FVector GetSeat(int32 Index = 0) const override { return Body.Position + FVector(0.0, 0.0, 0.0); }

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
	// Shoves the carts and mobs it touches, once it has moved.
	void PushMobs(FMcEntityManager& Manager);
	// How much of its speed it keeps a tick without a rider: 0.96, or for a chest or hopper cart 0.995 empty down to 0.98
	// full.
	double GetFriction();
	// The speed another cart sees when they meet: a driving furnace cart's is FurnaceDriveSpeed.
	FVector2D GetPushVelocity() const;
	void SetPushVelocity(const FVector2D& Velocity);
	TOptional<FIntVector> LastDetector;
	// Which way the rail it was last on runs: carts only shove one another along it.
	FVector2D RailAlong = FVector2D::ZeroVector;
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
	// Two, or one with a chest taking the back seat.
	virtual int32 GetCapacity() const override { return bChest ? 1 : 2; }
	virtual bool CanCarryMob(const FMcMob& Mob) const override;
	virtual FVector GetSeat(int32 Index = 0) const override;

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
	// Which paddles row (bit 0 the left, bit 1 the right): both going forward, the left turning right and the right
	// turning left; going back rows neither (paddle_log.txt: 30 ticks of the back key, the paddles at rest). Nothing rows
	// without a rider. (RiderStrafe is positive to the left, as Turn reads it.)
	int32 GetPaddleBits() const
	{
		if (!HasRider())
		{
			return 0;
		}
		if (RiderForward > 0.0f)
		{
			return 3;
		}
		return (RiderStrafe < 0.0f ? 1 : 0) | (RiderStrafe > 0.0f ? 2 : 0);
	}
	// What it takes to break it.
	static constexpr float BreakWear = 40.0f;

private:
	// UnderFlowing: under flowing water, a waterfall, which holds it down where still water would lift it.
	enum class ESurface : uint8 { Air, Water, Underwater, UnderFlowing, Ground };
	// Falling last tick, so landing in water stops it.
	bool bWasInAir = false;
	// What the boat floats or rests on; fills the friction its glide keeps (0.9 water, 0.6 stone, more on ice).
	ESurface Sense(FMcBlockWorld& World, double& OutSurface, double& OutFriction) const;
	// How much of its speed a boat on land keeps a tick, from the blocks under its hull.
	double GroundFriction(FMcBlockWorld& World) const;
};
