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

	// Glides from a jump off a high place (falling at 0.44 blocks a tick, as vanilla is when the script's glide starts)
	// through looks held for so many ticks each; the velocity after, in blocks a tick, and the height lost over the last.
	struct FLeg { float Pitch; int32 Ticks; };
	const auto Fly = [](std::initializer_list<FLeg> Legs, int32 Boosted = 0, double* LastDrop = nullptr)
	{
		FVector Velocity(0.0, -0.44, 0.0);
		int32 Left = 0;
		for (const FLeg& Leg : Legs)
		{
			Left += Leg.Ticks;
		}
		for (const FLeg& Leg : Legs)
		{
			double Drop = 0.0;
			for (int32 Tick = 0; Tick < Leg.Ticks; ++Tick, --Left)
			{
				Velocity = McElytra::Glide(Velocity, 0.0f, Leg.Pitch);
				if (Left <= Boosted)
				{
					Velocity = McElytra::Boost(Velocity, 0.0f, Leg.Pitch);
				}
				Drop -= Velocity.Y;
			}
			if (LastDrop)
			{
				*LastDrop = Drop;
			}
		}
		return Velocity;
	};

	// Measured in vanilla (mc-reference scripts/glide_steady.txt): 10 s level runs at 1.18 blocks a tick and sinks
	// 0.135; 7 s at 20 down after 21 s level, 1.85 ahead; 10 s straight down after 10 s level, 3.0 a tick down.
	{
		const FVector Level = Fly({ { 0.0f, 200 } });
		TestTrue(TEXT("level flight heads the way it looks"), Level.Z > 0.0 && FMath::Abs(Level.X) < 1e-6);
		TestTrue(TEXT("10 s of level flight runs at about 1.18 a tick"), Level.Z > 1.0 && Level.Z < 1.35);
		TestTrue(TEXT("and sinks about 0.135 a tick"), Level.Y < -0.1 && Level.Y > -0.2);
		const FVector Long = Fly({ { 0.0f, 1200 } });
		TestTrue(TEXT("a long level glide goes about 10 blocks for each one lost (wiki)"), Long.Z / -Long.Y > 8.0 && Long.Z / -Long.Y < 13.0);
		const FVector Down = Fly({ { 0.0f, 420 }, { 20.0f, 140 } });
		TestTrue(TEXT("20 down holds about 1.85 a tick"), Down.Z > 1.6 && Down.Z < 2.1);
		double Drop = 0.0;
		Fly({ { 0.0f, 200 }, { 90.0f, 200 } }, 0, &Drop);
		TestTrue(TEXT("straight down averages about 3 a tick over 10 s"), Drop / 200.0 > 2.7 && Drop / 200.0 < 3.3);
	}
	// Climbing steeply loses its speed ahead and stalls.
	{
		const FVector Climb = Fly({ { 0.0f, 200 }, { -60.0f, 1200 } });
		TestTrue(TEXT("a steep climb stalls and sinks"), Climb.Z < 0.2 && Climb.Y < 0.0);
	}
	// A rocket pulls toward 33.5 blocks a second along the look.
	{
		const FVector Boosted = Fly({ { 0.0f, 1200 } }, 40);
		TestTrue(TEXT("a rocket speeds a level glide up to near 33.5 m/s"), Boosted.Z * 20.0 > 32.0 && Boosted.Size() * 20.0 < 34.0);
		// Measured (scripts/glide_speed.txt): the gap to the rocket's speed about halves each tick.
		const FVector Start(0.0, 0.0, 1.0);
		const FVector Next = McElytra::Boost(Start, 0.0f, 0.0f);
		TestTrue(TEXT("halving the gap each tick"), FMath::IsNearlyEqual((33.5 / 20.0 - Next.Z) / (33.5 / 20.0 - Start.Z), 0.5, 0.05));
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
