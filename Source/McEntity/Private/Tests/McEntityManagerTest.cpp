#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McPot.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEntityManagerTest, "McClient.McEntity.Manager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcEntityManagerTest::RunTest(const FString& Parameters)
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
	const auto DistXZ = [](const FVector& A, const FVector& B) { return FMath::Sqrt(FMath::Square(A.X - B.X) + FMath::Square(A.Z - B.Z)); };
	FMcEntityManager Manager(World);
	FMcItemStack Diamond;
	Diamond.Item = TEXT("minecraft:diamond");
	Diamond.Count = 3;
	const FBox PlayerBox(FVector(0.2, 1.0, 0.2), FVector(0.8, 2.8, 0.8));

	{
		FMcItemEntity& Item = Manager.DropItem(FVector(0.5, 5.0, 0.5), Diamond);
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a dropped item lands on the floor"), Item.Body.bOnGround);
		TestTrue(TEXT("and rests on top of it"), FMath::Abs(Item.Body.Position.Y - 1.0) < 0.01);
		TestTrue(TEXT("having slid to a stop"), FMath::Abs(Item.Body.Velocity.X) < 0.01);
		TestEqual(TEXT("a player on top of it takes it"), Manager.CollectItems(PlayerBox).Num(), 1);
		Manager.Tick();
		TestEqual(TEXT("the item entity is gone"), Manager.GetEntities().Num(), 0);
	}
	{
		// Pickup delay holds the item back.
		Manager.DropItem(FVector(0.5, 1.0, 0.5), Diamond, 40);
		TestEqual(TEXT("not yet"), Manager.CollectItems(PlayerBox).Num(), 0);
		for (int32 Tick = 0; Tick < 41; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("after its delay"), Manager.CollectItems(PlayerBox).Num(), 1);
		Manager.Tick();
	}
	{
		// Two stacks of one item that land close together become one, and it despawns after five minutes.
		// Set down at rest: with the usual random push they scatter before landing, and only stacks lying on the
		// ground look for a partner.
		Manager.DropItem(FVector(5.5, 1.0, 5.5), Diamond).Body.Velocity = FVector::ZeroVector;
		Manager.DropItem(FVector(5.6, 1.0, 5.5), Diamond).Body.Velocity = FVector::ZeroVector;
		// Resting stacks look for a partner every ten ticks.
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Manager.Tick();
		}
		if (TestEqual(TEXT("two stacks merge"), Manager.GetEntities().Num(), 1))
		{
			TestEqual(TEXT("into one of six"), static_cast<FMcItemEntity*>(Manager.GetEntities()[0].Get())->Stack.Count, 6);
		}
		for (int32 Tick = 0; Tick < FMcItemEntity::LifeTicks && !Manager.GetEntities().IsEmpty(); ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("items despawn after five minutes"), Manager.GetEntities().Num(), 0);
	}
	{
		// Items in water float; in lava they burn.
		for (int32 X = 7; X <= 13; ++X)
		{
			for (int32 Z = 7; Z <= 13; ++Z)
			{
				if (X != 12 || Z != 10)
				{
					World.SetBlock(FIntVector(X, 1, Z), State(TEXT("minecraft:water[level=0]")));
					World.SetBlock(FIntVector(X, 2, Z), State(TEXT("minecraft:water[level=0]")));
				}
			}
		}
		World.SetBlock(FIntVector(12, 1, 10), State(TEXT("minecraft:lava[level=0]")));
		FMcItemEntity& Floating = Manager.DropItem(FVector(10.5, 1.2, 10.5), Diamond);
		Manager.DropItem(FVector(12.5, 1.2, 10.5), Diamond);
		// A dropped item is tossed up out of the water and sinks back in; the buoyancy of 0.0005 a tick takes a few hundred
		// ticks to turn that round (vanilla's rule: only added below 0.06, no cap).
		for (int32 Tick = 0; Tick < 500; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("the lava item burns"), Manager.GetEntities().Num(), 1);
		TestTrue(TEXT("the water item floats near the top"), Floating.Body.Position.Y > 1.8);
	}
	{
		// Mobs: a pig wanders on the floor, a zombie walks up to a player and bites, damage kills.
		FMcMob* Pig = new FMcMob(*FMcMobType::Find(TEXT("minecraft:pig")));
		Pig->Body.Position = FVector(0.5, 1.0, 0.5);
		Manager.Add(TUniquePtr<FMcEntity>(Pig));
		bool bDied = false;
		Pig->OnDeath = [&bDied](FMcMob&) { bDied = true; };
		for (int32 Tick = 0; Tick < 600; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a pig stays on the floor"), Pig->Body.bOnGround && FMath::Abs(Pig->Body.Position.Y - 1.0) < 0.01);
		TestTrue(TEXT("and has wandered off"), DistXZ(Pig->Body.Position, FVector(0.5, 1.0, 0.5)) > 1.0);
		const FVector Hitter(Pig->Body.Position.X - 1.0, 1.0, Pig->Body.Position.Z);
		TestFalse(TEXT("a hit does not kill a pig"), Pig->Damage(4.0f, &Hitter));
		TestTrue(TEXT("it is pushed away"), Pig->Body.Velocity.X > 0.1);
		TestTrue(TEXT("and runs"), Pig->PanicTicks > 0);
		TestEqual(TEXT("the hit is counted"), Pig->HurtEvents, 1);
		TestFalse(TEXT("a second hit in the same instant is ignored"), Pig->Damage(100.0f, &Hitter) || bDied);
		TestEqual(TEXT("and not counted"), Pig->HurtEvents, 1);
		for (int32 Tick = 0; Tick < 10; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("the next one kills it"), Pig->Damage(8.0f, &Hitter));
		TestTrue(TEXT("and its death is reported"), bDied);
		TestEqual(TEXT("and counted"), Pig->HurtEvents, 2);
		Manager.Tick();

		// Each mob runs its own random stream: pigs set down together don't all pick the same walk.
		TArray<FMcMob*> Herd;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FMcMob* Member = new FMcMob(*FMcMobType::Find(TEXT("minecraft:pig")));
			Member->Body.Position = FVector(0.5, 1.0, 0.5);
			Manager.Add(TUniquePtr<FMcEntity>(Member));
			Herd.Add(Member);
		}
		TArray<TOptional<FVector>> FirstGoals;
		FirstGoals.SetNum(Herd.Num());
		for (int32 Tick = 0; Tick < 3000; ++Tick)
		{
			Manager.Tick();
			bool bAll = true;
			for (int32 Index = 0; Index < Herd.Num(); ++Index)
			{
				if (!FirstGoals[Index].IsSet() && Herd[Index]->Goal.IsSet())
				{
					FirstGoals[Index] = Herd[Index]->Goal.GetValue();
				}
				bAll &= FirstGoals[Index].IsSet();
			}
			if (bAll)
			{
				break;
			}
		}
		bool bAllChose = true;
		bool bAnyAlike = false;
		for (int32 A = 0; A < Herd.Num(); ++A)
		{
			bAllChose &= FirstGoals[A].IsSet();
			for (int32 B = A + 1; B < Herd.Num() && FirstGoals[A].IsSet(); ++B)
			{
				bAnyAlike |= FirstGoals[B].IsSet() && FVector::DistSquared(FirstGoals[A].GetValue(), FirstGoals[B].GetValue()) < 1e-6;
			}
		}
		TestTrue(TEXT("every pig chose somewhere to walk"), bAllChose);
		TestFalse(TEXT("and no two chose the same place"), bAnyAlike);
		for (FMcMob* Member : Herd)
		{
			Member->bRemoved = true;
		}
		Manager.Tick();

		FMcMob* Zombie = new FMcMob(*FMcMobType::Find(TEXT("minecraft:zombie")));
		Zombie->Body.Position = FVector(20.5, 1.0, 20.5);
		float Bitten = 0.0f;
		const FVector Player(16.5, 1.0, 20.5);
		Manager.PlayerPositions = { Player };
		Zombie->OnAttackPlayer = [&Bitten](const FVector&, float Damage) { Bitten += Damage; };
		Manager.Add(TUniquePtr<FMcEntity>(Zombie));
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("the zombie walked up to the player"), DistXZ(Zombie->Body.Position, Player) < 2.0);
		TestTrue(TEXT("and bit"), Bitten >= 3.0f);
		// The wall in its way is climbed (a one block step).
		World.SetBlock(FIntVector(18, 1, 20), State(TEXT("minecraft:stone")));
		Zombie->Body.Position = FVector(20.5, 1.0, 20.5);
		Bitten = 0.0f;
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a block in the way is jumped"), Bitten >= 3.0f);
	}
	{
		// Stacks of one item with different components stay apart: a pot with sherds and a plain one.
		FMcItemStack Plain(TEXT("minecraft:decorated_pot"));
		FMcItemStack Sherds = Plain;
		McPot::SetSides(Sherds, { TEXT("minecraft:brick"), TEXT("minecraft:angler_pottery_sherd"), TEXT("minecraft:brick"), TEXT("minecraft:brick") });
		Manager.DropItem(FVector(5.5, 1.0, 5.5), Plain).Body.Velocity = FVector::ZeroVector;
		Manager.DropItem(FVector(5.6, 1.0, 5.5), Sherds).Body.Velocity = FVector::ZeroVector;
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Manager.Tick();
		}
		int32 Pots = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			Pots += !Entity->bRemoved && Entity->GetType() == FMcItemEntity::StaticType() && static_cast<const FMcItemEntity&>(*Entity).Stack.Item == TEXT("minecraft:decorated_pot");
		}
		TestEqual(TEXT("a pot with sherds doesn't join a plain one"), Pots, 2);
	}
	return true;
}

#endif
