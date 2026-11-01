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
		Boat.SetRider(-1.0f, 0.0f, 0.0f);
		TestEqual(TEXT("back rows neither"), Boat.GetPaddleBits(), 0);
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
			// Full speed from the first tick (vehicle_log3.txt: 3.5, 3.7, 3.9).
			Manager.Tick();
			TestEqual(TEXT("fed, it moves 0.2 straight away"), Furnace->Body.Position.X, 4.7, 1e-6);
			Manager.Tick();
			TestEqual(TEXT("and 0.2 the next tick"), Furnace->Body.Position.X, 4.9, 1e-6);
			for (int32 Tick = 0; Tick < 38; ++Tick)
			{
				Manager.Tick();
			}
			TestTrue(TEXT("it drives away from the player"), Furnace->Body.Position.X > 7.0);
			TestTrue(TEXT("but no faster than 0.2 a tick"), Furnace->Body.Velocity.Size() <= FMcMinecartEntity::FurnaceSpeed + 0.001);
			Furnace->Fuel = 1;
			Furnace->Body.Position = FVector(1.5, 1.0625, 26.5);
			Furnace->Body.Velocity = FVector::ZeroVector;
			// vehicle_log6.txt: the tick the fuel ends it is left going at 2, held to 0.2 a tick until 0.9408 a tick wears
			// it below that.
			Manager.Tick();
			TestEqual(TEXT("fuel burns out"), Furnace->Fuel, 0);
			Manager.Tick();
			TestNearlyEqual(TEXT("out of fuel it still moves 0.2"), Furnace->Body.Position.X, 1.9, 1e-6);
			TestNearlyEqual(TEXT("going at 2 * 0.9408"), Furnace->Body.Velocity.X, 1.8816, 1e-6);
			for (int32 Tick = 2; Tick < 39; ++Tick)
			{
				Manager.Tick();
			}
			TestNearlyEqual(TEXT("for 38 ticks"), Furnace->Body.Position.X, 1.5 + 39 * 0.2, 1e-6);
			Manager.Tick();
			TestNearlyEqual(TEXT("then 0.19676"), Furnace->Body.Position.X, 1.5 + 39 * 0.2 + 0.19676, 1e-5);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVehicleLogTest, "McClient.McEntity.VehicleLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// The numbers vanilla gave in vehicle_log.txt (log sents, one line a tick), its scene rebuilt 62 blocks higher.
