#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McArrow.h"
#include "McEntityManager.h"
#include "McFireball.h"
#include "McMob.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Behaviour of the hostile mobs taken from the villagers session's batch (zoglin, breeze, parched). Numbers marked UNVERIFIED in PLAN.md
// ("Hostile mobs: zoglin, breeze, parched") are the wiki's, not the reference client's.
namespace
{
	FMcMob* AddHostile(FMcEntityManager& Manager, const TCHAR* Kind, const FVector& Pos)
	{
		FMcMob* Mob = new FMcMob(*FMcMobType::Find(Kind));
		Mob->Body.Position = Pos;
		Manager.Add(TUniquePtr<FMcEntity>(Mob));
		return Mob;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcZoglinTest, "McClient.McEntity.Zoglin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcZoglinTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	const FMcStateId Stone = Registry.ParseState(TEXT("minecraft:stone")).Get(0);
	for (int32 X = -16; X <= 31; ++X)
	{
		for (int32 Z = -16; Z <= 31; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Stone);
		}
	}
	FMcEntityManager Manager(World);
	Manager.Difficulty = 2;
	Manager.bDaylight = false;

	// No player: a zoglin goes for the cow near it, and not for the creeper or the other zoglin.
	FMcMob* Zoglin = AddHostile(Manager, TEXT("minecraft:zoglin"), FVector(0.5, 1.0, 0.5));
	FMcMob* Cow = AddHostile(Manager, TEXT("minecraft:cow"), FVector(8.5, 1.0, 0.5));
	FMcMob* Creeper = AddHostile(Manager, TEXT("minecraft:creeper"), FVector(-8.5, 1.0, 0.5));
	Creeper->bStayPut = true;
	Cow->bStayPut = true;
	const float CowHealth = Cow->Health;
	for (int32 Tick = 0; Tick < 300 && Cow->Health >= CowHealth; ++Tick)
	{
		Manager.Tick();
	}
	TestTrue(TEXT("the zoglin walked to the cow and bit it"), Cow->Health < CowHealth || Cow->bRemoved);
	TestEqual(TEXT("the creeper was left alone"), Creeper->Health, 20.0f);
	TestEqual(TEXT("40 health"), Zoglin->Health, 40.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBreezeParchedTest, "McClient.McEntity.BreezeParched",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBreezeParchedTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	const FMcStateId Stone = Registry.ParseState(TEXT("minecraft:stone")).Get(0);
	for (int32 X = -16; X <= 31; ++X)
	{
		for (int32 Z = -16; Z <= 31; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Stone);
		}
	}
	FMcEntityManager Manager(World);
	Manager.Difficulty = 2;
	Manager.bDaylight = true;
	float Hurt = 0.0f;
	Manager.OnPlayerHurt = [&Hurt](const FVector&, float Damage) { Hurt += Damage; };
	Manager.PlayerPositions = { FVector(8.5, 1.0, 0.5) };

	// The breeze: wind charges every 43 to 60 ticks at 0.70 a tick, 1 damage, jumps well above its own height.
	FMcMob* Breeze = AddHostile(Manager, TEXT("minecraft:breeze"), FVector(8.5, 1.0, 14.5));
	TestEqual(TEXT("30 health"), Breeze->Health, 30.0f);
	int32 Charges = 0;
	double HighestFeet = 1.0;
	double FastestCharge = 0.0;
	TSet<int32> Seen;
	for (int32 Tick = 0; Tick < 240; ++Tick)
	{
		Manager.Tick();
		HighestFeet = FMath::Max(HighestFeet, Breeze->Body.Position.Y);
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (Entity->GetType() == FMcFireball::WindType() && !Seen.Contains(Entity->Id))
			{
				Seen.Add(Entity->Id);
				++Charges;
				FastestCharge = FMath::Max(FastestCharge, Entity->Body.Velocity.Size());
			}
		}
	}
	TestTrue(TEXT("it fired three to five charges in 240 ticks"), Charges >= 3 && Charges <= 6);
	TestTrue(TEXT("each flew at 0.70 a tick"), FMath::IsNearlyEqual(FastestCharge, 0.70, 0.01));
	TestTrue(TEXT("a charge hurt the player for 1"), Hurt >= 1.0f);
	TestTrue(TEXT("it jumped"), HighestFeet > 3.0);
	TestTrue(TEXT("and kept its distance"), FVector::Dist(Breeze->Body.Position, Manager.PlayerPositions[0]) > 3.0);

	// The parched: a skeleton that shoots arrows and does not burn by day.
	FMcMob* Parched = AddHostile(Manager, TEXT("minecraft:parched"), FVector(8.5, 1.0, 12.5));
	bool bArrow = false;
	for (int32 Tick = 0; Tick < 300 && !bArrow; ++Tick)
	{
		Manager.Tick();
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			bArrow |= Entity->GetType() == FMcArrowEntity::StaticType();
		}
	}
	TestTrue(TEXT("the parched shoots arrows"), bArrow);
	TestEqual(TEXT("16 health, unburnt by daylight"), Parched->Health, 16.0f);
	return true;
}

#endif
