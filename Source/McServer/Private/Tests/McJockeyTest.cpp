#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJockeyTest, "McClient.McServer.MobJockeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	void FlatWorld(FMcBlockWorld& World)
	{
		FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		for (int32 X = -16; X < 32; ++X)
		{
			for (int32 Z = -16; Z < 32; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
			}
		}
	}

	// A rider on a mount, as the game's finalizeSpawn leaves them.
	void Seat(FMcMob& Rider, FMcMob& Mount)
	{
		Rider.RidingId = Mount.Id;
		Mount.PassengerId = Rider.Id;
		Rider.Body.Position = Mount.Body.Position + Mount.GetSeatOffset(Rider);
	}
}

bool FMcJockeyTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	// The seats, as logged in the reference (jockey_chicken.txt, jockey_spider.txt): at rest a baby zombie sits 0.513 above its
	// chicken and 0.1 behind its middle, a skeleton 0.065 above its spider.
	{
		FMcBlockWorld World{FMcDimensionType()};
		FlatWorld(World);
		FMcEntityHost Host(World, 1);
		FMcMob* Chicken = Host.SpawnMob(TEXT("minecraft:chicken"), FVector(8.5, 1.0, 8.5));
		FMcMob* Zombie = Host.SpawnMob(TEXT("minecraft:zombie"), FVector(8.5, 1.0, 8.5));
		FMcMob* Spider = Host.SpawnMob(TEXT("minecraft:spider"), FVector(-8.5, 1.0, -8.5));
		FMcMob* Skeleton = Host.SpawnMob(TEXT("minecraft:skeleton"), FVector(-8.5, 1.0, -8.5));
		if (!TestTrue(TEXT("the mobs spawn"), Chicken && Zombie && Spider && Skeleton))
		{
			return false;
		}
		Zombie->SetYoung(FMcMob::BabyForever);
		Seat(*Zombie, *Chicken);
		Seat(*Skeleton, *Spider);
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			Host.GetManager().Tick();
		}
		const FVector Chicken2Zombie = Zombie->Body.Position - Chicken->Body.Position;
		TestTrue(*FString::Printf(TEXT("a baby zombie sits 0.513 up and 0.1 behind a chicken (%s)"), *Chicken2Zombie.ToString()),
			FMath::Abs(Chicken2Zombie.Y - 0.513) < 0.01 && FMath::Abs(FVector2D(Chicken2Zombie.X, Chicken2Zombie.Z).Size() - 0.1) < 0.01);
		TestTrue(TEXT("a skeleton sits 0.065 above a spider"), FMath::Abs((Skeleton->Body.Position - Spider->Body.Position).Y - 0.065) < 0.01);
		TestTrue(TEXT("a baby zombie stays a baby"), Zombie->IsYoung());
	}
	// The pair walks to a player the rider's way at the mount's speed: 2.75 blocks a second in the log (a chicken's 0.25).
	{
		FMcBlockWorld World{FMcDimensionType()};
		FlatWorld(World);
		FMcEntityHost Host(World, 1);
		FMcEntityManager& Manager = Host.GetManager();
		Manager.bDaylight = false;
		Manager.PlayerPositions = { FVector(8.5, 1.0, 18.5) };
		FMcMob* Chicken = Host.SpawnMob(TEXT("minecraft:chicken"), FVector(8.5, 1.0, -8.5));
		FMcMob* Zombie = Host.SpawnMob(TEXT("minecraft:zombie"), FVector(8.5, 1.0, -8.5));
		Zombie->SetYoung(FMcMob::BabyForever);
		Seat(*Zombie, *Chicken);
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			Manager.Tick();
		}
		const FVector Start = Chicken->Body.Position;
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Manager.Tick();
		}
		const double PerSecond = (Chicken->Body.Position - Start).Size() / 3.0;
		TestTrue(*FString::Printf(TEXT("a chicken jockey goes for the player at 2.75 blocks a second (%.2f)"), PerSecond), FMath::Abs(PerSecond - 2.75) < 0.25);
		TestTrue(TEXT("toward the player, not off to the side"), Chicken->Body.Position.Z > Start.Z + 4.0 && FMath::Abs(Chicken->Body.Position.X - 8.5) < 1.0);
		TestTrue(*FString::Printf(TEXT("the rider goes with it (%s vs %s)"), *Zombie->Body.Position.ToString(), *Chicken->Body.Position.ToString()), FVector2D::Distance(FVector2D(Zombie->Body.Position.X, Zombie->Body.Position.Z), FVector2D(Chicken->Body.Position.X, Chicken->Body.Position.Z)) < 0.2);

		// Save and load: one record, the rider inside it, and the link comes back.
		const TArray<uint8> Bytes = Host.CaptureChunk(FIntPoint(FMath::FloorToInt(Chicken->Body.Position.X / 16.0), FMath::FloorToInt(Chicken->Body.Position.Z / 16.0)));
		int32 Count = 0;
		FMemoryReaderView Reader(Bytes);
		Reader << Count;
		TestEqual(TEXT("the pair is one record in the chunk's save"), Count, 1);
		FMcEntityHost Back(World, 1);
		TestEqual(TEXT("the loaded save has the pair"), Back.RestoreChunk(Bytes), 1);
		int32 Mobs = 0;
		FMcMob* LoadedChicken = nullptr;
		FMcMob* LoadedZombie = nullptr;
		for (const TUniquePtr<FMcEntity>& Entity : Back.GetManager().GetEntities())
		{
			if (Entity->IsLiving() && !Entity->bRemoved)
			{
				++Mobs;
				FMcMob* Mob = static_cast<FMcMob*>(Entity.Get());
				(Mob->Type.Name == FName(TEXT("minecraft:chicken")) ? LoadedChicken : LoadedZombie) = Mob;
			}
		}
		TestEqual(TEXT("two mobs came back"), Mobs, 2);
		if (LoadedChicken && LoadedZombie)
		{
			TestTrue(TEXT("the zombie rides the chicken again"), LoadedZombie->RidingId == LoadedChicken->Id && LoadedChicken->PassengerId == LoadedZombie->Id);
			TestTrue(TEXT("and is still a baby"), LoadedZombie->IsYoung());
			TestTrue(TEXT("at the same place"), FVector::Dist(LoadedChicken->Body.Position, Chicken->Body.Position) < 0.01);
		}
		// Whoever sits on a mob that goes is free of it.
		Chicken->bRemoved = true;
		Manager.Tick();
		TestEqual(TEXT("a rider whose mount is gone rides nothing"), Zombie->RidingId, 0);
	}
	return true;
}

#endif