bool FMcVehicleLogTest::RunTest(const FString& Parameters)
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
	for (int32 X = 0; X <= 12; ++X)
	{
		World.SetBlock(FIntVector(X, 1, 0), State(TEXT("minecraft:rail[shape=east_west]")));
	}
	// powered_log.txt: powered rails against a block, and unpowered ones.
	World.SetBlock(FIntVector(0, 1, 5), State(TEXT("minecraft:stone")));
	for (int32 X = 1; X <= 10; ++X)
	{
		World.SetBlock(FIntVector(X, 1, 5), State(TEXT("minecraft:powered_rail[shape=east_west,powered=true]")));
		World.SetBlock(FIntVector(X, 1, 8), State(TEXT("minecraft:powered_rail[shape=east_west,powered=false]")));
	}
	// vehicle_log2.txt: a ridden cart and a fuelled furnace cart nobody pushed.
	for (int32 X = 0; X <= 12; ++X)
	{
		World.SetBlock(FIntVector(X, 1, 12), State(TEXT("minecraft:rail[shape=east_west]")));
		World.SetBlock(FIntVector(X, 1, 14), State(TEXT("minecraft:rail[shape=east_west]")));
	}
	// vehicle_log5.txt: a floor of blue ice three wide, and one row of packed ice between stone.
	for (int32 X = 0; X <= 12; ++X)
	{
		for (int32 Z = 16; Z <= 18; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:blue_ice")));
		}
		World.SetBlock(FIntVector(X, 0, 20), State(TEXT("minecraft:packed_ice")));
	}
	// vehicle_log6.txt: slime and soul sand, three wide.
	for (int32 X = 0; X <= 12; ++X)
	{
		for (int32 Z = -4; Z <= -2; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:slime_block")));
			World.SetBlock(FIntVector(X, 0, Z - 4), State(TEXT("minecraft:soul_sand")));
			World.SetBlock(FIntVector(X, 0, Z - 8), State(TEXT("minecraft:honey_block")));
		}
		// vehicle_log9.txt: a row of ice each side of an empty one.
		World.SetBlock(FIntVector(X, 0, -16), State(TEXT("minecraft:ice")));
		World.SetBlock(FIntVector(X, 0, -15), State(TEXT("minecraft:air")));
		World.SetBlock(FIntVector(X, 0, -14), State(TEXT("minecraft:ice")));
	}
	// A slope rising east, a block a block.
	for (int32 X = 0; X <= 5; ++X)
	{
		World.SetBlock(FIntVector(X, 1 + X, 25), State(TEXT("minecraft:rail[shape=ascending_east]")));
	}
	for (int32 X = 14; X <= 22; ++X)
	{
		for (int32 Z = 22; Z <= 30; ++Z)
		{
			World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:water")));
		}
	}
	FMcEntityManager Manager(World);

	FMcMinecartEntity* Flat = new FMcMinecartEntity();
	Flat->Body.Position = FVector(2.5, 1.0625, 0.5);
	Flat->Body.Velocity = FVector(0.4, 0.0, 0.0);
	Manager.Add(TUniquePtr<FMcEntity>(Flat));
	FMcMinecartEntity* Slope = new FMcMinecartEntity();
	Slope->Body.Position = FVector(3.5, 4.5625, 25.5);
	Manager.Add(TUniquePtr<FMcEntity>(Slope));
	FMcMinecartEntity* Kicked = new FMcMinecartEntity();
	Kicked->Body.Position = FVector(1.5, 1.0625, 5.5);
	Manager.Add(TUniquePtr<FMcEntity>(Kicked));
	FMcMinecartEntity* Braked = new FMcMinecartEntity();
	Braked->Body.Position = FVector(5.5, 1.0625, 8.5);
	Braked->Body.Velocity = FVector(0.15612, 0.0, 0.0);
	Manager.Add(TUniquePtr<FMcEntity>(Braked));
	FMcMinecartEntity* Ridden = new FMcMinecartEntity();
	Ridden->Body.Position = FVector(2.5, 1.0625, 12.5);
	Ridden->Body.Velocity = FVector(0.4, 0.0, 0.0);
	Ridden->SetRider(0.0f, 0.0f, 0.0f);
	Manager.Add(TUniquePtr<FMcEntity>(Ridden));
	FMcMinecartEntity* Furnace = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Furnace);
	Furnace->Body.Position = FVector(2.5, 1.0625, 14.5);
	Furnace->Body.Velocity = FVector(0.1, 0.0, 0.0);
	Furnace->Fuel = 300;
	Manager.Add(TUniquePtr<FMcEntity>(Furnace));
	FMcBoatEntity* Stone = new FMcBoatEntity();
	Stone->Body.Position = FVector(2.5, 1.0, 28.5);
	Stone->Body.Velocity = FVector(0.3, 0.0, 0.0);
	Stone->Body.bOnGround = true;
	Manager.Add(TUniquePtr<FMcEntity>(Stone));
	FMcBoatEntity* BlueIce = new FMcBoatEntity();
	BlueIce->Body.Position = FVector(2.5, 1.0, 17.5);
	BlueIce->Body.Velocity = FVector(0.5, 0.0, 0.0);
	BlueIce->Body.bOnGround = true;
	Manager.Add(TUniquePtr<FMcEntity>(BlueIce));
	FMcBoatEntity* Mixed = new FMcBoatEntity();
	Mixed->Body.Position = FVector(2.5, 1.0, 20.5);
	Mixed->Body.Velocity = FVector(0.5, 0.0, 0.0);
	Mixed->Body.bOnGround = true;
	Manager.Add(TUniquePtr<FMcEntity>(Mixed));
	FMcBoatEntity* Slime = new FMcBoatEntity();
	Slime->Body.Position = FVector(2.5, 1.0, -2.5);
	Slime->Body.Velocity = FVector(0.5, 0.0, 0.0);
	Slime->Body.bOnGround = true;
	Manager.Add(TUniquePtr<FMcEntity>(Slime));
	// Summoned a block up, it drops the eighth of a block onto the soul sand.
	FMcBoatEntity* SoulSand = new FMcBoatEntity();
	SoulSand->Body.Position = FVector(2.5, 1.0, -6.5);
	SoulSand->Body.Velocity = FVector(0.5, 0.0, 0.0);
	Manager.Add(TUniquePtr<FMcEntity>(SoulSand));
	// vehicle_log9.txt: summoned a block up, it drops the sixteenth of a block onto honey.
	FMcBoatEntity* Honey = new FMcBoatEntity();
	Honey->Body.Position = FVector(2.5, 1.0, -10.5);
	Honey->Body.Velocity = FVector(0.5, 0.0, 0.0);
	Manager.Add(TUniquePtr<FMcEntity>(Honey));
	FMcBoatEntity* Trench = new FMcBoatEntity();
	Trench->Body.Position = FVector(2.5, 1.0, -14.5);
	Trench->Body.Velocity = FVector(0.5, 0.0, 0.0);
	Trench->Body.bOnGround = true;
	Manager.Add(TUniquePtr<FMcEntity>(Trench));
	// Vanilla's floating boat as its first logged tick left it.
	FMcBoatEntity* Float = new FMcBoatEntity();
	Float->Body.Position = FVector(14.95, -60.58989 + 62.0, 23.5);
	Float->Body.Velocity = FVector(0.45, 0.010114, 0.0);
	Manager.Add(TUniquePtr<FMcEntity>(Float));
	FMcBoatEntity* Drop = new FMcBoatEntity();
	Drop->Body.Position = FVector(21.5, 5.0, 29.5);
	Manager.Add(TUniquePtr<FMcEntity>(Drop));

	Manager.Tick();
	TestNearlyEqual(TEXT("a cart at 0.4 on flat rail moves all of it first"), Flat->Body.Position.X, 2.9, 1e-4);
	TestNearlyEqual(TEXT("then keeps 0.96 of it"), Flat->Body.Velocity.X, 0.384, 1e-4);
	TestNearlyEqual(TEXT("a cart at rest on a slope moves 0.0078125 down it"), Slope->Body.Position.X, 3.5 - 0.0078125, 1e-5);
	TestNearlyEqual(TEXT("and the same down"), Slope->Body.Position.Y, 4.5625 - 0.0078125, 1e-5);
	TestNearlyEqual(TEXT("going 1.01 times that after"), Slope->Body.Velocity.X, -0.007891, 2e-6);
	TestNearlyEqual(TEXT("a cart at rest on powered rail against a block is pushed off at 0.02"), Kicked->Body.Velocity.X, 0.02, 1e-6);
	TestNearlyEqual(TEXT("not moving yet"), Kicked->Body.Position.X, 1.5, 1e-6);
	TestNearlyEqual(TEXT("an unpowered powered rail halves the move"), Braked->Body.Position.X, 5.5 + 0.07806, 1e-5);
	TestNearlyEqual(TEXT("and friction takes its share after"), Braked->Body.Velocity.X, 0.07494, 1e-5);
	TestNearlyEqual(TEXT("a ridden cart at 0.4 moves 0.3"), Ridden->Body.Position.X, 2.8, 1e-5);
	TestNearlyEqual(TEXT("and keeps 0.997"), Ridden->Body.Velocity.X, 0.3988, 1e-5);
	TestNearlyEqual(TEXT("a furnace cart with fuel and no push only coasts"), Furnace->Body.Position.X, 2.6, 1e-5);
	TestNearlyEqual(TEXT("keeping 0.9408"), Furnace->Body.Velocity.X, 0.09408, 1e-5);
	TestNearlyEqual(TEXT("a boat on stone keeps 0.6 of its speed"), Stone->Body.Velocity.X, 0.18, 1e-6);
	TestNearlyEqual(TEXT("and moves by what it kept"), Stone->Body.Position.X, 2.68, 1e-6);
	TestNearlyEqual(TEXT("a boat on blue ice keeps 0.989"), BlueIce->Body.Position.X, 2.9945, 1e-5);
	TestNearlyEqual(TEXT("one over a row of packed ice between stone keeps the mean of the three"), Mixed->Body.Position.X, 2.86333, 1e-5);
	TestNearlyEqual(TEXT("a boat on water keeps 0.9"), Float->Body.Velocity.X, 0.405, 1e-6);
	TestNearlyEqual(TEXT("and bobs up as vanilla's did"), Float->Body.Velocity.Y, 0.01687, 2e-6);
	TestNearlyEqual(TEXT("moving by it"), Float->Body.Position.Y, -60.57302 + 62.0, 2e-5);
	TestNearlyEqual(TEXT("a boat in the air falls 0.04 a tick"), Drop->Body.Position.Y, 4.96, 1e-6);
	TestNearlyEqual(TEXT("a boat on slime keeps 0.8 before it moves"), Slime->Body.Position.X, 2.9, 1e-6);
	TestNearlyEqual(TEXT("and 0.16 of that after"), Slime->Body.Velocity.X, 0.064, 1e-6);
	TestNearlyEqual(TEXT("one dropping onto soul sand keeps 0.9 before it moves"), SoulSand->Body.Position.X, 2.95, 1e-6);
	TestNearlyEqual(TEXT("and 0.4 of that after"), SoulSand->Body.Velocity.X, 0.18, 1e-6);
	TestNearlyEqual(TEXT("one dropping onto honey keeps 0.9 before it moves"), Honey->Body.Position.X, 2.95, 1e-6);
	TestNearlyEqual(TEXT("and 0.4 of that after, as on soul sand"), Honey->Body.Velocity.X, 0.18, 1e-6);
	TestNearlyEqual(TEXT("one over ice, an empty row and ice keeps the ice's 0.98: the empty cells don't count"), Trench->Body.Position.X, 2.99, 1e-6);

	Manager.Tick();
	TestNearlyEqual(TEXT("the flat cart's second tick"), Flat->Body.Position.X, 3.284, 1e-4);
	TestNearlyEqual(TEXT("and speed"), Flat->Body.Velocity.X, 0.36864, 1e-4);
	TestNearlyEqual(TEXT("the slope cart's second"), Slope->Body.Position.X, 3.5 - 0.0078125 - 0.0157035, 2e-5);
	TestNearlyEqual(TEXT("and speed"), Slope->Body.Velocity.X, -0.01586, 2e-5);
	TestNearlyEqual(TEXT("the kicked cart moves its 0.02"), Kicked->Body.Position.X, 1.52, 1e-6);
	TestNearlyEqual(TEXT("and the rail adds 0.06 to what friction leaves"), Kicked->Body.Velocity.X, 0.0792, 1e-6);
	TestNearlyEqual(TEXT("the ridden cart's second"), Ridden->Body.Position.X, 3.0991, 1e-4);
	TestNearlyEqual(TEXT("the furnace cart's second"), Furnace->Body.Velocity.X, 0.08851, 1e-5);
	TestNearlyEqual(TEXT("the stone boat's second"), Stone->Body.Velocity.X, 0.108, 1e-6);
	TestNearlyEqual(TEXT("the floating boat's second"), Float->Body.Velocity.Y, 0.020552, 2e-6);
	TestNearlyEqual(TEXT("the falling boat, with no drag"), Drop->Body.Position.Y, 4.88, 1e-6);
	TestNearlyEqual(TEXT("the slime boat's second"), Slime->Body.Position.X, 2.9512, 1e-6);
	TestNearlyEqual(TEXT("the soul sand boat's second"), SoulSand->Body.Position.X, 3.112, 1e-6);
	TestNearlyEqual(TEXT("the honey boat's second, landing"), Honey->Body.Position.X, 3.112, 1e-6);
	TestNearlyEqual(TEXT("and speed"), Honey->Body.Velocity.X, 0.0648, 1e-6);
	TestNearlyEqual(TEXT("the trench boat's second"), Trench->Body.Position.X, 3.4702, 1e-6);
	TestNearlyEqual(TEXT("still falling"), SoulSand->Body.Position.Y, 0.88, 1e-6);

	Manager.Tick();
	TestNearlyEqual(TEXT("the kicked cart's third"), Kicked->Body.Velocity.X, 0.13603, 1e-5);
	TestNearlyEqual(TEXT("on honey, stone's 0.6 before the move"), Honey->Body.Position.X, 3.15088, 1e-6);
	TestNearlyEqual(TEXT("and 0.4 after"), Honey->Body.Velocity.X, 0.015552, 1e-6);
	Manager.Tick();
	TestNearlyEqual(TEXT("a cart braked under 0.03 stops dead"), Braked->Body.Velocity.X, 0.0, 1e-9);
	TestNearlyEqual(TEXT("where vanilla's did"), Braked->Body.Position.X, 5.5 + 0.07806 + 0.03747 + 0.01798, 1e-4);
	for (int32 Tick = 4; Tick < 12; ++Tick)
	{
		Manager.Tick();
	}
	TestNearlyEqual(TEXT("the dropped boat is just in the water after 12 ticks"), Drop->Body.Position.Y, -60.12 + 62.0, 1e-4);
	TestNearlyEqual(TEXT("still falling"), Drop->Body.Velocity.Y, -0.48, 1e-6);
	Manager.Tick();
	TestNearlyEqual(TEXT("then sinks in and stops"), Drop->Body.Velocity.Y, 0.0, 1e-9);
	TestNearlyEqual(TEXT("where vanilla's did, whatever the fall"), Drop->Body.Position.Y, -60.4615 + 62.0, 1e-4);
	for (int32 Tick = 13; Tick < 120; ++Tick)
	{
		Manager.Tick();
	}
	TestNearlyEqual(TEXT("a boat settles 0.65 of its height under the surface"), Float->Body.Position.Y, -60.47674 + 62.0, 1e-4);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCartPushLogTest, "McClient.McEntity.CartPushLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// The numbers vanilla gave in vehicle_log7.txt and vehicle_log8.txt (log sents, one line a tick): carts into carts, mobs
