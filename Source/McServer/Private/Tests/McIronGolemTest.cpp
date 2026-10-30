#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "McVillager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcIronGolemTest, "McClient.McServer.IronGolem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcIronGolemTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto Count = [](FMcEntityHost& Host, const TCHAR* Kind)
	{
		int32 Found = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			Found += (!Entity->bRemoved && Entity->GetType() == FName(Kind)) ? 1 : 0;
		}
		return Found;
	};
	const auto MakeFloor = [&](FMcBlockWorld& World)
	{
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
	};
	{
		// Three villagers living together call for a golem, and only one.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Host.SpawnMob(TEXT("minecraft:villager"), FVector(10.5 + Index, 1.0, 10.5));
		}
		for (int32 Tick = 0; Tick < 4000; ++Tick)
		{
			Host.GetManager().Tick();
		}
		TestEqual(TEXT("the village gets one golem"), Count(Host, TEXT("minecraft:iron_golem")), 1);
	}
	{
		// A golem kills a zombie that comes near, and a creeper is left alone.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		Host.SpawnMob(TEXT("minecraft:iron_golem"), FVector(10.5, 1.0, 10.5));
		FMcMob* Zombie = Host.SpawnMob(TEXT("minecraft:zombie"), FVector(14.5, 1.0, 10.5));
		FMcMob* Creeper = Host.SpawnMob(TEXT("minecraft:creeper"), FVector(10.5, 1.0, 5.5));
		if (!Zombie || !Creeper)
		{
			return false;
		}
		Zombie->bFrozen = true;
		Creeper->bFrozen = true;
		for (int32 Tick = 0; Tick < 400; ++Tick)
		{
			Zombie->HurtTime = 0;
			Host.GetManager().Tick();
		}
		TestTrue(TEXT("the zombie is dead"), Zombie->Health <= 0.0f);
		TestTrue(TEXT("the creeper is untouched"), Creeper->Health >= Creeper->Type.MaxHealth);
	}
	{
		// A carved pumpkin on a T of iron blocks is a golem; without an arm it is not.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		const auto Put = [&](const FIntVector& Cell, const TCHAR* Block)
		{
			World.SetBlock(Cell, Registry.ParseState(Block).Get(0));
		};
		Put(FIntVector(5, 1, 5), TEXT("minecraft:iron_block"));
		Put(FIntVector(5, 2, 5), TEXT("minecraft:iron_block"));
		Put(FIntVector(4, 2, 5), TEXT("minecraft:iron_block"));
		Put(FIntVector(5, 3, 5), TEXT("minecraft:carved_pumpkin"));
		TArray<FIntVector> Blocks;
		FVector Feet;
		TestFalse(TEXT("one arm is not enough"), Host.MatchGolem(FIntVector(5, 3, 5), Blocks, Feet));
		Put(FIntVector(6, 2, 5), TEXT("minecraft:iron_block"));
		TestTrue(TEXT("two arms make a golem"), Host.MatchGolem(FIntVector(5, 3, 5), Blocks, Feet));
		TestEqual(TEXT("five blocks go"), Blocks.Num(), 5);
		TestEqual(TEXT("it stands on the lower block"), Feet.Y, 1.0);
	}
	return true;
}

#endif
