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
#include "McWorldActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Clicks on an animal in single player: the host's own pawn gets the server hooks first (EnterWorld), and the client hooks (the attack that the click
// calls) must still be set after them. They were not (they waited for a pawn without a block world), so a click never reached the cow. Then bare-hand
// swings, each after the recharge, kill the cow (10 health) and it drops its items.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcClickKillTest, "McClient.McPlayer.ClickKillsAnimal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcClickKillTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()) || !GEngine)
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AMcWorldActor* Actor = World->SpawnActor<AMcWorldActor>();
	const auto Finish = [&]()
	{
		if (Actor)
		{
			Actor->Destroy();
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	if (Actor && !Actor->HasActorBegunPlay())
	{
		Actor->DispatchBeginPlay();
	}
	if (!TestNotNull(TEXT("a world actor"), Actor) || !TestNotNull(TEXT("a block world"), Actor ? Actor->GetBlockWorld() : nullptr) || !TestNotNull(TEXT("an entity host"), Actor->GetEntityHost()))
	{
		Finish();
		return false;
	}
	FMcBlockWorld& Blocks = *Actor->GetBlockWorld();
	if (!Blocks.HasChunk(FIntPoint(0, 0)))
	{
		Blocks.AddChunk(Blocks.MakeChunk(FIntPoint(0, 0)));
	}
	for (int32 X = 0; X < 12; ++X)
	{
		for (int32 Z = 0; Z < 12; ++Z)
		{
			Blocks.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:grass_block")).Get(0));
		}
	}
	AMcPlayerController* Controller = World->SpawnActor<AMcPlayerController>();
	AMcPlayerPawn* Pawn = World->SpawnActor<AMcPlayerPawn>();
	if (!TestNotNull(TEXT("a controller"), Controller) || !TestNotNull(TEXT("a pawn"), Pawn) || !TestNotNull(TEXT("its data"), Pawn ? Pawn->Data.Get() : nullptr))
	{
		Finish();
		return false;
	}
	Controller->Possess(Pawn);
	Controller->EnterWorld(*Actor);
	// What the controller's tick does for the local player.
	Controller->SetUpLocalPawn(*Pawn);
	TestTrue(TEXT("the click has somewhere to go after the server hooks came first"), static_cast<bool>(Pawn->AttackHandler));
	TestTrue(TEXT("and so has a block edit"), static_cast<bool>(Pawn->SetBlockHandler));

	// Survival first, then creative (a creative player's click kills and drops too), both bare-handed.
	for (const bool bCreative : { false, true })
	{
		Pawn->Data->bCreative = bCreative;
		Pawn->ServerTeleport(FVector(2.5, 1.0, 2.5), 0.0f);
		FMcEntityHost& Host = *Actor->GetEntityHost();
		FMcMob* Cow = Host.SpawnMob(TEXT("minecraft:cow"), FVector(2.5, 1.0, 4.0));
		if (!TestNotNull(TEXT("a cow"), Cow))
		{
			Finish();
			return false;
		}
		const int32 CowId = Cow->Id;
		// The player follows the cow as one does: stands 1.5 blocks off it and aims at its middle for every swing (it is knocked back and runs).
		// The pawn is not ticked here, so each swing is a tenth charged (damage 0.2 to 0.3 a bare-hand swing): many swings.
		int32 Swings = 0;
		for (; Swings < 400; ++Swings)
		{
			for (int32 Tick = 0; Tick < 12; ++Tick)
			{
				Host.GetManager().Tick();
			}
			FVector CowAt = FVector::ZeroVector;
			bool bAlive = false;
			for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
			{
				if (Entity->Id == CowId && !Entity->bRemoved)
				{
					bAlive = true;
					CowAt = Entity->Body.Position;
				}
			}
			if (!bAlive)
			{
				break;
			}
			Pawn->ServerTeleport(CowAt - FVector(0.0, 0.0, 1.5), 0.0f);
			const FVector Eye = Pawn->GetFeetPosition() + FVector(0.0, 1.62, 0.0);
			Pawn->ServerSwing((CowAt + FVector(0.0, 0.7, 0.0) - Eye).GetSafeNormal());
		}
		TestTrue(*FString::Printf(TEXT("the cow is dead after %d swings (%s)"), Swings, bCreative ? TEXT("creative") : TEXT("survival")), Swings < 400);
		int32 Drops = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			Drops += (!Entity->IsLiving() && !Entity->bRemoved && Entity->Id > CowId) ? 1 : 0;
		}
		TestTrue(TEXT("it dropped something on the ground"), Drops > 0);
	}
	Finish();
	return true;
}

#endif