// and furnace carts, chest and hopper carts, a TNT cart's fuse. Each scene starts from a tick vanilla logged, 62 blocks
// higher and moved to its own rail; the carts are added in vanilla's entity order, which is the order they tick in.
bool FMcCartPushLogTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 3; ++Z)
	{
		for (int32 X = -1; X <= 2; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	for (int32 X = -2; X <= 40; ++X)
	{
		for (int32 Z = 0; Z <= 54; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
		}
		for (int32 Z = 0; Z <= 48; Z += 4)
		{
			World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:rail[shape=east_west]")));
		}
		World.SetBlock(FIntVector(X, 1, 49), State(TEXT("minecraft:rail[shape=east_west]")));
		World.SetBlock(FIntVector(X, 1, 52), State(TEXT("minecraft:rail[shape=east_west]")));
	}
	World.SetBlock(FIntVector(10, 1, 44), State(TEXT("minecraft:activator_rail[powered=true,shape=east_west]")));
	FMcEntityManager Manager(World);
	using EVariant = FMcMinecartEntity::EVariant;
	const auto Cart = [&](EVariant Variant, double X, int32 Z, double Speed)
	{
		FMcMinecartEntity* New = new FMcMinecartEntity(Variant);
		New->Body.Position = FVector(X, 1.0625, Z + 0.5);
		New->Body.Velocity = FVector(Speed, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(New));
		return New;
	};
	const auto Mob = [&](const TCHAR* Type, double X, int32 Z)
	{
		FMcMob* New = new FMcMob(*FMcMobType::Find(Type));
		New->Body.Position = FVector(X, 1.0, Z + 0.5);
		New->Body.bOnGround = true;
		Manager.Add(TUniquePtr<FMcEntity>(New));
		return New;
	};

	// vehicle_log7.txt, ticks 424413 on: a cart at rest (id 272) and one running into it (273).
	FMcMinecartEntity* Still = Cart(EVariant::Plain, 8.5, 0, 0.0);
	FMcMinecartEntity* Runner = Cart(EVariant::Plain, 7.29597, 0, 0.208161);
	// Ticks 424428 on: two head on.
	FMcMinecartEntity* East = Cart(EVariant::Plain, 7.88422, 4, 0.084631);
	FMcMinecartEntity* West = Cart(EVariant::Plain, 9.11578, 4, -0.084631);
	// Ticks 424544 on: a cart at rest (313) and a furnace cart driving into it (314).
	FMcMinecartEntity* Ahead = Cart(EVariant::Plain, 10.5, 8, 0.0);
	FMcMinecartEntity* Driving = Cart(EVariant::Furnace, 9.3, 8, FMcMinecartEntity::FurnaceSpeed);
	Driving->Fuel = 1000;
	Driving->PushDirection = FVector2D(1.0, 0.0);
	// vehicle_log8.txt, ticks 425388 on: a cart (114) into an unfuelled furnace cart at rest (115).
	FMcMinecartEntity* Bouncer = Cart(EVariant::Plain, 7.29597, 12, 0.208161);
	FMcMinecartEntity* Cold = Cart(EVariant::Furnace, 8.5, 12, 0.0);
	// Ticks 425387 on: an iron golem (105) and a cart running into it (106). There is no iron golem here, so a cow stands
	// in for it, 1.1 ahead (as the golem was more than a block ahead, and within reach), and a TNT cart, which takes nothing
	// in and keeps the same 0.96, for the cart.
	FMcMob* Golem = Mob(TEXT("minecraft:cow"), 8.4, 16);
	FMcMinecartEntity* GolemCart = Cart(EVariant::Tnt, 7.07914, 16, 0.216835);
	// Ticks 425388 on: a cart with someone in it (108) running into a cow (107), which here ticks after it, as vanilla's
	// never moved.
	FMcMinecartEntity* Full2 = Cart(EVariant::Plain, 7.19350, 52, 0.381226);
	Full2->SetRider(0.0f, 0.0f, 0.0f);
	FMcMob* Blocker = Mob(TEXT("minecraft:cow"), 8.5, 52);
	// Ticks 425373 on: a cart at rest (110) with a cow standing 0.7 into it (111).
	FMcMinecartEntity* Stood = Cart(EVariant::Plain, 8.5, 20, 0.0);
	FMcMob* Cow = Mob(TEXT("minecraft:cow"), 9.2, 20);
	// vehicle_log7.txt, ticks 424764 on: chest carts from 0.4, empty, with a stack of stone and full.
	FMcMinecartEntity* Empty = Cart(EVariant::Chest, 2.5, 24, 0.4);
	FMcMinecartEntity* OneStack = Cart(EVariant::Chest, 2.5, 28, 0.4);
	OneStack->GetInventory()->Set(0, FMcItemStack(TEXT("minecraft:stone"), 64));
	FMcMinecartEntity* Full = Cart(EVariant::Chest, 2.5, 32, 0.4);
	for (int32 Slot = 0; Slot < 27; ++Slot)
	{
		Full->GetInventory()->Set(Slot, FMcItemStack(TEXT("minecraft:stone"), 64));
	}
	// vehicle_log8.txt: an empty hopper cart from 0.4, and ones at rest with an item 1.6 or 2.0 over them, or 0.6 or 0.8 aside.
	FMcMinecartEntity* Hopper = Cart(EVariant::Hopper, 2.5, 36, 0.4);
	FMcMinecartEntity* Picker = Cart(EVariant::Hopper, 20.5, 40, 0.0);
	FMcMinecartEntity* High = Cart(EVariant::Hopper, 24.5, 40, 0.0);
	FMcMinecartEntity* Side = Cart(EVariant::Hopper, 28.5, 40, 0.0);
	FMcMinecartEntity* Far = Cart(EVariant::Hopper, 32.5, 40, 0.0);
	const auto Float = [&](double X, double Up)
	{
		Manager.DropItem(FVector(X, 1.0625 + Up, 40.5), FMcItemStack(TEXT("minecraft:cobblestone"), 1), 0).Body.Velocity = FVector::ZeroVector;
	};
	Float(20.5, 1.6);
	Float(24.5, 2.0);
	Float(29.1, 0.8);
	Float(33.3, 0.8);
	// Tick 425383 on: a TNT cart coming up to a powered activator rail.
	FMcMinecartEntity* Tnt = Cart(EVariant::Tnt, 9.85167, 44, 0.265933);
	// Ticks 425388 on: a cart passing one at rest on the next rail over.
	FMcMinecartEntity* Passing = Cart(EVariant::Plain, 7.29597, 48, 0.208161);
	FMcMinecartEntity* Beside = Cart(EVariant::Plain, 8.5, 49, 0.0);

	Manager.Tick();
	TestNearlyEqual(TEXT("a cart running into one at rest keeps a fifth and half the two together, less the shove"), Runner->Body.Velocity.X, 0.089884, 1e-5);
	TestNearlyEqual(TEXT("having moved first"), Runner->Body.Position.X, 7.50413, 1e-5);
	TestNearlyEqual(TEXT("and the one at rest gets the other half and the shove"), Still->Body.Velocity.X, 0.149917, 1e-5);
	TestNearlyEqual(TEXT("head on, the first to tick bounces back"), East->Body.Velocity.X, -0.051004, 1e-5);
	TestNearlyEqual(TEXT("the second moved by what the first left it"), West->Body.Position.X, 9.14075, 1e-5);
	TestNearlyEqual(TEXT("and goes back the other way"), West->Body.Velocity.X, 0.044931, 1e-5);
	TestNearlyEqual(TEXT("a driving furnace cart throws the cart ahead at its own speed and the shove"), Ahead->Body.Velocity.X, 3.505616, 1e-5);
	TestNearlyEqual(TEXT("moving its 0.2 still"), Driving->Body.Position.X, 9.5, 1e-6);
	TestNearlyEqual(TEXT("a cart into a furnace cart at rest bounces off it"), Bouncer->Body.Velocity.X, -0.052007, 1e-5);
	TestNearlyEqual(TEXT("which doesn't move"), Cold->Body.Velocity.X, 0.0, 1e-9);
	TestNearlyEqual(TEXT("a cart at 0.2 reaching a mob it can't take in is shoved back 0.05"), GolemCart->Body.Velocity.X, 0.158161, 1e-5);
	TestNearlyEqual(TEXT("and the mob 0.05 on"), Golem->Body.Velocity.X, 0.05, 1e-5);
	TestNearlyEqual(TEXT("a cart with someone in it reaching a cow moves three quarters"), Full2->Body.Position.X, 7.47942, 1e-5);
	TestNearlyEqual(TEXT("and isn't shoved back"), Full2->Body.Velocity.X, 0.380082, 1e-5);
	TestNearlyEqual(TEXT("a cow standing in a cart at rest shoves it off at 0.05"), Stood->Body.Velocity.X, -0.05, 1e-6);
	TestNearlyEqual(TEXT("an empty chest cart keeps 0.995"), Empty->Body.Velocity.X, 0.398, 1e-6);
	TestNearlyEqual(TEXT("with a stack in it 0.994"), OneStack->Body.Velocity.X, 0.3976, 1e-6);
	TestNearlyEqual(TEXT("full 0.98"), Full->Body.Velocity.X, 0.392, 1e-6);
	TestNearlyEqual(TEXT("and an empty hopper cart 0.995"), Hopper->Body.Velocity.X, 0.398, 1e-6);
	TestEqual(TEXT("a hopper cart takes in an item floating 1.6 over it"), Picker->GetInventory()->CountItem(TEXT("minecraft:cobblestone")), 1);
	TestEqual(TEXT("but not one 2.0 over"), High->GetInventory()->CountItem(TEXT("minecraft:cobblestone")), 0);
	TestEqual(TEXT("one 0.6 to the side"), Side->GetInventory()->CountItem(TEXT("minecraft:cobblestone")), 1);
	TestEqual(TEXT("but not 0.8"), Far->GetInventory()->CountItem(TEXT("minecraft:cobblestone")), 0);
	TestEqual(TEXT("the TNT cart isn't on the activator rail yet"), Tnt->Fuse, -1);
	TestNearlyEqual(TEXT("a cart on the next rail over isn't shoved"), Beside->Body.Velocity.Size(), 0.0, 1e-9);
	TestNearlyEqual(TEXT("nor shoves"), Passing->Body.Velocity.X, 0.199835, 1e-6);

	Manager.Tick();
	TestNearlyEqual(TEXT("the cart that was at rest moves by what it was given"), Still->Body.Position.X, 8.64992, 1e-5);
	TestNearlyEqual(TEXT("and shoves the other as it goes, and is shoved back"), Still->Body.Velocity.X, 0.223737, 1e-5);
	TestNearlyEqual(TEXT("the runner, shoved before it moved"), Runner->Body.Position.X, 7.59537, 1e-5);
	TestNearlyEqual(TEXT("and after"), Runner->Body.Velocity.X, 0.108563, 1e-5);
	TestNearlyEqual(TEXT("the thrown cart is held to 2 and moves 0.4"), Ahead->Body.Position.X, 10.9, 1e-6);
	TestNearlyEqual(TEXT("keeping 0.96 of 2"), Ahead->Body.Velocity.X, 1.92, 1e-6);
	TestNearlyEqual(TEXT("the cart off the furnace cart, shoved again by it standing"), Bouncer->Body.Velocity.X, -0.059256, 1e-5);
	TestNearlyEqual(TEXT("the cow shoves the cart standing in it again"), Stood->Body.Velocity.X, -0.098, 1e-3);
	TestEqual(TEXT("which is too slow to take it in"), Cow->RidingId, 0);
	TestTrue(TEXT("the TNT cart on the activator rail is lit"), Tnt->Fuse >= 0);
	TestNearlyEqual(TEXT("a cart with someone in it stops dead where it meets the cow"), Full2->Body.Velocity.X, 0.0, 1e-9);
	// Against the cow's box, which has moved on a little since, ticking after it.
	TestTrue(TEXT("its box against the cow's"), Full2->Body.GetBox().Max.X <= Blocker->Body.GetBox().Min.X + 1e-6
		&& Full2->Body.GetBox().Max.X > Blocker->Body.GetBox().Min.X - 0.1);
	const int32 LitAt = 2;
	Manager.Tick();
	TestEqual(TEXT("still too slow at 0.098"), Cow->RidingId, 0);
	Manager.Tick();
	TestEqual(TEXT("at 0.144 the cart takes the cow in"), Cow->RidingId, Stood->Id);

	int32 Gone = 0;
	for (int32 Tick = 5; Tick <= 90 && Gone == 0; ++Tick)
	{
		Manager.Tick();
		Gone = Tnt->bRemoved ? Tick : 0;
	}
	TestEqual(TEXT("the TNT cart goes off 81 ticks after the one it was lit in"), Gone - LitAt, 81);
	return true;
}

#endif
