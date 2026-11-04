// Leads against vehicle_log22.txt: a tied boat is left alone inside 6 blocks, pulled after the player beyond, the lead snaps past
// 12 and drops; tied to a fence knot it stays; the knot hands back to the player; a hostile mob can't be tied.

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McLeash.h"
#include "McMob.h"
#include "McVehicle.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLeashTest, "McClient.McEntity.Leash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLeashTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 2; ++Z) { for (int32 X = -1; X <= 5; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
	for (int32 X = -16; X < 80; ++X) { for (int32 Z = -2; Z <= 2; ++Z) { World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0)); } }
	FMcEntityManager Manager(World);
	FMcBoatEntity* Boat = new FMcBoatEntity(false);
	Boat->Body.Position = FVector(5.5, 1.0, 0.5);
	Boat->Body.bOnGround = true;
	Manager.Add(TUniquePtr<FMcEntity>(Boat));

	FVector Player(8.5, 1.0, 0.5);
	TestTrue(TEXT("a boat can be tied"), McLeash::Attach(*Boat, Player));
	TestFalse(TEXT("not twice"), McLeash::Attach(*Boat, Player));
	Manager.PlayerPositions = { Player };
	for (int32 Tick = 0; Tick < 10; ++Tick) { Manager.Tick(); }
	TestEqual(TEXT("3 blocks off, a boat is left alone"), Boat->Body.Position.X, 5.5);

	// The player walks off at half a block a tick: the boat is dragged after, never farther than about 7 blocks behind.
	double Largest = 0.0;
	for (int32 Tick = 0; Tick < 120; ++Tick)
	{
		Player.X += 0.544;
		Manager.PlayerPositions = { Player };
		Manager.Tick();
		TestTrue(TEXT("still tied"), Boat->bLeashed);
		Largest = FMath::Max(Largest, Player.X - Boat->Body.Position.X);
	}
	TestTrue(*FString::Printf(TEXT("dragged along, %f behind at most (7.6 in the reference, boat friction differs a little)"), Player.X - Boat->Body.Position.X), Player.X - Boat->Body.Position.X > 6.0 && Player.X - Boat->Body.Position.X < 9.0);

	// Out of reach all at once (a teleport): the lead snaps and drops.
	Player.X = -30.0;
	Manager.PlayerPositions = { Player };
	Manager.Tick();
	TestFalse(TEXT("snapped"), Boat->bLeashed);
	int32 Leads = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		Leads += Entity->GetType() == FMcItemEntity::StaticType() && static_cast<FMcItemEntity&>(*Entity).Stack.Item == TEXT("minecraft:lead") ? 1 : 0;
	}
	TestEqual(TEXT("the lead dropped"), Leads, 1);

	// A knot on a fence post.
	Player = FVector(40.5, 1.0, 0.5);
	Boat->Body.Position = FVector(42.5, 1.0, 0.5);
	Boat->Body.Velocity = FVector::ZeroVector;
	Manager.PlayerPositions = { Player };
	TestTrue(TEXT("tied again"), McLeash::Attach(*Boat, Player));
	TestTrue(TEXT("a fence takes it"), McLeash::UseFence(Manager, FIntVector(44, 1, 0), Player, true));
	Manager.Tick();
	TestTrue(TEXT("on the knot"), Boat->bLeashed && Boat->LeashKnot != 0);
	Player.X = 20.0;
	Manager.PlayerPositions = { Player };
	for (int32 Tick = 0; Tick < 30; ++Tick) { Manager.Tick(); }
	TestTrue(TEXT("the player left, it stays tied to the post"), Boat->bLeashed && Boat->Body.Position.X > 40.0);
	Player = FVector(43.5, 1.0, 0.5);
	Manager.PlayerPositions = { Player };
	TestTrue(TEXT("the knot gives it back"), McLeash::UseFence(Manager, FIntVector(44, 1, 0), Player, false));
	TestTrue(TEXT("to the player"), Boat->bLeashed && Boat->LeashKnot == 0);

	if (const FMcMobType* Zombie = FMcMobType::Find(FName(TEXT("minecraft:zombie"))))
	{
		FMcMob Hostile(*Zombie);
		TestFalse(TEXT("a zombie can't be tied"), McLeash::CanLeash(Hostile));
	}
	return true;
}
