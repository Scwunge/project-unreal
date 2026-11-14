// Two worlds (the Overworld and the Nether) in one test world, and what the travel code does with riders, vehicles and
// the animals aboard them when they go through a Nether portal: a ridden horse, a cart with a pig, a boat with a cow.
// No game mode is there, so the subsystem is given the two worlds by name (UMcTravelSubsystem::WorldOverride).

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEntityHost.h"
#include "McEntityManager.h"
#include "McItemRegistry.h"
#include "McMob.h"
#include "McPlayerController.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "McPortal.h"
#include "McTravel.h"
#include "McVehicle.h"
#include "McWorldActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPortalTravelTest, "McClient.McEntity.PortalTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// The first live entity whose type name contains Text, or null.
	FMcEntity* FindByType(FMcEntityHost& Host, const TCHAR* Text)
	{
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (!Entity->bRemoved && Entity->GetType().ToString().Contains(Text))
			{
				return Entity.Get();
			}
		}
		return nullptr;
	}

	// Whether a portal block stands within Reach cells of Where.
	bool PortalNear(const FMcBlockWorld& Blocks, const FVector& Where, int32 Reach)
	{
		const FIntVector Cell(FMath::FloorToInt32(Where.X), FMath::FloorToInt32(Where.Y), FMath::FloorToInt32(Where.Z));
		for (int32 Y = -Reach; Y <= Reach; ++Y)
		{
			for (int32 Z = -Reach; Z <= Reach; ++Z)
			{
				for (int32 X = -Reach; X <= Reach; ++X)
				{
					if (McPortal::IsPortalBlock(Blocks.GetBlock(Cell + FIntVector(X, Y, Z))))
					{
						return true;
					}
				}
			}
		}
		return false;
	}
}

