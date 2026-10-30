#include "McElytra.h"
#include "McItemRegistry.h"
#include "McItemStack.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcElytraTest, "McClient.McPlayer.Elytra",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcElytraTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}

	// Glides for a minute at a fixed pitch from a gentle start; the steady velocity, in blocks a tick.
	const auto Settle = [](float Pitch, int32 Boosted = 0)
	{
		FVector Velocity(0.0, 0.0, 0.5);
		for (int32 Tick = 0; Tick < 1200; ++Tick)
		{
			Velocity = McElytra::Glide(Velocity, 0.0f, Pitch);
			if (Tick >= 1200 - Boosted)
			{
				Velocity = McElytra::Boost(Velocity, 0.0f, Pitch);
			}
		}
		return Velocity;
	};

	// Level flight goes about 10 blocks for each one lost, at about 12 metres a second.
	{
		const FVector Level = Settle(0.0f);
		const double Ahead = FVector2D(Level.X, Level.Z).Size();
		TestTrue(TEXT("level flight heads the way it looks"), Level.Z > 0.0 && FMath::Abs(Level.X) < 1e-6);
		TestTrue(TEXT("level flight sinks slowly"), Level.Y < 0.0 && Ahead / -Level.Y > 9.0 && Ahead / -Level.Y < 11.0);
		TestTrue(TEXT("at about 12 m/s"), Level.Size() * 20.0 > 11.0 && Level.Size() * 20.0 < 13.5);
	}
	// Straight down tops out near 68 m/s, and a 45 degree dive between the two.
	{
		const double Dive = Settle(90.0f).Size() * 20.0;
		const double Half = Settle(45.0f).Size() * 20.0;
		TestTrue(TEXT("a dive tops out near 68 m/s"), Dive > 62.0 && Dive < 74.0);
		TestTrue(TEXT("a 45 degree dive is in between"), Half > 30.0 && Half < Dive);
	}
	// Climbing steeply loses speed and stalls.
	{
		const FVector Climb = Settle(-60.0f);
		TestTrue(TEXT("a steep climb stalls and sinks"), Climb.Size() * 20.0 < 9.0 && Climb.Y < 0.0);
	}
	// A rocket pulls toward 33.5 blocks a second along the look.
	{
		const FVector Boosted = Settle(0.0f, 40);
		TestTrue(TEXT("a rocket speeds a level glide up to near 33.5 m/s"), Boosted.Z * 20.0 > 25.0 && Boosted.Size() * 20.0 < 34.0);
		FRandomStream Random(3);
		int32 Low = MAX_int32, High = 0;
		for (int32 Roll = 0; Roll < 2000; ++Roll)
		{
			const int32 Ticks = McElytra::RollBoostTicks(Random, 1);
			Low = FMath::Min(Low, Ticks);
			High = FMath::Max(High, Ticks);
		}
		TestEqual(TEXT("a flight 1 rocket pulls at least 20 ticks"), Low, 20);
		TestEqual(TEXT("and at most 31"), High, 31);
		TestEqual(TEXT("flight 3 starts at 40"), McElytra::RollBoostTicks(Random, 3) >= 40, true);
	}
	// Walls: 10 times the speed lost, less 3.
	{
		TestEqual(TEXT("a graze does no harm"), McElytra::WallDamage(0.25, 0.0), 0.0f);
		TestTrue(TEXT("a fast hit hurts"), FMath::IsNearlyEqual(McElytra::WallDamage(1.5, 0.2), 10.0f, 1e-4f));
	}
	// The wings work until their last point of durability.
	{
		FMcItemStack Wings(FName(TEXT("minecraft:elytra")), 1);
		TestTrue(TEXT("new elytra glide"), McElytra::CanGlide(Wings));
		TestEqual(TEXT("elytra last 432"), Wings.GetMaxDamage(), 432);
		Wings.SetDamage(430);
		TestTrue(TEXT("with two points left they still glide"), McElytra::CanGlide(Wings));
		Wings.SetDamage(431);
		TestFalse(TEXT("with one left they don't"), McElytra::CanGlide(Wings));
		TestFalse(TEXT("a chestplate doesn't glide"), McElytra::CanGlide(FMcItemStack(FName(TEXT("minecraft:iron_chestplate")), 1)));
	}
	return true;
}

#endif
