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
		// A dead mob is taken out of the world, so it is looked up by id each tick rather than held by pointer.
		const int32 ZombieId = Zombie->Id;
		bool bZombieGone = false;
		for (int32 Tick = 0; Tick < 400 && !bZombieGone; ++Tick)
		{
			if (FMcEntity* Found = Host.GetManager().Find(ZombieId))
			{
				static_cast<FMcMob*>(Found)->HurtTime = 0;
			}
			Host.GetManager().Tick();
			const FMcEntity* Found = Host.GetManager().Find(ZombieId);
			bZombieGone = !Found || Found->bRemoved || static_cast<const FMcMob*>(Found)->Health <= 0.0f;
		}
		TestTrue(TEXT("the zombie is dead"), bZombieGone);
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
	{
		// A wandering trader comes with two llamas on leads, which follow it.
		FMcBlockWorld World{FMcDimensionType()};
		// The trader comes 24 to 48 blocks off, so the ground has to reach that far.
		for (int32 ChunkX = 0; ChunkX < 8; ++ChunkX)
		{
			for (int32 ChunkZ = 0; ChunkZ < 8; ++ChunkZ)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(ChunkX, ChunkZ)));
			}
		}
		for (int32 X = 0; X < 128; ++X)
		{
			for (int32 Z = 0; Z < 128; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
			}
		}
		FMcEntityHost Host(World, 1);
		TestTrue(TEXT("the trader arrives"), Host.SpawnWanderingTrader(FVector(64.0, 1.0, 64.0)));
		TestEqual(TEXT("one trader"), Count(Host, TEXT("minecraft:wandering_trader")), 1);
		TestEqual(TEXT("two llamas"), Count(Host, TEXT("minecraft:trader_llama")), 2);
		int32 Leashed = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (Entity->GetType() == FName(TEXT("minecraft:trader_llama")) && static_cast<FMcMob&>(*Entity).LeashHolderId != 0)
			{
				++Leashed;
			}
		}
		TestEqual(TEXT("both on leads"), Leashed, 2);
	}
	{
		// A villager shuts an open wooden door at night, both halves, and leaves an iron one alone.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		Host.GetManager().OnSetBlock = [&World](const FIntVector& Pos, FMcStateId State) { World.SetBlock(Pos, State); };
		Host.GetManager().bDaylight = false;
		const auto Put = [&](const FIntVector& Cell, const TCHAR* State)
		{
			World.SetBlock(Cell, Registry.ParseState(State).Get(0));
		};
		Put(FIntVector(5, 1, 5), TEXT("minecraft:oak_door[facing=north,half=lower,hinge=left,open=true,powered=false]"));
		Put(FIntVector(5, 2, 5), TEXT("minecraft:oak_door[facing=north,half=upper,hinge=left,open=true,powered=false]"));
		Put(FIntVector(8, 1, 5), TEXT("minecraft:iron_door[facing=north,half=lower,hinge=left,open=true,powered=false]"));
		Host.SpawnMob(TEXT("minecraft:villager"), FVector(6.5, 1.0, 6.5));
		for (int32 Tick = 0; Tick < 400; ++Tick)
		{
			Host.GetManager().Tick();
		}
		TestEqual(TEXT("the lower half is shut"), Registry.GetPropertyValue(World.GetBlock(FIntVector(5, 1, 5)), TEXT("open")), FName(TEXT("false")));
		TestEqual(TEXT("the upper half is shut"), Registry.GetPropertyValue(World.GetBlock(FIntVector(5, 2, 5)), TEXT("open")), FName(TEXT("false")));
		TestEqual(TEXT("the iron door stays open"), Registry.GetPropertyValue(World.GetBlock(FIntVector(8, 1, 5)), TEXT("open")), FName(TEXT("true")));
	}
	{
		// Two villagers that ate enough, with a bed to spare, have a baby; the baby runs after them and grows up.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		Host.GetManager().OnVillagerBred = [&Host](const FVector& Where, FMcMob& Parent)
		{
			if (FMcMob* Made = Host.SpawnMob(FMcVillager::StaticType(), Where))
			{
				static_cast<FMcVillager&>(*Made).SetBaby(true);
			}
		};
		World.SetBlock(FIntVector(12, 1, 12), Registry.ParseState(TEXT("minecraft:red_bed[facing=north,occupied=false,part=foot]")).Get(0));
		FMcMob* First = Host.SpawnMob(TEXT("minecraft:villager"), FVector(10.5, 1.0, 10.5));
		FMcMob* Second = Host.SpawnMob(TEXT("minecraft:villager"), FVector(11.5, 1.0, 10.5));
		if (!First || !Second)
		{
			return false;
		}
		static_cast<FMcVillager*>(First)->SetFood(12);
		static_cast<FMcVillager*>(Second)->SetFood(12);
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Host.GetManager().Tick();
		}
		int32 Babies = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			Babies += (!Entity->bRemoved && Entity->GetType() == FMcVillager::StaticType() && static_cast<FMcVillager&>(*Entity).IsBaby()) ? 1 : 0;
		}
		TestEqual(TEXT("one baby is born"), Babies, 1);
		TestEqual(TEXT("the parents spent their food"), static_cast<FMcVillager*>(First)->GetFood(), 0);
		TestFalse(TEXT("and are not willing again at once"), static_cast<FMcVillager*>(First)->IsWilling());
	}
	{
		// Bread lying near a villager is eaten.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		FMcMob* Villager = Host.SpawnMob(TEXT("minecraft:villager"), FVector(10.5, 1.0, 10.5));
		if (!Villager)
		{
			return false;
		}
		Host.GetManager().DropItem(FVector(10.5, 1.0, 10.5), FMcItemStack(TEXT("minecraft:bread"), 3), 0);
		for (int32 Tick = 0; Tick < 60; ++Tick)
		{
			Host.GetManager().Tick();
		}
		TestEqual(TEXT("three loaves are twelve points"), static_cast<FMcVillager*>(Villager)->GetFood(), 12);
	}
	{
		// A golem holds a poppy out to a child villager near it.
		FMcBlockWorld World{FMcDimensionType()};
		MakeFloor(World);
		FMcEntityHost Host(World, 1);
		FMcMob* Golem = Host.SpawnMob(TEXT("minecraft:iron_golem"), FVector(10.5, 1.0, 10.5));
		FMcMob* Child = Host.SpawnMob(TEXT("minecraft:villager"), FVector(13.5, 1.0, 10.5));
		if (!Golem || !Child)
		{
			return false;
		}
		static_cast<FMcVillager*>(Child)->SetBaby(true);
		Child->bFrozen = true;
		int32 MostOffered = 0;
		for (int32 Tick = 0; Tick < 1500; ++Tick)
		{
			Host.GetManager().Tick();
			MostOffered = FMath::Max(MostOffered, Golem->OfferTicks);
		}
		TestTrue(TEXT("the golem offers a poppy"), MostOffered > 0);
	}
	return true;
}

#endif