bool FMcPortalTravelTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()) || !GEngine)
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AMcWorldActor* Over = World->SpawnActor<AMcWorldActor>();
	AMcWorldActor* Nether = nullptr;
	const auto Finish = [&]()
	{
		if (Nether)
		{
			Nether->Destroy();
		}
		if (Over)
		{
			Over->Destroy();
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	if (Over && !Over->HasActorBegunPlay())
	{
		Over->DispatchBeginPlay();
	}
	if (!TestNotNull(TEXT("an overworld"), Over) || !TestNotNull(TEXT("with blocks"), Over ? Over->GetBlockWorld() : nullptr) || !TestNotNull(TEXT("and an entity host"), Over->GetEntityHost()))
	{
		Finish();
		return false;
	}
	Nether = World->SpawnActorDeferred<AMcWorldActor>(AMcWorldActor::StaticClass(), FTransform::Identity);
	if (Nether)
	{
		Nether->Dimension = TEXT("minecraft:the_nether");
		Nether->NoiseSettings = TEXT("minecraft:nether");
		Nether->InheritFrom(*Over);
		Nether->FinishSpawning(FTransform::Identity);
	}
	if (!TestNotNull(TEXT("a nether"), Nether) || !TestNotNull(TEXT("with blocks"), Nether ? Nether->GetBlockWorld() : nullptr) || !TestNotNull(TEXT("and an entity host"), Nether->GetEntityHost()))
	{
		Finish();
		return false;
	}
	if (!Nether->HasActorBegunPlay())
	{
		Nether->DispatchBeginPlay();
	}
	UMcTravelSubsystem* Travel = World->GetSubsystem<UMcTravelSubsystem>();
	if (!TestNotNull(TEXT("the travel subsystem"), Travel))
	{
		Finish();
		return false;
	}
	Travel->WorldOverride = [Over, Nether](const FString& Dimension) -> AMcWorldActor*
	{
		return Dimension == Nether->Dimension ? Nether : Dimension == Over->Dimension ? Over : nullptr;
	};

	// The Overworld: stone floor, and a portal of two by three at x 2-3, z 0, running along x. The Nether has the chunks round where
	// the portal leads (chunk 0,0, a radius of 2) and nothing else, so one is built there.
	FMcBlockWorld& OverBlocks = *Over->GetBlockWorld();
	FMcBlockWorld& NetherBlocks = *Nether->GetBlockWorld();
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			if (!OverBlocks.HasChunk(FIntPoint(X, Z)))
			{
				OverBlocks.AddChunk(OverBlocks.MakeChunk(FIntPoint(X, Z)));
			}
		}
	}
	for (int32 Z = -2; Z <= 2; ++Z)
	{
		for (int32 X = -2; X <= 2; ++X)
		{
			if (!NetherBlocks.HasChunk(FIntPoint(X, Z)))
			{
				NetherBlocks.AddChunk(NetherBlocks.MakeChunk(FIntPoint(X, Z)));
			}
		}
	}
	for (int32 X = -16; X <= 31; ++X)
	{
		for (int32 Z = -16; Z <= 31; ++Z)
		{
			OverBlocks.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
		}
	}
	for (int32 X = 1; X <= 4; ++X)
	{
		OverBlocks.SetBlock(FIntVector(X, 0, 0), State(TEXT("minecraft:obsidian")));
		OverBlocks.SetBlock(FIntVector(X, 4, 0), State(TEXT("minecraft:obsidian")));
	}
	for (int32 Y = 1; Y <= 3; ++Y)
	{
		OverBlocks.SetBlock(FIntVector(1, Y, 0), State(TEXT("minecraft:obsidian")));
		OverBlocks.SetBlock(FIntVector(4, Y, 0), State(TEXT("minecraft:obsidian")));
		OverBlocks.SetBlock(FIntVector(2, Y, 0), State(TEXT("minecraft:nether_portal[axis=x]")));
		OverBlocks.SetBlock(FIntVector(3, Y, 0), State(TEXT("minecraft:nether_portal[axis=x]")));
	}
	TestTrue(TEXT("the portal block is one"), McPortal::IsPortalBlock(OverBlocks.GetBlock(FIntVector(2, 1, 0))));
	FMcEntityHost& OverHost = *Over->GetEntityHost();
	FMcEntityHost& NetherHost = *Nether->GetEntityHost();

	// What is left on either side is cleared between the cases.
	const auto Clear = [&]()
	{
		for (FMcEntityHost* Host : { &OverHost, &NetherHost })
		{
			for (const TUniquePtr<FMcEntity>& Entity : Host->GetManager().GetEntities())
			{
				Entity->bRemoved = true;
			}
			Host->GetManager().Tick();
		}
	};

	// ---- A cart with a pig in it, nobody else: both go, the pig boards the cart again in the Nether ----
	{
		FMcMinecartEntity* Cart = new FMcMinecartEntity();
		Cart->Body.Position = FVector(2.5, 1.0, 0.5);
		OverHost.GetManager().Add(TUniquePtr<FMcEntity>(Cart));
		FMcMob* Pig = OverHost.SpawnMob(TEXT("minecraft:pig"), FVector(2.5, 1.0, 0.5));
		if (TestNotNull(TEXT("a pig"), Pig))
		{
			for (int32 Tick = 0; Tick < 3 && Pig->RidingId == 0; ++Tick)
			{
				OverHost.GetManager().Tick();
			}
			TestEqual(TEXT("the pig sits in the cart before it goes"), Pig->RidingId, Cart->Id);
			Cart->Body.Position = FVector(2.5, 1.0, 0.5);
			Cart->Body.Velocity = FVector::ZeroVector;
			Cart->PortalCooldown = 0;
			Travel->StepEntities();
			TestNull(TEXT("no cart left in the Overworld"), FindByType(OverHost, TEXT("minecart")));
			TestNull(TEXT("no pig left in the Overworld"), FindByType(OverHost, TEXT("pig")));
			FMcEntity* NetherCart = FindByType(NetherHost, TEXT("minecart"));
			FMcEntity* NetherPig = FindByType(NetherHost, TEXT("pig"));
			if (TestNotNull(TEXT("a cart in the Nether"), NetherCart) && TestNotNull(TEXT("and a pig"), NetherPig))
			{
				TestTrue(TEXT("the cart is set down by a portal"), PortalNear(NetherBlocks, NetherCart->Body.Position, 3));
				TestTrue(TEXT("the pig is beside it"), FVector::Dist(NetherCart->Body.Position, NetherPig->Body.Position) < 2.0);
				TestNotEqual(TEXT("with new ids"), NetherCart->Id, NetherPig->Id);
				TestEqual(TEXT("the cart keeps still"), NetherCart->Body.Velocity.Size(), 0.0, 1e-6);
				for (int32 Tick = 0; Tick < 5 && static_cast<FMcMob*>(NetherPig)->RidingId == 0; ++Tick)
				{
					NetherHost.GetManager().Tick();
				}
				TestEqual(TEXT("the pig boards the cart again"), static_cast<FMcMob*>(NetherPig)->RidingId, NetherCart->Id);
				TestEqual(TEXT("and the cart counts it"), static_cast<FMcVehicle*>(NetherCart)->GetPassengerCount(), 1);
				TestTrue(TEXT("neither goes back through at once"), NetherCart->PortalCooldown > 0);
			}
		}
		Clear();
	}

	// ---- A boat with a cow ----
	{
		FMcBoatEntity* Boat = new FMcBoatEntity();
		Boat->Body.Position = FVector(3.5, 1.0, 0.5);
		OverHost.GetManager().Add(TUniquePtr<FMcEntity>(Boat));
		FMcMob* Cow = OverHost.SpawnMob(TEXT("minecraft:cow"), FVector(3.5, 1.0, 0.5));
		if (TestNotNull(TEXT("a cow"), Cow))
		{
			for (int32 Tick = 0; Tick < 3 && Cow->RidingId == 0; ++Tick)
			{
				OverHost.GetManager().Tick();
			}
			TestEqual(TEXT("the cow sits in the boat before it goes"), Cow->RidingId, Boat->Id);
			Boat->Body.Position = FVector(3.5, 1.0, 0.5);
			Boat->PortalCooldown = 0;
			Travel->StepEntities();
			TestNull(TEXT("no boat left in the Overworld"), FindByType(OverHost, TEXT("boat")));
			TestNull(TEXT("no cow left in the Overworld"), FindByType(OverHost, TEXT("cow")));
			FMcEntity* NetherBoat = FindByType(NetherHost, TEXT("boat"));
			FMcEntity* NetherCow = FindByType(NetherHost, TEXT("cow"));
			if (TestNotNull(TEXT("a boat in the Nether"), NetherBoat) && TestNotNull(TEXT("and a cow"), NetherCow))
			{
				TestTrue(TEXT("the boat is set down by a portal"), PortalNear(NetherBlocks, NetherBoat->Body.Position, 3));
				for (int32 Tick = 0; Tick < 5 && static_cast<FMcMob*>(NetherCow)->RidingId == 0; ++Tick)
				{
					NetherHost.GetManager().Tick();
				}
				TestEqual(TEXT("the cow boards the boat again"), static_cast<FMcMob*>(NetherCow)->RidingId, NetherBoat->Id);
			}
		}
		Clear();
	}

	// ---- A player on a horse: the horse goes with them and they sit on it ----
	{
		AMcPlayerController* Controller = World->SpawnActor<AMcPlayerController>();
		AMcPlayerPawn* Pawn = World->SpawnActor<AMcPlayerPawn>();
		if (TestNotNull(TEXT("a controller"), Controller) && TestNotNull(TEXT("a pawn"), Pawn) && TestNotNull(TEXT("its data"), Pawn ? Pawn->Data.Get() : nullptr))
		{
			Controller->Possess(Pawn);
			Controller->EnterWorld(*Over);
			Controller->SetUpLocalPawn(*Pawn);
			Pawn->ServerTeleport(FVector(2.5, 1.0, 0.5), 0.0f);
			FMcMob* Horse = OverHost.SpawnMob(TEXT("minecraft:horse"), FVector(2.5, 1.0, 0.5));
			if (TestNotNull(TEXT("a horse"), Horse))
			{
				Horse->Body.Position = FVector(2.5, 1.0, 0.5);
				Pawn->ServerRide(Horse->Id);
				TestEqual(TEXT("the player rides it"), Pawn->GetRidingId(), Horse->Id);
				TestTrue(TEXT("the trip starts"), Travel->StartPortalTrip(*Controller, FIntVector(2, 1, 0)));
				TestTrue(TEXT("the player is on a trip"), Travel->IsOnTrip(Controller));
				TestNull(TEXT("the horse is out of the Overworld"), FindByType(OverHost, TEXT("horse")));
				OverHost.GetManager().Tick();
				Travel->TickTrips();
				TestFalse(TEXT("the trip is done"), Travel->IsOnTrip(Controller));
				TestTrue(TEXT("the player is in the Nether"), Controller->GetWorldActor() == Nether);
				FMcEntity* NetherHorse = FindByType(NetherHost, TEXT("horse"));
				if (TestNotNull(TEXT("a horse in the Nether"), NetherHorse))
				{
					TestEqual(TEXT("the player rides it"), Pawn->GetRidingId(), NetherHorse->Id);
					TestTrue(TEXT("by a portal"), PortalNear(NetherBlocks, NetherHorse->Body.Position, 3));
					TestTrue(TEXT("with the player at it"), FVector::Dist(Pawn->GetFeetPosition(), NetherHorse->Body.Position) < 1.5);
					TestEqual(TEXT("and it has no speed"), NetherHorse->Body.Velocity.Size(), 0.0, 1e-6);
					TestEqual(TEXT("or a cooldown to wait out"), NetherHorse->PortalCooldown, 300);
				}
			}
		}
		Clear();
	}

	Finish();
	return true;
}

#endif
