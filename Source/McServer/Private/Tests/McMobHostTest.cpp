#include "McArrow.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMobHostTest, "McClient.McServer.MobsSlimesAndArrows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcMobHostTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	for (int32 X = 0; X < 16; ++X)
	{
		for (int32 Z = 0; Z < 16; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0));
		}
	}
	FMcEntityHost Host(World, 1);
	FMcEntityManager& Manager = Host.GetManager();

	// A large slime and an arrow in the chunk; a save keeps the slime, with its size, and not the arrow.
	FMcMob* Original = new FMcMob(*FMcMobType::Find(TEXT("minecraft:slime")));
	Original->SetSize(4);
	Original->Body.Position = FVector(8.5, 1.0, 8.5);
	Manager.Add(TUniquePtr<FMcEntity>(Original));
	TUniquePtr<FMcArrowEntity> Arrow = MakeUnique<FMcArrowEntity>();
	Arrow->Body.Position = FVector(4.5, 3.0, 4.5);
	Manager.Add(MoveTemp(Arrow));
	const TArray<uint8> Bytes = Host.CaptureChunk(FIntPoint(0, 0));
	Original->bRemoved = true;
	TestEqual(TEXT("the save holds the slime and not the arrow"), Host.RestoreChunk(Bytes), 1);

	const auto Alive = [&Manager]()
	{
		TArray<FMcMob*> Mobs;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (Entity->IsLiving() && !Entity->bRemoved)
			{
				Mobs.Add(static_cast<FMcMob*>(Entity.Get()));
			}
		}
		return Mobs;
	};
	TArray<FMcMob*> Mobs = Alive();
	if (!TestEqual(TEXT("one slime came back"), Mobs.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("still a large one"), Mobs[0]->Size, 4);
	TestTrue(TEXT("with the box of one"), FMath::IsNearlyEqual(Mobs[0]->Body.Width, 2.08f, 0.001f));
	TestTrue(TEXT("and its health"), FMath::IsNearlyEqual(Mobs[0]->Health, 16.0f));

	// Killing it splits it into two to four medium ones, and those into small ones.
	Mobs[0]->Damage(100.0f, nullptr);
	TArray<FMcMob*> Medium = Alive();
	TestTrue(TEXT("a large slime splits into 2 to 4"), Medium.Num() >= 2 && Medium.Num() <= 4);
	bool bAllMedium = true;
	for (FMcMob* Mob : Medium)
	{
		bAllMedium &= Mob->Size == 2 && FMath::IsNearlyEqual(Mob->Health, 4.0f);
	}
	TestTrue(TEXT("all medium, with 4 health"), bAllMedium);
	if (!Medium.IsEmpty())
	{
		Medium[0]->Damage(100.0f, nullptr);
		TArray<FMcMob*> Small = Alive();
		const int32 Fresh = Small.Num() - (Medium.Num() - 1);
		TestTrue(TEXT("a medium one splits into 2 to 4 small"), Fresh >= 2 && Fresh <= 4);
		int32 NumSmall = 0;
		for (FMcMob* Mob : Small)
		{
			NumSmall += Mob->Size == 1 ? 1 : 0;
		}
		TestEqual(TEXT("of size 1"), NumSmall, Fresh);
		if (NumSmall > 0)
		{
			for (FMcMob* Mob : Small)
			{
				if (Mob->Size == 1)
				{
					Mob->Damage(100.0f, nullptr);
					break;
				}
			}
			TestEqual(TEXT("a small one doesn't split"), Alive().Num(), Small.Num() - 1);
		}
	}

	// Creepers, endermen and slimes count as hostile for the caps and for despawning far from players.
	TestTrue(TEXT("a creeper is hostile"), FMcMobType::Find(TEXT("minecraft:creeper"))->IsHostile());
	TestTrue(TEXT("a slime is hostile"), FMcMobType::Find(TEXT("minecraft:slime"))->IsHostile());
	TestFalse(TEXT("a pig isn't"), FMcMobType::Find(TEXT("minecraft:pig"))->IsHostile());
	return true;
}

#endif
