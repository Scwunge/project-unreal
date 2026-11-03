#include "McArrow.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McFireball.h"
#include "McMob.h"
#include "McThrown.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void FillMobBox(FMcBlockWorld& World, int32 X0, int32 Y0, int32 Z0, int32 X1, int32 Y1, int32 Z1, FMcStateId Block)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 Z = Z0; Z <= Z1; ++Z)
				{
					World.SetBlock(FIntVector(X, Y, Z), Block);
				}
			}
		}
	}

	// Nine chunks with a stone floor at y 0, so a mob's feet stand at y 1.
	void MakeFlatWorld(FMcBlockWorld& World, FMcStateId Stone)
	{
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		FillMobBox(World, -16, 0, -16, 31, 0, 31, Stone);
	}

	FMcMob* AddMob(FMcEntityManager& Manager, const TCHAR* Kind, const FVector& Pos)
	{
		FMcMob* Mob = new FMcMob(*FMcMobType::Find(Kind));
		Mob->Body.Position = Pos;
		Manager.Add(TUniquePtr<FMcEntity>(Mob));
		return Mob;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMobBehaviourTest, "McClient.McEntity.MobBehaviours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcMobBehaviourTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Air = State(TEXT("minecraft:air"));

	{
		// A zombie walks round a wall to bite a player behind it: a straight line would leave it standing at the stone.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FillMobBox(World, 3, 1, -5, 3, 2, 5, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = false;
		Manager.PlayerPositions = { FVector(7.5, 1.0, 0.5) };
		FMcMob* Zombie = AddMob(Manager, TEXT("minecraft:zombie"), FVector(0.5, 1.0, 0.5));
		bool bBit = false;
		Zombie->OnAttackPlayer = [&bBit](const FVector&, float) { bBit = true; };
		double MostSideways = 0.0;
		for (int32 Tick = 0; Tick < 400 && !bBit; ++Tick)
		{
			Manager.Tick();
			MostSideways = FMath::Max(MostSideways, FMath::Abs(Zombie->Body.Position.Z - 0.5));
		}
		TestTrue(TEXT("the zombie gets round the wall and bites"), bBit);
		TestTrue(TEXT("by going out past its end"), MostSideways > 4.0);
	}
	{
		// Speed: a chasing zombie covers about 2.3 blocks a second on the flat, not the 10 the player's physics would
		// give its 0.23 attribute; a pig's input comes to about 2.4.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = false;
		Manager.PlayerPositions = { FVector(20.5, 1.0, 0.5) };
		FMcMob* Zombie = AddMob(Manager, TEXT("minecraft:zombie"), FVector(-12.5, 1.0, 0.5));
		for (int32 Tick = 0; Tick < 40; ++Tick)
		{
			Manager.Tick();
		}
		const FVector Start = Zombie->Body.Position;
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Manager.Tick();
		}
		const double Walked = FMath::Sqrt(FMath::Square(Zombie->Body.Position.X - Start.X) + FMath::Square(Zombie->Body.Position.Z - Start.Z));
		const double PerSecond = Walked / 3.0;
		TestTrue(*FString::Printf(TEXT("a zombie walks 2.3 blocks a second (got %.2f)"), PerSecond), PerSecond > 2.1 && PerSecond < 2.5);
		const double PigPerSecond = FMcMob::GetInputSpeed(*FMcMobType::Find(TEXT("minecraft:pig"))) * FMcMob::PlayerBlocksPerSecondPerSpeed;
		TestTrue(*FString::Printf(TEXT("a pig walks about 2.4 (got %.2f)"), PigPerSecond), PigPerSecond > 2.2 && PigPerSecond < 2.6);
	}
	{
		// Creeper: walks up, stands still and swells for 30 ticks, goes off, takes the floor with it and hurts the player.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = true;
		float PlayerDamage = 0.0f;
		Manager.OnPlayerHurt = [&PlayerDamage](const FVector&, float Damage) { PlayerDamage += Damage; };
		const FVector Player(8.5, 1.0, 14.5);
		Manager.PlayerPositions = { Player };
		FMcMob* Creeper = AddMob(Manager, TEXT("minecraft:creeper"), FVector(8.5, 1.0, 8.5));
		int32 SwellStart = -1;
		FVector SwellPosition = FVector::ZeroVector;
		bool bMovedWhileSwelling = false;
		int32 Tick = 0;
		for (; Tick < 400 && !Creeper->bRemoved; ++Tick)
		{
			Manager.Tick();
			if (Creeper->bSwelling && SwellStart < 0)
			{
				SwellStart = Tick;
				SwellPosition = Creeper->Body.Position;
			}
			else if (Creeper->bSwelling && !Creeper->bRemoved)
			{
				// It slides to a stop from the walk; it doesn't go on walking.
				bMovedWhileSwelling |= FMath::Square(Creeper->Body.Position.X - SwellPosition.X) + FMath::Square(Creeper->Body.Position.Z - SwellPosition.Z) > 0.5 * 0.5;
			}
		}
		TestTrue(TEXT("the creeper goes off"), Creeper->bRemoved);
		TestTrue(TEXT("having started to swell"), SwellStart >= 0);
		TestTrue(TEXT("after a 30 tick fuse"), Tick - SwellStart >= FMcMob::FuseTicks - 1 && Tick - SwellStart <= FMcMob::FuseTicks + 2);
		TestFalse(TEXT("standing still while it swelled"), bMovedWhileSwelling);
		TestTrue(TEXT("the player is hurt"), PlayerDamage > 0.0f);
		TestEqual(TEXT("and the floor under it is gone"), World.GetBlock(FIntVector(FMath::FloorToInt32(SwellPosition.X), 0, FMath::FloorToInt32(SwellPosition.Z))), Air);
		TestEqual(TEXT("a creeper that went off is not left standing"), Manager.GetEntities().Num(), 0);
	}
	{
		// A creeper's fuse goes out if the player gets away before it is up.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.PlayerPositions = { FVector(8.5, 1.0, 10.0) };
		FMcMob* Creeper = AddMob(Manager, TEXT("minecraft:creeper"), FVector(8.5, 1.0, 8.5));
		for (int32 Tick = 0; Tick < 15; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("swelling next to a player"), Creeper->bSwelling && Creeper->SwellTicks > 5);
		// Beyond the 16 blocks it follows from, so it doesn't walk up and start again.
		Manager.PlayerPositions = { FVector(8.5, 1.0, 30.5) };
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			Manager.Tick();
		}
		TestFalse(TEXT("a player gone 11 blocks away: it stops"), Creeper->bSwelling);
		TestEqual(TEXT("and the fuse is out"), Creeper->SwellTicks, 0);
		TestFalse(TEXT("without having gone off"), Creeper->bRemoved && World.GetBlock(FIntVector(8, 0, 8)) == Air);
	}
	{
		// Spider: neutral in daylight until it's hurt, hostile at night; climbs walls.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = true;
		Manager.PlayerPositions = { FVector(8.5, 1.0, 10.0) };
		FMcMob* Spider = AddMob(Manager, TEXT("minecraft:spider"), FVector(8.5, 1.0, 8.5));
		int32 Bites = 0;
		Spider->OnAttackPlayer = [&Bites](const FVector&, float) { ++Bites; };
		for (int32 Tick = 0; Tick < 80; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("in daylight a spider next to a player leaves it alone"), Bites, 0);
		Spider->Damage(0.5f, nullptr);
		for (int32 Tick = 0; Tick < 80; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("once hurt, it bites"), Bites > 0);

		FMcEntityManager Night(World);
		Night.bDaylight = false;
		Night.PlayerPositions = { FVector(8.5, 1.0, 10.0) };
		FMcMob* NightSpider = AddMob(Night, TEXT("minecraft:spider"), FVector(8.5, 1.0, 8.5));
		int32 NightBites = 0;
		NightSpider->OnAttackPlayer = [&NightBites](const FVector&, float) { ++NightBites; };
		for (int32 Tick = 0; Tick < 80; ++Tick)
		{
			Night.Tick();
		}
		TestTrue(TEXT("at night it bites without being hurt"), NightBites > 0);

		// A wall four high between it and the player: it goes up and over.
		FillMobBox(World, 14, 1, -16, 14, 4, 31, Stone);
		FMcEntityManager Climb(World);
		Climb.bDaylight = false;
		Climb.PlayerPositions = { FVector(18.5, 1.0, 8.5) };
		FMcMob* Climber = AddMob(Climb, TEXT("minecraft:spider"), FVector(11.5, 1.0, 8.5));
		bool bBit = false;
		Climber->OnAttackPlayer = [&bBit](const FVector&, float) { bBit = true; };
		double Highest = 0.0;
		for (int32 Tick = 0; Tick < 300 && !bBit; ++Tick)
		{
			Climb.Tick();
			Highest = FMath::Max(Highest, Climber->Body.Position.Y);
		}
		TestTrue(TEXT("the spider climbs the wall"), Highest > 4.5);
		TestTrue(TEXT("and bites the player on the other side"), bBit);
		FillMobBox(World, 14, 1, -16, 14, 4, 31, Air);
	}
	{
		// Skeleton: stops at a distance and shoots; the arrow flies in an arc and hurts the player.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = false;
		float Damage = 0.0f;
		Manager.OnPlayerHurt = [&Damage](const FVector&, float Hurt) { Damage += Hurt; };
		const FVector Player(8.5, 1.0, 20.5);
		Manager.PlayerPositions = { Player };
		FMcMob* Skeleton = AddMob(Manager, TEXT("minecraft:skeleton"), FVector(8.5, 1.0, 12.5));
		bool bSawArrow = false;
		for (int32 Tick = 0; Tick < 500 && Damage == 0.0f; ++Tick)
		{
			Manager.Tick();
			for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
			{
				bSawArrow |= Entity->GetType() == FMcArrowEntity::StaticType();
			}
		}
		TestTrue(TEXT("the skeleton shoots arrows"), bSawArrow);
		TestTrue(TEXT("and one hurts the player for 3 to 5"), Damage >= 3.0f && Damage <= 5.0f);
		TestTrue(TEXT("while it keeps its distance (10 blocks)"), FMath::Abs(Skeleton->Body.Position.Z - Player.Z) >= 7.0);

		// An arrow into a wall sticks there and is gone after a minute.
		FillMobBox(World, 8, 1, 10, 9, 3, 10, Stone);
		TUniquePtr<FMcArrowEntity> Arrow = MakeUnique<FMcArrowEntity>();
		Arrow->Body.Position = FVector(8.5, 2.0, 5.5);
		Arrow->Body.Velocity = FVector(0.0, 0.0, 1.0);
		FMcArrowEntity* Shot = Arrow.Get();
		Manager.PlayerPositions.Reset();
		Manager.Add(MoveTemp(Arrow));
		for (int32 Tick = 0; Tick < 20; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("an arrow sticks in a wall"), Shot->bStuck);
		TestTrue(TEXT("just inside it"), Shot->Body.Position.Z >= 10.0 &&Shot->Body.Position.Z < 10.5);
		TestFalse(TEXT("arrows are not kept in a chunk's save"), Shot->IsPersistent());
	}
	{
		// A player's arrow at full draw (3 blocks a tick) hurts a mob in its way by 6, and passes players.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = true;
		Manager.PlayerPositions = { FVector(8.5, 1.0, 5.5) };
		FMcMob* Pig = AddMob(Manager, TEXT("minecraft:pig"), FVector(8.5, 1.0, 12.5));
		const float Before = Pig->Health;
		TUniquePtr<FMcArrowEntity> Arrow = MakeUnique<FMcArrowEntity>();
		Arrow->Body.Position = FVector(8.5, 1.4, 5.8);
		Arrow->Body.Velocity = FVector(0.0, 0.0, 3.0);
		Arrow->PlayerBase = 2.0f;
		Arrow->bPickup = true;
		FMcArrowEntity* Shot = Arrow.Get();
		Manager.Add(MoveTemp(Arrow));
		for (int32 Tick = 0; Tick < 6; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a player's arrow hurts the pig it hits by 6"), FMath::IsNearlyEqual(Before - Pig->Health, 6.0f, 0.01f));
		TestFalse(TEXT("the hit arrow is spent"), Shot != nullptr && !Manager.GetEntities().IsEmpty() && Manager.GetEntities().Last().Get() == Shot);
	}
	{
		// A snowball knocks a pig back without hurting it; an egg breaks into a chick one time in eight; a pearl comes
		// down in front of the wall it hits and tells where.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = true;
		Manager.PlayerPositions = { FVector(8.5, 1.0, 5.5) };
		FMcMob* Pig = AddMob(Manager, TEXT("minecraft:pig"), FVector(8.5, 1.0, 9.5));
		const float Health = Pig->Health;
		TUniquePtr<FMcThrownEntity> Ball = MakeUnique<FMcThrownEntity>(FMcThrownEntity::EKind::Snowball);
		Ball->Body.Position = FVector(8.5, 1.4, 5.8);
		Ball->Body.Velocity = FVector(0.0, 0.0, FMcThrownEntity::Speed);
		Manager.Add(MoveTemp(Ball));
		for (int32 Tick = 0; Tick < 6; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("a snowball flinches the pig it hits"), Pig->HurtEvents, 1);
		TestEqual(TEXT("without taking health"), Pig->Health, Health);

		TestEqual(TEXT("an egg rolling under an eighth makes a chick"), FMcThrownEntity::GetChickCount(0.1f, 0.5f), 1);
		TestEqual(TEXT("one in thirty-two of those make four"), FMcThrownEntity::GetChickCount(0.1f, 0.01f), 4);
		TestEqual(TEXT("and over an eighth makes none"), FMcThrownEntity::GetChickCount(0.2f, 0.01f), 0);

		FillMobBox(World, 8, 1, 14, 9, 3, 14, Stone);
		TUniquePtr<FMcThrownEntity> Pearl = MakeUnique<FMcThrownEntity>(FMcThrownEntity::EKind::EnderPearl);
		Pearl->Body.Position = FVector(8.5, 3.5, 5.8);
		Pearl->Body.Velocity = FVector(0.0, 0.0, FMcThrownEntity::Speed);
		FVector Landed = FVector::ZeroVector;
		Pearl->OnPearlLanded = [&Landed](const FVector& At) { Landed = At; };
		Manager.Add(MoveTemp(Pearl));
		for (int32 Tick = 0; Tick < 12; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("a pearl lands in front of the wall"), Landed.Z > 12.0 && Landed.Z < 14.0);
		// A baby chicken is half the size and grows up.
		FMcMob* Chick = AddMob(Manager, TEXT("minecraft:chicken"), FVector(2.5, 1.0, 2.5));
		Chick->MakeBaby();
		TestTrue(TEXT("a chick is half a chicken's size"), FMath::IsNearlyEqual(Chick->Body.Width, 0.2f, 0.001f));
	}
	{
		// Enderman: neutral until hurt; a hit teleports it off.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = false;
		Manager.PlayerPositions = { FVector(8.5, 1.0, 10.5) };
		FMcMob* Enderman = AddMob(Manager, TEXT("minecraft:enderman"), FVector(8.5, 1.0, 8.5));
		int32 Bites = 0;
		Enderman->OnAttackPlayer = [&Bites](const FVector&, float) { ++Bites; };
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("an enderman left alone doesn't attack"), Bites, 0);
		const FVector Before = Enderman->Body.Position;
		Enderman->Damage(1.0f, nullptr);
		Manager.Tick();
		TestTrue(TEXT("hurt, it teleports away"), FVector::Dist(Enderman->Body.Position, Before) > 1.0);
		TestTrue(TEXT("onto ground"), Enderman->Body.Position.Y > 0.5);
		TestTrue(TEXT("and is now angry"), Enderman->AngerTicks > 0);
	}
	{
		// Slime: hops (sits, then jumps), and the box and health go by size.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = false;
		Manager.PlayerPositions = { FVector(8.5, 1.0, 20.5) };
		FMcMob* Slime = AddMob(Manager, TEXT("minecraft:slime"), FVector(8.5, 1.0, 8.5));
		Slime->SetSize(2);
		TestTrue(TEXT("a medium slime is 1.04 across"), FMath::IsNearlyEqual(Slime->Body.Width, 1.04f, 0.001f));
		TestTrue(TEXT("with 4 health"), FMath::IsNearlyEqual(Slime->Health, 4.0f));
		TestTrue(TEXT("and bites for 2"), FMath::IsNearlyEqual(Slime->GetAttackDamage(), 2.0f));
		Slime->SetSize(4);
		TestTrue(TEXT("a large one has 16"), FMath::IsNearlyEqual(Slime->Health, 16.0f));
		TestTrue(TEXT("and bites for 4"), FMath::IsNearlyEqual(Slime->GetAttackDamage(), 4.0f));
		Slime->SetSize(1);
		TestTrue(TEXT("the smallest doesn't bite"), FMath::IsNearlyEqual(Slime->GetAttackDamage(), 0.0f));
		Slime->SetSize(2);
		const FVector Start = Slime->Body.Position;
		bool bLeftGround = false;
		int32 Hops = 0;
		bool bWasOnGround = true;
		for (int32 Tick = 0; Tick < 120; ++Tick)
		{
			Manager.Tick();
			bLeftGround |= !Slime->Body.bOnGround;
			Hops += (bWasOnGround && !Slime->Body.bOnGround) ? 1 : 0;
			bWasOnGround = Slime->Body.bOnGround;
		}
		TestTrue(TEXT("a slime hops, not walks"), bLeftGround && Hops >= 2);
		TestTrue(TEXT("towards the player"), Slime->Body.Position.Z > Start.Z + 3.0);
	}
	{
		// The light estimate that decides where hostile mobs spawn.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		const FIntVector Open(5, 1, 5);
		Manager.bDaylight = false;
		TestEqual(TEXT("open sky at night is 4"), Manager.EstimateSkyLight(Open), 4);
		TestTrue(TEXT("dark enough to spawn"), Manager.IsDarkEnoughToSpawn(Open));
		Manager.bDaylight = true;
		TestEqual(TEXT("open sky by day is 15"), Manager.EstimateSkyLight(Open), 15);
		TestFalse(TEXT("too bright to spawn"), Manager.IsDarkEnoughToSpawn(Open));
		// Under a roof 21 across, by day: dark.
		FillMobBox(World, 0, 3, 0, 20, 3, 20, Stone);
		TestEqual(TEXT("under a wide roof the sky is out"), Manager.EstimateSkyLight(FIntVector(10, 1, 10)), 0);
		TestTrue(TEXT("so mobs spawn there by day"), Manager.IsDarkEnoughToSpawn(FIntVector(10, 1, 10)));
		TestTrue(TEXT("though near the edge, light creeps in"), Manager.EstimateSkyLight(FIntVector(1, 1, 10)) >= 8);
		// A torch lights its surroundings, to 13 a block off.
		World.SetBlock(FIntVector(10, 1, 11), State(TEXT("minecraft:torch")));
		TestEqual(TEXT("a block from a torch: 13"), Manager.EstimateBlockLight(FIntVector(10, 1, 10)), 13);
		TestEqual(TEXT("five blocks: 9"), Manager.EstimateBlockLight(FIntVector(10, 1, 6)), 9);
		TestEqual(TEXT("fifteen blocks: dark"), Manager.EstimateBlockLight(FIntVector(10, 1, -4)), 0);
		TestFalse(TEXT("no spawning beside it, roof or not"), Manager.IsDarkEnoughToSpawn(FIntVector(10, 1, 10)));
	}
	{
		// In a pool (logged in the reference, drownswim.txt): a drowned swims at its target at about 0.0485 blocks a tick and a zombie
		// only sinks.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		const FMcStateId Water = State(TEXT("minecraft:water"));
		FillMobBox(World, 0, 1, -4, 30, 12, 4, Water);
		FMcEntityManager Manager(World);
		Manager.bDaylight = false;
		Manager.PlayerPositions = { FVector(0.5, 5.0, 0.5) };
		FMcMob* Drowned = AddMob(Manager, TEXT("minecraft:drowned"), FVector(20.5, 5.0, 0.5));
		FMcMob* Zombie = AddMob(Manager, TEXT("minecraft:zombie"), FVector(20.5, 9.0, 3.5));
		for (int32 Tick = 0; Tick < 100; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("the drowned has swum 3 to 6 blocks toward the player"), Drowned->Body.Position.X < 20.5 - 3.0 && Drowned->Body.Position.X > 20.5 - 6.0);
		TestTrue(TEXT("the zombie has sunk, not risen"), Zombie->Body.Position.Y < 8.0);
	}
	{
		// Bartering (barter1.txt, 50 runs): a piglin takes a gold ingot dropped by it and throws one item about 120 ticks later.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.bDaylight = false;
		FMcMob* Piglin = AddMob(Manager, TEXT("minecraft:piglin"), FVector(10.5, 1.0, 10.5));
		Manager.DropItem(FVector(10.5, 1.0, 14.5), FMcItemStack(TEXT("minecraft:gold_ingot"), 1), 0);
		int32 Items = 0;
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			Manager.Tick();
		}
		for (const TUniquePtr<FMcEntity>& Each : Manager.GetEntities())
		{
			if (Each->GetType() == FMcItemEntity::StaticType() && static_cast<FMcItemEntity*>(Each.Get())->Stack.Item != TEXT("minecraft:gold_ingot"))
			{
				++Items;
			}
		}
		TestEqual(TEXT("one item thrown for the ingot"), Items, 1);
		TestEqual(TEXT("and the piglin is ready again"), Piglin->BarterTicks, 0);
	}
	{
		// A blaze in sight of a player shoots three small fireballs 6 ticks apart then rests until 178 ticks after the first (nether1.txt),
		// and a fireball's speed climbs 0.1, 0.19, 0.2755 (v = 0.95 * (v + 0.1)).
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.Difficulty = 2;
		Manager.PlayerPositions = { FVector(10.5, 1.0, 20.5) };
		AddMob(Manager, TEXT("minecraft:blaze"), FVector(10.5, 3.0, 10.5));
		TArray<int32> FireTicks;
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			const int32 Before = Manager.GetEntities().Num();
			Manager.Tick();
			for (int32 Index = Before; Index < Manager.GetEntities().Num(); ++Index)
			{
				if (Manager.GetEntities()[Index]->GetType() == FMcFireball::SmallType())
				{
					FireTicks.Add(Tick);
				}
			}
		}
		TestTrue(TEXT("two bursts of three at 178 apart"), FireTicks.Num() >= 4 && FireTicks[1] - FireTicks[0] == 6 && FireTicks[2] - FireTicks[1] == 6 && FireTicks[3] - FireTicks[0] == 178);
	}
	for (const TCHAR* Kind : { TEXT("minecraft:strider"), TEXT("minecraft:endermite"), TEXT("minecraft:shulker"), TEXT("minecraft:guardian"), TEXT("minecraft:phantom"), TEXT("minecraft:hoglin") })
	{
		// Each Nether, End and sea mob stays in the world for ten seconds beside a player (none is removed or sent out of the world on its own).
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.Difficulty = 2;
		Manager.PlayerPositions = { FVector(10.5, 1.0, 20.5) };
		FMcMob* Mob = AddMob(Manager, Kind, FVector(10.5, 1.0, 10.5));
		const int32 Id = Mob->Id;
		for (int32 Tick = 0; Tick < 200; ++Tick)
		{
			Manager.Tick();
		}
		bool bFound = false;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			bFound |= Entity->Id == Id;
		}
		TestTrue(FString::Printf(TEXT("%s is still there after 200 ticks"), Kind), bFound);
	}
	{
		// A phantom circles above a player and dives at it: over 900 ticks it passes within 3 blocks at least twice and climbs above 6.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFlatWorld(World, Stone);
		FMcEntityManager Manager(World);
		Manager.Difficulty = 2;
		const FVector Player(10.5, 1.0, 10.5);
		Manager.PlayerPositions = { Player };
		FMcMob* Phantom = AddMob(Manager, TEXT("minecraft:phantom"), FVector(10.5, 9.0, 0.5));
		int32 Passes = 0;
		bool bNear = false;
		double Highest = 0.0;
		for (int32 Tick = 0; Tick < 900; ++Tick)
		{
			Manager.Tick();
			const double Distance = FVector::Dist(Phantom->Body.Position, Player + FVector(0, 0.9, 0));
			Highest = FMath::Max(Highest, Phantom->Body.Position.Y - Player.Y);
			if (Distance < 3.0 && !bNear)
			{
				++Passes;
			}
			bNear = Distance < 3.0;
		}
		TestTrue(FString::Printf(TEXT("a phantom dives at least twice in 900 ticks (%d) and climbs (%.1f)"), Passes, Highest), Passes >= 2 && Highest > 6.0);
	}
	return true;
}

#endif
