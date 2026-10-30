#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McItemStack.h"
#include "McMob.h"
#include "McVehicle.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVehicleTest, "McClient.McEntity.Vehicles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcVehicleTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	for (int32 X = -16; X <= 31; ++X)
	{
		for (int32 Z = -16; Z <= 31; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
		}
	}
	FMcEntityManager Manager(World);

	// ---- A minecart on a straight rail rolls and slows ----
	for (int32 X = 0; X <= 12; ++X)
	{
		World.SetBlock(FIntVector(X, 1, 0), State(TEXT("minecraft:rail[shape=east_west]")));
	}
	{
		FMcMinecartEntity* Cart = new FMcMinecartEntity();
		Cart->Body.Position = FVector(2.5, 1.0625, 0.5);
		Cart->Body.Velocity = FVector(0.3, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Cart));
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a pushed cart runs along the rail"), Cart->Body.Position.X > 3.5);
		TestNearlyEqual(TEXT("and stays on its line"), static_cast<float>(Cart->Body.Position.Z), 0.5f, 0.01f);
		TestNearlyEqual(TEXT("at rail height"), static_cast<float>(Cart->Body.Position.Y), 1.0625f, 0.05f);
		const double Fast = Cart->Body.Velocity.X;
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("empty, it slows down"), Cart->Body.Velocity.X < Fast);
		Cart->bRemoved = true;
		Manager.Tick();
	}

	// ---- A powered rail speeds a moving cart up, an unpowered one stops it ----
	for (int32 X = 0; X <= 12; ++X)
	{
		World.SetBlock(FIntVector(X, 1, 2), State(TEXT("minecraft:powered_rail[powered=true,shape=east_west]")));
		World.SetBlock(FIntVector(X, 1, 4), State(TEXT("minecraft:powered_rail[powered=false,shape=east_west]")));
	}
	{
		FMcMinecartEntity* Boosted = new FMcMinecartEntity();
		Boosted->Body.Position = FVector(0.5, 1.0625, 2.5);
		Boosted->Body.Velocity = FVector(0.1, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Boosted));
		FMcMinecartEntity* Braked = new FMcMinecartEntity();
		Braked->Body.Position = FVector(0.5, 1.0625, 4.5);
		Braked->Body.Velocity = FVector(0.1, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Braked));
		for (int32 Tick = 0; Tick < 8; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a powered rail adds speed"), Boosted->Body.Velocity.X > 0.2);
		TestTrue(TEXT("and an unpowered one holds a cart back"), Braked->Body.Velocity.X < 0.05);
		Boosted->bRemoved = true;
		Braked->bRemoved = true;
		Manager.Tick();
	}

	// ---- A slope pulls a cart down it ----
	World.SetBlock(FIntVector(20, 1, 0), State(TEXT("minecraft:rail[shape=ascending_east]")));
	World.SetBlock(FIntVector(21, 2, 0), State(TEXT("minecraft:rail[shape=east_west]")));
	World.SetBlock(FIntVector(19, 1, 0), State(TEXT("minecraft:rail[shape=east_west]")));
	{
		FMcMinecartEntity* Cart = new FMcMinecartEntity();
		Cart->Body.Position = FVector(20.9, 1.9, 0.5);
		Cart->Body.Velocity = FVector::ZeroVector;
		Manager.Add(TUniquePtr<FMcEntity>(Cart));
		for (int32 Tick = 0; Tick < 10; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a cart on a slope rolls down it, to the low side"), Cart->Body.Position.X < 20.7);
		Cart->bRemoved = true;
		Manager.Tick();
	}

	// ---- A cart carries its rider, who steers a boat ----
	{
		FMcMinecartEntity Cart;
		TestFalse(TEXT("nobody sits in a new cart"), Cart.HasRider());
		Cart.SetRider(0.0f, 0.0f, 0.0f);
		TestTrue(TEXT("a rider is told"), Cart.HasRider());
		Cart.ClearRider();
		TestFalse(TEXT("and gets out"), Cart.HasRider());
	}

	// ---- A boat's paddles row with its rider ----
	{
		FMcBoatEntity Boat;
		TestEqual(TEXT("no rider, no rowing"), Boat.GetPaddleBits(), 0);
		Boat.SetRider(0.0f, 0.0f, 0.0f);
		TestEqual(TEXT("a rider doing nothing rows nothing"), Boat.GetPaddleBits(), 0);
		Boat.SetRider(1.0f, 0.0f, 0.0f);
		TestEqual(TEXT("forward rows both"), Boat.GetPaddleBits(), 3);
		Boat.SetRider(0.0f, -1.0f, 0.0f);
		TestEqual(TEXT("turning right rows the left paddle"), Boat.GetPaddleBits(), 1);
		Boat.SetRider(0.0f, 1.0f, 0.0f);
		TestEqual(TEXT("turning left rows the right paddle"), Boat.GetPaddleBits(), 2);
	}

	// ---- Two riders in a boat, one in a cart ----
	{
		FMcBoatEntity Boat;
		Boat.Yaw = 0.0f;
		TestEqual(TEXT("a boat carries two"), Boat.GetCapacity(), 2);
		TestTrue(TEXT("the first sits"), Boat.SetRider(1.0f, 0.0f, 0.0f, 11));
		const FVector Alone = Boat.GetSeat(0);
		TestTrue(TEXT("and the second"), Boat.SetRider(0.0f, 1.0f, 0.0f, 22));
		TestFalse(TEXT("a third finds it full"), Boat.SetRider(0.0f, 0.0f, 0.0f, 33));
		TestEqual(TEXT("the first is the driver"), Boat.GetPaddleBits(), 3);
		TestEqual(TEXT("seats are in the order they were taken"), Boat.GetSeatIndex(22), 1);
		TestTrue(TEXT("two sit apart, front and back along the way it faces (south at yaw 0)"), Boat.GetSeat(0).Z > Alone.Z && Boat.GetSeat(1).Z < Alone.Z);
		Boat.ClearRider(11);
		TestEqual(TEXT("the second becomes the driver when the first gets out"), Boat.GetPaddleBits(), 2);
		TestTrue(TEXT("and there is room for another"), Boat.HasFreeSeat());
		Boat.EjectRiders();
		TestTrue(TEXT("a rider thrown off is told so once"), Boat.ConsumeEject(22));
		TestFalse(TEXT("and only once"), Boat.ConsumeEject(22));

		FMcMinecartEntity Cart;
		TestTrue(TEXT("a cart takes one"), Cart.SetRider(0.0f, 0.0f, 0.0f, 5));
		TestFalse(TEXT("and not a second"), Cart.SetRider(0.0f, 0.0f, 0.0f, 6));
	}

	// ---- A cart shoves mobs aside, and a plain one going along takes one in ----
	{
		FMcMinecartEntity* Chest = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Chest);
		Chest->Body.Position = FVector(2.5, 1.0625, 0.5);
		Chest->Body.Velocity = FVector(0.3, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Chest));
		FMcMob* Pig = new FMcMob(*FMcMobType::Find(TEXT("minecraft:pig")));
		Pig->Body.Position = FVector(3.3, 1.0, 0.7);
		Manager.Add(TUniquePtr<FMcEntity>(Pig));
		const FVector PigBefore = Pig->Body.Position;
		Manager.Tick();
		Manager.Tick();
		TestEqual(TEXT("a chest cart takes no mob in"), Pig->RidingId, 0);
		TestTrue(TEXT("but shoves it out of the way"), FVector2D::Distance(FVector2D(Pig->Body.Position.X, Pig->Body.Position.Z), FVector2D(PigBefore.X, PigBefore.Z)) > 0.1);
		Chest->bRemoved = true;
		Pig->bRemoved = true;
		Manager.Tick();

		FMcMinecartEntity* Still = new FMcMinecartEntity();
		Still->Body.Position = FVector(8.5, 1.0625, 0.5);
		Manager.Add(TUniquePtr<FMcEntity>(Still));
		FMcMob* Cow = new FMcMob(*FMcMobType::Find(TEXT("minecraft:cow")));
		Cow->Body.Position = FVector(8.5, 1.0, 0.5);
		Manager.Add(TUniquePtr<FMcEntity>(Cow));
		Manager.Tick();
		TestEqual(TEXT("a cart standing still takes nothing in"), Cow->RidingId, 0);
		Still->bRemoved = true;
		Manager.Tick();

		FMcMinecartEntity* Rolling = new FMcMinecartEntity();
		Rolling->Body.Position = FVector(4.5, 1.0625, 0.5);
		Rolling->Body.Velocity = FVector(0.3, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Rolling));
		Cow->Body.Position = FVector(5.2, 1.0, 0.5);
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("a plain cart running into a mob takes it in"), Cow->RidingId, Rolling->Id);
		TestFalse(TEXT("so a player can't get in"), Rolling->SetRider(0.0f, 0.0f, 0.0f, 3));
		TestTrue(TEXT("and the mob goes with it, sitting down in it"), FVector2D::Distance(FVector2D(Cow->Body.Position.X, Cow->Body.Position.Z), FVector2D(Rolling->Body.Position.X, Rolling->Body.Position.Z)) < 0.01
			&& Cow->Body.Position.Y < Rolling->GetSeat(0).Y);
		Rolling->EjectRiders();
		Manager.Tick();
		TestEqual(TEXT("an activator rail throws the mob out"), Cow->RidingId, 0);
		Manager.Tick();
		TestEqual(TEXT("and it isn't taken straight back in"), Cow->RidingId, 0);
		Rolling->bRemoved = true;
		Cow->bRemoved = true;
		Manager.Tick();
	}

	// ---- Breaking one ----
	{
		FMcMinecartEntity* Cart = new FMcMinecartEntity();
		Cart->Body.Position = FVector(5.5, 1.0625, 0.5);
		Manager.Add(TUniquePtr<FMcEntity>(Cart));
		TestTrue(TEXT("a hit lands"), Cart->OnAttacked(1.0f, false));
		TestTrue(TEXT("and breaks a cart at once"), Cart->bRemoved);
		int32 Drops = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			Drops += Entity->GetType() == FMcItemEntity::StaticType() ? 1 : 0;
		}
		TestEqual(TEXT("leaving a minecart item"), Drops, 1);
		Manager.Tick();
	}
	{
		FMcBoatEntity* Boat = new FMcBoatEntity();
		Boat->Body.Position = FVector(5.5, 1.0, 8.5);
		Manager.Add(TUniquePtr<FMcEntity>(Boat));
		TestTrue(TEXT("a boat takes a hit"), Boat->OnAttacked(1.0f, false));
		TestFalse(TEXT("and survives a light one"), Boat->bRemoved);
		Boat->OnAttacked(4.0f, false);
		TestTrue(TEXT("but not enough of them"), Boat->bRemoved);
		Manager.Tick();
		FMcBoatEntity* Gentle = new FMcBoatEntity();
		Manager.Add(TUniquePtr<FMcEntity>(Gentle));
		Gentle->OnAttacked(1.0f, true);
		TestTrue(TEXT("a creative blow breaks it at once"), Gentle->bRemoved);
		Manager.Tick();
	}

	// ---- A boat floats, and goes where its rider steers ----
	for (int32 X = 0; X <= 12; ++X)
	{
		for (int32 Z = 10; Z <= 20; ++Z)
		{
			World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:water")));
			World.SetBlock(FIntVector(X, 2, Z), State(TEXT("minecraft:water")));
		}
	}
	{
		FMcBoatEntity* Boat = new FMcBoatEntity();
		Boat->Body.Position = FVector(5.5, 3.0, 12.5);
		Manager.Add(TUniquePtr<FMcEntity>(Boat));
		for (int32 Tick = 0; Tick < 80; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a boat dropped on a pond floats at its surface"), Boat->Body.Position.Y > 2.0 && Boat->Body.Position.Y < 3.2);
		const FVector Before = Boat->Body.Position;
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			Boat->SetRider(1.0f, 0.0f, 0.0f);
			Manager.Tick();
		}
		TestTrue(TEXT("pushed forward by its rider, it moves"), FVector::Dist(Before, Boat->Body.Position) > 0.5);
		Boat->bRemoved = true;
		Manager.Tick();
	}
	{
		// A waterfall onto the pond: the boat under it stays down in the pond, where still water would lift it to the top.
		for (int32 Y = 3; Y <= 6; ++Y)
		{
			World.SetBlock(FIntVector(9, Y, 18), State(TEXT("minecraft:water[level=8]")));
		}
		World.SetBlock(FIntVector(9, 7, 18), State(TEXT("minecraft:water")));
		FMcBoatEntity* Boat = new FMcBoatEntity();
		Boat->Body.Position = FVector(9.5, 2.5, 18.5);
		Manager.Add(TUniquePtr<FMcEntity>(Boat));
		for (int32 Tick = 0; Tick < 80; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a boat under a waterfall doesn't climb it"), Boat->Body.Position.Y < 3.0);
		Boat->bRemoved = true;
		Manager.Tick();
		for (int32 Y = 3; Y <= 7; ++Y)
		{
			World.SetBlock(FIntVector(9, Y, 18), State(TEXT("minecraft:air")));
		}
	}

	// ---- A boat takes in the mobs it bumps into, up to its seats ----
	{
		FMcBoatEntity* Boat = new FMcBoatEntity();
		Boat->Body.Position = FVector(5.5, 2.0, 15.5);
		Manager.Add(TUniquePtr<FMcEntity>(Boat));
		TArray<FMcMob*> Pigs;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FMcMob* Pig = new FMcMob(*FMcMobType::Find(TEXT("minecraft:pig")));
			Pig->Body.Position = FVector(5.5, 2.0, 15.5);
			Manager.Add(TUniquePtr<FMcEntity>(Pig));
			Pigs.Add(Pig);
		}
		Manager.Tick();
		Manager.Tick();
		int32 Aboard = 0;
		for (const FMcMob* Pig : Pigs)
		{
			Aboard += Pig->RidingId == Boat->Id ? 1 : 0;
		}
		TestEqual(TEXT("two pigs get in and the third is left"), Aboard, 2);
		TestEqual(TEXT("the boat counts them"), Boat->GetPassengerCount(), 2);
		TestFalse(TEXT("so a player finds it full"), Boat->SetRider(0.0f, 0.0f, 0.0f, 9));
		const FVector Where = Boat->Body.Position;
		TestTrue(TEXT("a pig sits where the boat is"), FVector::Dist(Pigs[0]->Body.Position, Where) < 1.0);
		TestTrue(TEXT("down in the hull, its feet below the seat"), Pigs[0]->Body.Position.Y < Boat->GetSeat(0).Y - 0.3);
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("and stays there"), FVector::Dist(Pigs[0]->Body.Position, Boat->Body.Position) < 1.0 && Pigs[0]->RidingId == Boat->Id);
		Boat->bRemoved = true;
		Manager.Tick();
		Manager.Tick();
		TestEqual(TEXT("they get out when it breaks"), Pigs[0]->RidingId, 0);
		for (FMcMob* Pig : Pigs)
		{
			Pig->bRemoved = true;
		}
		Manager.Tick();

		// A player in it leaves one seat.
		FMcBoatEntity* Second = new FMcBoatEntity();
		Second->Body.Position = FVector(8.5, 2.0, 15.5);
		Manager.Add(TUniquePtr<FMcEntity>(Second));
		Second->SetRider(0.0f, 0.0f, 0.0f, 4);
		FMcMob* Cow = new FMcMob(*FMcMobType::Find(TEXT("minecraft:cow")));
		Cow->Body.Position = FVector(8.5, 2.0, 15.5);
		Manager.Add(TUniquePtr<FMcEntity>(Cow));
		FMcMob* Sheep = new FMcMob(*FMcMobType::Find(TEXT("minecraft:sheep")));
		Sheep->Body.Position = FVector(8.5, 2.0, 15.5);
		Manager.Add(TUniquePtr<FMcEntity>(Sheep));
		Manager.Tick();
		Manager.Tick();
		FMcBoatEntity ChestBoat(true);
		TestEqual(TEXT("a chest boat seats one"), ChestBoat.GetCapacity(), 1);
		TestEqual(TEXT("a boat with a player takes one mob"), (Cow->RidingId == Second->Id ? 1 : 0) + (Sheep->RidingId == Second->Id ? 1 : 0), 1);
		Second->bRemoved = true;
		Cow->bRemoved = true;
		Sheep->bRemoved = true;
		Manager.Tick();
	}

	// ---- Saving ----
	{
		FMcBoatEntity Boat;
		Boat.WoodName = TEXT("minecraft:birch");
		Boat.Body.Position = FVector(1.0, 2.0, 3.0);
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Boat.Serialize(Writer);
		FMcBoatEntity Loaded;
		FMemoryReader Reader(Bytes);
		TestTrue(TEXT("a boat reads back"), Loaded.Serialize(Reader));
		TestEqual(TEXT("with its wood"), Loaded.WoodName, FName(TEXT("minecraft:birch")));
		TestEqual(TEXT("which is its item"), Loaded.GetItemName(), FName(TEXT("minecraft:birch_boat")));
		TestNearlyEqual(TEXT("and where it was"), static_cast<float>(Loaded.Body.Position.Z), 3.0f);
	}

	// ---- Boats of every wood, and chest boats ----
	{
		const TCHAR* Woods[] = { TEXT("oak"), TEXT("spruce"), TEXT("birch"), TEXT("jungle"), TEXT("acacia"), TEXT("dark_oak"), TEXT("mangrove"),
			TEXT("cherry"), TEXT("pale_oak"), TEXT("bamboo") };
		for (const TCHAR* Wood : Woods)
		{
			const bool bRaft = FCString::Strcmp(Wood, TEXT("bamboo")) == 0;
			const FName Item(*(FString(TEXT("minecraft:")) + Wood + (bRaft ? TEXT("_raft") : TEXT("_boat"))));
			const FName ChestItem(*(FString(TEXT("minecraft:")) + Wood + (bRaft ? TEXT("_chest_raft") : TEXT("_chest_boat"))));
			TUniquePtr<FMcVehicle> Plain = FMcVehicle::CreateFromItem(Item);
			TUniquePtr<FMcVehicle> WithChest = FMcVehicle::CreateFromItem(ChestItem);
			if (!TestTrue(*FString::Printf(TEXT("%s places a boat"), *Item.ToString()), Plain.IsValid() && WithChest.IsValid()))
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("%s drops itself"), *Item.ToString()), Plain->GetItemName(), Item);
			TestEqual(*FString::Printf(TEXT("%s drops itself"), *ChestItem.ToString()), WithChest->GetItemName(), ChestItem);
			TestEqual(TEXT("a plain boat has no storage"), Plain->GetInventorySlots(), 0);
			TestEqual(TEXT("a chest boat has 27 slots"), WithChest->GetInventorySlots(), 27);
			TestTrue(TEXT("the item's name is a type too"), FMcVehicle::IsVehicleType(Item) && FMcVehicle::IsVehicleType(ChestItem));
			// The bamboo raft is a kind of its own (its own model); the other woods share the boat's.
			TestEqual(TEXT("which type it is"), Plain->GetType(), bRaft ? FMcBoatEntity::StaticRaftType() : FMcBoatEntity::StaticType());
			TestEqual(TEXT("and the chest one"), WithChest->GetType(), bRaft ? FMcBoatEntity::StaticChestRaftType() : FMcBoatEntity::StaticChestType());
			TestTrue(TEXT("a saved one is made again from its type"), FMcVehicle::Create(Plain->GetType()).IsValid() && FMcVehicle::Create(WithChest->GetType())->GetInventorySlots() == 27);
		}
		TestFalse(TEXT("a bamboo boat is not a thing"), FMcVehicle::IsVehicleItem(TEXT("minecraft:bamboo_boat")));
		TestFalse(TEXT("nor is an oak raft"), FMcVehicle::IsVehicleItem(TEXT("minecraft:oak_raft")));
		TestFalse(TEXT("nor a poplar boat"), FMcVehicle::IsVehicleItem(TEXT("minecraft:poplar_boat")));
		TestFalse(TEXT("a stick is not a vehicle"), FMcVehicle::IsVehicleItem(TEXT("minecraft:stick")));
		TestTrue(TEXT("the minecart is"), FMcVehicle::IsVehicleItem(TEXT("minecraft:minecart")));
		TestFalse(TEXT("only a minecart runs on rails"), FMcVehicle::CreateFromItem(TEXT("minecraft:oak_boat"))->RunsOnRails());
		TestTrue(TEXT("the minecart does"), FMcVehicle::CreateFromItem(TEXT("minecraft:minecart"))->RunsOnRails());

		// A chest boat keeps what it is given through a save, and drops it when it breaks.
		FMcBoatEntity Chest(true);
		Chest.WoodName = TEXT("minecraft:cherry");
		TestEqual(TEXT("its type is the chest boat's"), Chest.GetType(), FMcBoatEntity::StaticChestType());
		Chest.GetInventory()->Set(3, FMcItemStack(TEXT("minecraft:diamond"), 5));
		Chest.GetInventory()->Set(26, FMcItemStack(TEXT("minecraft:stick"), 64));
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Chest.Serialize(Writer);
		TUniquePtr<FMcVehicle> Loaded = FMcVehicle::Create(FMcBoatEntity::StaticChestType());
		FMemoryReader Reader(Bytes);
		TestTrue(TEXT("a chest boat reads back"), Loaded.IsValid() && Loaded->Serialize(Reader));
		if (Loaded.IsValid())
		{
			TestEqual(TEXT("with its diamonds"), Loaded->GetInventory()->Get(3).Count, 5);
			TestEqual(TEXT("and its sticks in the last slot"), Loaded->GetInventory()->Get(26).Item, FName(TEXT("minecraft:stick")));
			TestEqual(TEXT("and its wood"), static_cast<FMcBoatEntity&>(*Loaded).WoodName, FName(TEXT("minecraft:cherry")));
		}
		FMcBoatEntity* Breaking = new FMcBoatEntity(true);
		Breaking->Body.Position = FVector(5.5, 1.0, 8.5);
		Breaking->GetInventory()->Set(0, FMcItemStack(TEXT("minecraft:apple"), 2));
		Manager.Add(TUniquePtr<FMcEntity>(Breaking));
		Breaking->OnAttacked(10.0f, false);
		int32 Items = 0;
		bool bApples = false, bBoat = false;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (Entity->GetType() == FMcItemEntity::StaticType())
			{
				++Items;
				const FMcItemEntity& Dropped = static_cast<const FMcItemEntity&>(*Entity);
				bApples |= Dropped.Stack.Item == TEXT("minecraft:apple") && Dropped.Stack.Count == 2;
				bBoat |= Dropped.Stack.Item == TEXT("minecraft:oak_chest_boat");
			}
		}
		TestTrue(TEXT("a broken chest boat drops its boat and what it carried"), Items >= 2 && bApples && bBoat);
		Manager.Tick();
		TestTrue(TEXT("only a sneaking click opens a chest boat"), Chest.OpensOnUse(true) && !Chest.OpensOnUse(false));
	}

	// ---- Carts with a load ----
	{
		const TCHAR* Names[] = { TEXT("chest_minecart"), TEXT("furnace_minecart"), TEXT("hopper_minecart"), TEXT("tnt_minecart") };
		const int32 Slots[] = { 27, 0, 5, 0 };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FName Item(*(FString(TEXT("minecraft:")) + Names[Index]));
			TUniquePtr<FMcVehicle> Cart = FMcVehicle::CreateFromItem(Item);
			if (!TestTrue(*FString::Printf(TEXT("%s places a cart"), *Item.ToString()), Cart.IsValid() && Cart->RunsOnRails()))
			{
				continue;
			}
			TestEqual(TEXT("it drops itself"), Cart->GetItemName(), Item);
			TestEqual(TEXT("its type is its name"), Cart->GetType(), Item);
			TestEqual(TEXT("it carries what its kind does"), Cart->GetInventorySlots(), Slots[Index]);
			TestFalse(TEXT("nobody can sit in it"), Cart->CanRide());
			TestTrue(TEXT("the type is a vehicle's"), FMcVehicle::IsVehicleType(Item));
		}
		TestTrue(TEXT("a plain cart can be sat in"), FMcVehicle::CreateFromItem(TEXT("minecraft:minecart"))->CanRide());
		TestFalse(TEXT("a command block minecart is not made"), FMcVehicle::IsVehicleItem(TEXT("minecraft:command_block_minecart")));

		for (int32 X = 0; X <= 12; ++X)
		{
			World.SetBlock(FIntVector(X, 1, 24), State(TEXT("minecraft:rail[shape=east_west]")));
			World.SetBlock(FIntVector(X, 1, 26), State(TEXT("minecraft:rail[shape=east_west]")));
			World.SetBlock(FIntVector(X, 1, 28), State(TEXT("minecraft:activator_rail[powered=true,shape=east_west]")));
			World.SetBlock(FIntVector(X, 1, -12), State(TEXT("minecraft:rail[shape=east_west]")));
		}

		// A chest cart keeps what it is given through a save, and spills it when it breaks.
		{
			FMcMinecartEntity Chest(FMcMinecartEntity::EVariant::Chest);
			Chest.GetInventory()->Set(0, FMcItemStack(TEXT("minecraft:gold_ingot"), 7));
			TArray<uint8> Bytes;
			FMemoryWriter Writer(Bytes);
			Chest.Serialize(Writer);
			TUniquePtr<FMcVehicle> Loaded = FMcVehicle::Create(TEXT("minecraft:chest_minecart"));
			FMemoryReader Reader(Bytes);
			TestTrue(TEXT("a chest cart reads back"), Loaded.IsValid() && Loaded->Serialize(Reader));
			TestEqual(TEXT("with its gold"), Loaded->GetInventory()->Get(0).Count, 7);
			TestTrue(TEXT("a click opens it, sneaking or not"), Chest.OpensOnUse(false) && Chest.OpensOnUse(true));

			FMcMinecartEntity* Breaking = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Chest);
			Breaking->Body.Position = FVector(5.5, 1.0625, 24.5);
			Breaking->GetInventory()->Set(1, FMcItemStack(TEXT("minecraft:apple"), 3));
			Manager.Add(TUniquePtr<FMcEntity>(Breaking));
			Breaking->OnAttacked(1.0f, false);
			bool bApples = false, bCart = false;
			for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
			{
				if (Entity->GetType() == FMcItemEntity::StaticType())
				{
					const FMcItemEntity& Dropped = static_cast<const FMcItemEntity&>(*Entity);
					bApples |= Dropped.Stack.Item == TEXT("minecraft:apple") && Dropped.Stack.Count == 3;
					bCart |= Dropped.Stack.Item == TEXT("minecraft:chest_minecart");
				}
			}
			TestTrue(TEXT("breaking it leaves the cart and its things"), bApples && bCart);
			Manager.Tick();
		}

		// A furnace cart takes coal, and with it drives itself along the rail, away from the one who fed it, until it
		// is out of fuel.
		{
			FMcMinecartEntity* Furnace = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Furnace);
			Furnace->Body.Position = FVector(4.5, 1.0625, 26.5);
			Manager.Add(TUniquePtr<FMcEntity>(Furnace));
			TestTrue(TEXT("a stick is no fuel"), Furnace->UseItem(TEXT("minecraft:stick"), FVector(2.5, 1.0, 26.5)) == FMcVehicle::EUse::None);
			for (int32 Tick = 0; Tick < 5; ++Tick)
			{
				Manager.Tick();
			}
			TestTrue(TEXT("unfuelled, it stays put"), FMath::Abs(Furnace->Body.Position.X - 4.5) < 0.01);
			TestTrue(TEXT("coal is used up"), Furnace->UseItem(TEXT("minecraft:coal"), FVector(2.5, 1.0, 26.5)) == FMcVehicle::EUse::Consumed);
			TestEqual(TEXT("for three minutes"), Furnace->Fuel, FMcMinecartEntity::FuelPerItem);
			for (int32 Tick = 0; Tick < 40; ++Tick)
			{
				Manager.Tick();
			}
			TestTrue(TEXT("it drives away from the player"), Furnace->Body.Position.X > 7.0);
			TestTrue(TEXT("but no faster than 0.2 a tick"), Furnace->Body.Velocity.Size() <= FMcMinecartEntity::FurnaceSpeed + 0.001);
			Furnace->Fuel = 1;
			Furnace->Body.Position = FVector(1.5, 1.0625, 26.5);
			Furnace->Body.Velocity = FVector::ZeroVector;
			for (int32 Tick = 0; Tick < 40; ++Tick)
			{
				Manager.Tick();
			}
			TestEqual(TEXT("fuel burns out"), Furnace->Fuel, 0);
			const double Stopped = Furnace->Body.Position.X;
			for (int32 Tick = 0; Tick < 20; ++Tick)
			{
				Manager.Tick();
			}
			TestTrue(TEXT("and then it coasts to a stop"), FMath::Abs(Furnace->Body.Position.X - Stopped) < 0.5);
			Furnace->bRemoved = true;
			Manager.Tick();
		}

		// A TNT cart lit with flint and steel, or by a powered activator rail, goes off after four seconds.
		{
			FMcMinecartEntity* Tnt = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Tnt);
			Tnt->Body.Position = FVector(4.5, 1.0625, -11.5);
			Manager.Add(TUniquePtr<FMcEntity>(Tnt));
			for (int32 Tick = 0; Tick < 10; ++Tick)
			{
				Manager.Tick();
			}
			TestEqual(TEXT("an unlit TNT cart is quiet"), Tnt->Fuse, -1);
			TestTrue(TEXT("flint and steel lights it and is kept"), Tnt->UseItem(TEXT("minecraft:flint_and_steel"), FVector(2.5, 1.0, -11.5)) == FMcVehicle::EUse::Kept);
			TestEqual(TEXT("with a four second fuse"), Tnt->Fuse, FMcMinecartEntity::FuseTicks);
			for (int32 Tick = 0; Tick < FMcMinecartEntity::FuseTicks - 1; ++Tick)
			{
				Manager.Tick();
			}
			TestFalse(TEXT("it has not gone off yet"), Tnt->bRemoved);
			Manager.Tick();
			Manager.Tick();
			TestTrue(TEXT("then it explodes"), Tnt->bRemoved);
			Manager.Tick();

			FMcMinecartEntity* OnActivator = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Tnt);
			OnActivator->Body.Position = FVector(4.5, 1.0625, 28.5);
			Manager.Add(TUniquePtr<FMcEntity>(OnActivator));
			Manager.Tick();
			TestTrue(TEXT("a powered activator rail lights a TNT cart"), OnActivator->Fuse >= 0);
			OnActivator->bRemoved = true;
			Manager.Tick();
		}

		// A hopper cart takes in the items lying in it, unless it is on a powered activator rail.
		{
			FMcMinecartEntity* Hopper = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Hopper);
			Hopper->Body.Position = FVector(4.5, 1.0625, 24.5);
			Manager.Add(TUniquePtr<FMcEntity>(Hopper));
			Manager.DropItem(FVector(4.5, 1.3, 24.5), FMcItemStack(TEXT("minecraft:cobblestone"), 5), 0).Body.Velocity = FVector::ZeroVector;
			FMcMinecartEntity* Locked = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Hopper);
			Locked->Body.Position = FVector(8.5, 1.0625, 28.5);
			Manager.Add(TUniquePtr<FMcEntity>(Locked));
			Manager.DropItem(FVector(8.5, 1.3, 28.5), FMcItemStack(TEXT("minecraft:cobblestone"), 5), 0).Body.Velocity = FVector::ZeroVector;
			for (int32 Tick = 0; Tick < 12; ++Tick)
			{
				Manager.Tick();
			}
			TestEqual(TEXT("a hopper cart picks the stack up"), Hopper->GetInventory()->CountItem(TEXT("minecraft:cobblestone")), 5);
			TestEqual(TEXT("one on a powered activator rail does not"), Locked->GetInventory()->CountItem(TEXT("minecraft:cobblestone")), 0);
			Hopper->bRemoved = true;
			Locked->bRemoved = true;
			Manager.Tick();
		}
	}
	return true;
}

#endif
