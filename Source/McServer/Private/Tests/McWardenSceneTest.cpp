#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McGameEvent.h"
#include "McGameRules.h"
#include "McItemRegistry.h"
#include "McSculkMobs.h"
#include "McSimulation.h"
#include "McWarden.h"
#include "McCreaking.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// The sculk chain in one scene, with the real simulation and entity host joined by McSculkMobs: a player sets off a natural shrieker
// four times, a warden rises near it, hears the player's steps, roars, walks to them and hits. (Numbers are the wiki's; see PLAN.md.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWardenSceneTest, "McClient.McServer.WardenScene",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWardenSceneTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -2; Z <= 2; ++Z)
	{
		for (int32 X = -2; X <= 2; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	for (int32 X = -30; X <= 30; ++X)
	{
		for (int32 Z = -30; Z <= 30; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
		}
	}
	FMcSimulation Sim(World);
	FMcEntityHost Host(World, 5);
	McSculkMobs::Connect(Sim, Host, World);

	const int32 PlayerId = 7;
	FVector Player(4.5, 1.0, 0.5);
	Host.FindPlayerById = [&](int32 Id, FVector& Out) { Out = Player; return Id == PlayerId; };
	Host.PlayerIdAt = [&](const FVector& Feet) { return FVector::Dist(Feet, Player) < 0.5 ? PlayerId : 0; };
	TArray<float> Hits;
	Host.OnPlayerHit = [&](const FVector&, float Damage) { Hits.Add(Damage); };
	int32 Darkness = 0;
	Host.OnPlayerEffect = [&](const FVector&, FName Effect, int32, int32) { Darkness += Effect == FName(TEXT("darkness")); };
	FMcGameRules Rules;
	Rules.bDoMobSpawning = false;
	Host.SetRules(Rules);

	const FIntVector Shrieker(0, 1, 0);
	World.SetBlock(Shrieker, State(TEXT("minecraft:sculk_shrieker[can_summon=true,shrieking=false,waterlogged=false]")));
	int32 Night = 0;
	const auto Step = [&]()
	{
		Sim.Tick();
		Host.Tick({ Player }, 18000.0);
		++Night;
	};
	const auto FindWarden = [&]() -> FMcWarden*
	{
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (!Entity->bRemoved && Entity->GetType() == FMcWarden::StaticType())
			{
				return static_cast<FMcWarden*>(Entity.Get());
			}
		}
		return nullptr;
	};
	for (int32 Index = 0; Index < 12; ++Index) { Step(); }

	// Three warnings call nothing; the fourth does, when its shriek ends.
	for (int32 Warning = 1; Warning <= 4; ++Warning)
	{
		Sim.ShriekerTouched(Shrieker, PlayerId);
		for (int32 Index = 0; Index < 205; ++Index) { Step(); }
		TestEqual(*FString::Printf(TEXT("warning %d"), Warning), Sim.GetWarningLevel(PlayerId), Warning);
		if (Warning < 4)
		{
			TestNull(TEXT("no warden before the fourth"), FindWarden());
		}
	}
	TestTrue(TEXT("each shriek gave Darkness to the player in range"), Darkness >= 4);
	FMcWarden* Warden = FindWarden();
	if (!TestNotNull(TEXT("a warden rose"), Warden))
	{
		return false;
	}
	TestTrue(TEXT("within 5 blocks across of the shrieker"), FMath::Abs(Warden->Body.Position.X) <= 5.6 && FMath::Abs(Warden->Body.Position.Z) <= 5.6);
	TestTrue(TEXT("it is still emerging or has just risen"), Warden->GetState() == FMcWarden::EState::Emerging || Warden->GetState() == FMcWarden::EState::Idle);

	// It hears nothing while it rises, then the player's steps (a step is heard within 15 blocks).
	const FMcGameEventSource Walker = [&]() { FMcGameEventSource Source; Source.bPlayer = true; Source.PlayerId = PlayerId; return Source; }();
	const FIntVector PlayerCell(FMath::FloorToInt32(Player.X), 1, FMath::FloorToInt32(Player.Z));
	Sim.EmitGameEvent(TEXT("step"), PlayerCell, Walker);
	TestEqual(TEXT("deaf while emerging"), Warden->GetBrain().GetAngerAt(PlayerId), 0);
	for (int32 Index = 0; Index < McWarden::EmergeTicks + 5; ++Index) { Step(); }
	TestEqual(TEXT("risen"), static_cast<int32>(Warden->GetState() == FMcWarden::EState::Emerging), 0);

	bool bRoared = false;
	for (int32 Tick = 0; Tick < 1500 && Hits.IsEmpty(); ++Tick)
	{
		if (Tick % 41 == 0 && Tick < 200)
		{
			Sim.EmitGameEvent(TEXT("step"), PlayerCell, Walker);
		}
		Step();
		bRoared |= Warden->GetState() == FMcWarden::EState::Roaring;
	}
	TestTrue(TEXT("it heard the steps and turned hostile"), bRoared);
	TestFalse(TEXT("and it hit the player"), Hits.IsEmpty());
	if (!Hits.IsEmpty())
	{
		TestEqual(TEXT("30 on Normal"), Hits[0], 30.0f);
	}
	// A sneaking player is not heard at that frequency.
	FMcWardenBrain Quiet;
	FMcGameEventSource Sneak = Walker;
	Sneak.bSneaking = true;
	FMcGameEvent SneakStep;
	SneakStep.Name = TEXT("step");
	SneakStep.Frequency = 1;
	SneakStep.Pos = PlayerCell;
	SneakStep.Source = Sneak;
	TestFalse(TEXT("a sneaking step is not heard"), McGameEvents::WardenCanHear(SneakStep, FIntVector(0, 1, 0)));
	return true;
}

