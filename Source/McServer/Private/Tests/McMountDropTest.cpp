// A mount a player has put a saddle on drops it when it dies (vehicle_log52: 15 of 16 saddled pigs and horses left a saddle).

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "McMount.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMountDropTest, "McClient.McServer.MountDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcMountDropTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	for (const TCHAR* Name : { TEXT("minecraft:pig"), TEXT("minecraft:horse") })
	{
		FMcBlockWorld World{FMcDimensionType()};
		World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
		FMcEntityHost Host(World, 1);
		FMcMob* Mount = Host.SpawnMob(FName(Name), FVector(8.5, 1.0, 8.5));
		if (!TestNotNull(Name, Mount))
		{
			continue;
		}
		Mount->bTamed = true;
		FMcItemStack Saddle(FName(TEXT("minecraft:saddle")), 1);
		TestEqual(TEXT("it takes the saddle"), McMount::Use(Host.GetManager(), *Mount, Saddle, 1, false, false), McMount::EUse::Saddled);
		Mount->Damage(1000.0f, nullptr);
		int32 Saddles = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Host.GetManager().GetEntities())
		{
			if (Entity->GetType() == FMcItemEntity::StaticType() && static_cast<const FMcItemEntity&>(*Entity).Stack.Item == TEXT("minecraft:saddle"))
			{
				++Saddles;
			}
		}
		TestEqual(*FString::Printf(TEXT("%s drops its saddle"), Name), Saddles, 1);
	}
	return true;
}

#endif
