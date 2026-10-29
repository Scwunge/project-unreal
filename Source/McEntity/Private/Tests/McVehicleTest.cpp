#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McItemStack.h"
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
	return true;
}

#endif
