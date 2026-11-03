#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McVehicle.h"
#include "Misc/AutomationTest.h"

// Boats against vehicle_log15.txt (a driven boat holding forward, every tick, x of the first moving ticks).
namespace
{
	const double IceRow[] = { 2.5400, 2.6192, 2.7368, 2.8921, 3.0842, 3.3125, 3.5763 };
	const double BlueRow[] = { 2.5400, 2.6196, 2.7382, 2.8956, 3.0913, 3.3248, 3.5957 };
	const double StoneRow[] = { 2.5400, 2.6040, 2.6824, 2.7694, 2.8617, 2.9570 };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBoatLogTest, "McClient.McEntity.BoatLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBoatLogTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };

	const auto Drive = [&](const TCHAR* Name, const TCHAR* Floor, const double* Expected, int32 Count)
	{
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 2; ++Z)
		{
			for (int32 X = -1; X <= 3; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		for (int32 X = 0; X < 40; ++X)
		{
			for (int32 Z = -2; Z <= 2; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), State(Floor));
			}
		}
		FMcEntityManager Manager(World);
		FMcBoatEntity* Boat = new FMcBoatEntity(false);
		Boat->Body.Position = FVector(2.5, 1.0, 0.5);
		Boat->Yaw = -90.0f;
		Boat->Body.bOnGround = true;
		Manager.Add(TUniquePtr<FMcEntity>(Boat));
		for (int32 Tick = 0; Tick < Count; ++Tick)
		{
			Boat->SetRider(1.0f, 0.0f, -90.0f, 7);
			Manager.Tick();
			TestTrue(*FString::Printf(TEXT("%s tick %d x %f vs %f"), Name, Tick + 1, Boat->Body.Position.X, Expected[Tick]),
				FMath::Abs(Boat->Body.Position.X - Expected[Tick]) < 2e-4);
		}
	};
	Drive(TEXT("ice"), TEXT("minecraft:ice"), IceRow, UE_ARRAY_COUNT(IceRow));
	Drive(TEXT("blue ice"), TEXT("minecraft:blue_ice"), BlueRow, UE_ARRAY_COUNT(BlueRow));
	Drive(TEXT("stone"), TEXT("minecraft:stone"), StoneRow, UE_ARRAY_COUNT(StoneRow));

	// A boat at 0.3 into one at rest on ice: shoved apart by pushes of 0.05 (rows 17 to 22 of vehicle_log15.txt).
	{
		const double Hit[] = { 6.773, 6.982, 7.088, 7.143, 7.148, 7.153 };
		const double Rest[] = { 8.500, 8.500, 8.549, 8.695, 8.887, 9.075 };
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 2; ++Z) { for (int32 X = -1; X <= 3; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
		for (int32 X = 0; X < 40; ++X) { for (int32 Z = -2; Z <= 2; ++Z) { World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:ice"))); } }
		FMcEntityManager Manager(World);
		FMcBoatEntity* Standing = new FMcBoatEntity(false);
		Standing->Body.Position = FVector(8.5, 1.0, 0.5);
		Standing->Body.bOnGround = true;
		FMcBoatEntity* Moving = new FMcBoatEntity(false);
		Moving->Body.Position = FVector(2.5, 1.0, 0.5);
		Moving->Body.Velocity = FVector(0.3, 0.0, 0.0);
		Moving->Body.bOnGround = true;
		Manager.Add(TUniquePtr<FMcEntity>(Standing));
		Manager.Add(TUniquePtr<FMcEntity>(Moving));
		for (int32 Tick = 1; Tick <= 22; ++Tick)
		{
			Manager.Tick();
			if (Tick >= 17)
			{
				TestTrue(*FString::Printf(TEXT("hitter tick %d: %f vs %f"), Tick, Moving->Body.Position.X, Hit[Tick - 17]), FMath::Abs(Moving->Body.Position.X - Hit[Tick - 17]) < 2e-3);
				TestTrue(*FString::Printf(TEXT("hit tick %d: %f vs %f"), Tick, Standing->Body.Position.X, Rest[Tick - 17]), FMath::Abs(Standing->Body.Position.X - Rest[Tick - 17]) < 2e-3);
			}
		}
	}
	return true;
}