// The heart and its mob together: an awake heart raises a real linked creaking, a hit on it grows resin, the comparator follows it, and
// breaking the heart removes it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCreakingSceneTest, "McClient.McServer.CreakingScene",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcCreakingSceneTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -2; Z <= 2; ++Z)
	{
		for (int32 X = -2; X <= 2; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	for (int32 X = -30; X <= 30; ++X)
	{
		for (int32 Z = -30; Z <= 30; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
		}
	}
	FMcSimulation Sim(World);
	FMcEntityHost Host(World, 6);
	McSculkMobs::Connect(Sim, Host, World);
	FMcGameRules Rules;
	Rules.bDoMobSpawning = false;
	Host.SetRules(Rules);
	const FIntVector Heart(0, 1, 0);
	const auto HeartState = [&]() { return Registry.GetPropertyValue(World.GetBlock(Heart), TEXT("creaking_heart_state")).ToString(); };
	const auto Step = [&](int32 Count) { for (int32 Index = 0; Index < Count; ++Index) { Sim.Tick(); Host.Tick({}, 18000.0); } };
	const auto FindCreaking = [&]() -> FMcCreaking*
	{
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (!Entity->bRemoved && Entity->GetType() == FMcCreaking::StaticType())
			{
				return static_cast<FMcCreaking*>(Entity.Get());
			}
		}
		return nullptr;
	};
	Sim.SetDayTime(13000);
	World.SetBlock(Heart, State(TEXT("minecraft:creaking_heart[axis=y,creaking_heart_state=uprooted,natural=true]")));
	Step(30);
	TestEqual(TEXT("uprooted without logs"), HeartState(), FString(TEXT("uprooted")));
	TestNull(TEXT("no creaking"), FindCreaking());
	World.SetBlock(Heart + FIntVector(0, -1, 0), State(TEXT("minecraft:pale_oak_log[axis=y]")));
	World.SetBlock(Heart + FIntVector(0, 1, 0), State(TEXT("minecraft:pale_oak_log[axis=y]")));
	Step(100);
	TestEqual(TEXT("awake between logs at night"), HeartState(), FString(TEXT("awake")));
	FMcCreaking* Creaking = FindCreaking();
	if (!TestNotNull(TEXT("a creaking was raised"), Creaking))
	{
		return false;
	}
	TestTrue(TEXT("tied to the heart"), Creaking->IsLinked() && Creaking->GetHeart() == Heart);
	TestTrue(TEXT("within 16 across and 8 up and down"), FMath::Abs(Creaking->Body.Position.X) <= 17.0 && FMath::Abs(Creaking->Body.Position.Z) <= 17.0 && FMath::Abs(Creaking->Body.Position.Y - 1.0) <= 9.0);
	TestFalse(TEXT("invulnerable"), Creaking->Damage(10.0f, nullptr));
	TestEqual(TEXT("still full health"), Creaking->Health, Creaking->Type.MaxHealth);
	// Its hit reaches the heart and grows resin.
	Creaking->HurtTime = 0;
	const FVector From(0.0, 1.0, 0.0);
	Creaking->Damage(1.0f, &From);
	int32 Resin = 0;
	const FName ClumpName = Registry.GetBlockOf(Registry.FindDefaultState(TEXT("minecraft:resin_clump")).Get(0)).Name;
	for (int32 Y = -3; Y <= 3; ++Y)
		for (int32 X = -4; X <= 4; ++X)
			for (int32 Z = -4; Z <= 4; ++Z)
			{
				const FMcStateId At = World.GetBlock(Heart + FIntVector(X, Y, Z));
				Resin += Registry.IsValidState(At) && Registry.GetBlockOf(At).Name == ClumpName;
			}
	TestEqual(TEXT("a hit grew resin on a log"), Resin, 1);
	// The distance the comparator reads is the creaking's.
	const double Distance = Creaking->GetHeartDistance();
	TestTrue(TEXT("a distance"), Distance >= 0.0 && Distance <= 32.0);
	// Day: it goes and the heart sleeps; night again: a new one.
	Sim.SetDayTime(1000);
	Step(60);
	TestNull(TEXT("gone at daybreak"), FindCreaking());
	TestEqual(TEXT("dormant by day"), HeartState(), FString(TEXT("dormant")));
	Sim.SetDayTime(13000);
	Step(100);
	TestNotNull(TEXT("a new one the next night"), FindCreaking());
	World.SetBlock(Heart, State(TEXT("minecraft:air")));
	Step(2);
	TestNull(TEXT("breaking the heart removes it"), FindCreaking());
	return true;
}

#endif
