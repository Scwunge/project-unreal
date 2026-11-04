#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEntityHost.h"
#include "McEntityManager.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "McWorldActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// A piston that extends shoves an item in its head's cell along with the head, half a block a tick for two ticks, and
// lifts what lies on a block it raises; one beside the arm stays (reference run pushent).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPistonPushTest, "McClient.McWorld.PistonPushesEntities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPistonPushTest::RunTest(const FString& Parameters)
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
	if (!TestNotNull(TEXT("a world actor"), Actor) || !TestNotNull(TEXT("a block world"), Actor ? Actor->GetBlockWorld() : nullptr)
		|| !TestNotNull(TEXT("an entity host"), Actor->GetEntityHost()) || !TestNotNull(TEXT("a simulation"), Actor->GetSimulation()))
	{
		Finish();
		return false;
	}
	FMcBlockWorld& Blocks = *Actor->GetBlockWorld();
	FMcSimulation& Sim = *Actor->GetSimulation();
	if (!Blocks.HasChunk(FIntPoint(0, 0)))
	{
		Blocks.AddChunk(Blocks.MakeChunk(FIntPoint(0, 0)));
	}
	const auto Step = [&](int32 Ticks)
	{
		for (int32 Index = 0; Index < Ticks; ++Index)
		{
			Sim.Tick();
			Actor->MovePushedEntities();
		}
	};
	for (int32 X = 0; X < 12; ++X)
	{
		for (int32 Z = 0; Z < 6; ++Z)
		{
			Blocks.SetBlock(FIntVector(X, 63, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
		}
	}
	// A piston facing east at x=3, z=1 with a lever-free power block under its tail; an item lies in the head's cell.
	const FIntVector Piston(3, 64, 1);
	Blocks.SetBlock(Piston, Registry.ParseState(TEXT("minecraft:piston[facing=east]")).Get(0));
	FMcItemEntity& Lying = Actor->GetEntityHost()->GetManager().DropItem(FVector(4.5, 64.0, 1.5), FMcItemStack(TEXT("minecraft:apple"), 1));
	Lying.Body.Position = FVector(4.5, 64.0, 1.5);
	Lying.Body.Velocity = FVector::ZeroVector;
	// A second beside the arm's lane stays put.
	FMcItemEntity& Beside = Actor->GetEntityHost()->GetManager().DropItem(FVector(4.5, 64.0, 3.5), FMcItemStack(TEXT("minecraft:apple"), 1));
	Beside.Body.Position = FVector(4.5, 64.0, 3.5);
	Beside.Body.Velocity = FVector::ZeroVector;
	Blocks.SetBlock(FIntVector(3, 64, 0), Registry.ParseState(TEXT("minecraft:redstone_block")).Get(0));
	Step(8);
	TestTrue(TEXT("the piston extended"), Registry.GetBlockOf(Blocks.GetBlock(FIntVector(4, 64, 1))).Name == TEXT("minecraft:piston_head"));
	TestTrue(TEXT("the item was carried to the next cell"), Lying.Body.Position.X > 5.0);
	TestEqual(TEXT("and no further than a block"), Lying.Body.Position.X, 5.5, 0.1);
	TestEqual(TEXT("it stays in its row"), Lying.Body.Position.Z, 1.5, 0.01);
	TestEqual(TEXT("the other item stays"), Beside.Body.Position.X, 4.5, 0.01);

	Finish();
	return true;
}

#endif
