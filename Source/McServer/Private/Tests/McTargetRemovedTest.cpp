#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Mobs keep other entities by id, not by pointer: a target, a leash holder or a prey that is gone mid-chase must not be
// touched. These run on whatever the removal does to the memory, so a stale pointer shows up as a crash or a wrong result.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTargetRemovedTest, "McClient.McServer.TargetRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcTargetRemovedTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 ChunkX = 0; ChunkX < 2; ++ChunkX)
	{
		for (int32 ChunkZ = 0; ChunkZ < 2; ++ChunkZ)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(ChunkX, ChunkZ)));
		}
	}
	for (int32 X = 0; X < 32; ++X)
	{
		for (int32 Z = 0; Z < 32; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
		}
	}
	FMcEntityHost Host(World, 1);
	FMcMob* Golem = Host.SpawnMob(TEXT("minecraft:iron_golem"), FVector(10.5, 1.0, 10.5));
	FMcMob* Zombie = Host.SpawnMob(TEXT("minecraft:zombie"), FVector(18.5, 1.0, 10.5));
	FMcMob* Villager = Host.SpawnMob(TEXT("minecraft:villager"), FVector(10.5, 1.0, 20.5));
	FMcMob* Trader = Host.SpawnMob(TEXT("minecraft:wandering_trader"), FVector(20.5, 1.0, 20.5));
	FMcMob* Llama = Host.SpawnMob(TEXT("minecraft:llama"), FVector(24.5, 1.0, 20.5));
	if (!Golem || !Zombie || !Villager || !Trader || !Llama)
	{
		return false;
	}
	const int32 GolemId = Golem->Id;
	const int32 ZombieId = Zombie->Id;
	Llama->LeashHolderId = Trader->Id;
	Zombie->bFrozen = true;
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Host.GetManager().Tick();
	}
	// The golem is on its way to the zombie, the zombie goes after the villager; take away the zombie, the trader and the villager mid-chase.
	Host.GetManager().Find(ZombieId)->bRemoved = true;
	Trader->bRemoved = true;
	Villager->bRemoved = true;
	for (int32 Tick = 0; Tick < 200; ++Tick)
	{
		Host.GetManager().Tick();
	}
	TestNotNull(TEXT("the golem is still about"), Host.GetManager().Find(GolemId));
	TestEqual(TEXT("the leashed llama let go of its gone holder"), static_cast<FMcMob*>(Host.GetManager().Find(Llama->Id))->LeashHolderId, 0);
	return true;
}

#endif
