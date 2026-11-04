// Carrying an entity from one dimension's host to another's (portal travel): a cart with a furnace's fuel, a boat, a cow
// and a dropped item come out the other side as the same things, at the new place, with new ids.

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "McVehicle.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEntityTransferTest, "McClient.McServer.EntityTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcEntityTransferTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld From{FMcDimensionType()};
	FMcBlockWorld To{FMcDimensionType()};
	From.AddChunk(From.MakeChunk(FIntPoint(0, 0)));
	To.AddChunk(To.MakeChunk(FIntPoint(0, 0)));
	FMcEntityHost Source(From, 1);
	FMcEntityHost Dest(To, 2);

	FMcMinecartEntity* Cart = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Furnace);
	Cart->Body.Position = FVector(3.5, 1.0, 3.5);
	Cart->Fuel = 1234;
	Source.GetManager().Add(TUniquePtr<FMcEntity>(Cart));
	const TArray<uint8> Bytes = Source.PackEntity(*Cart);
	TestFalse(TEXT("a cart packs"), Bytes.IsEmpty());
	FMcEntity* Arrived = Dest.UnpackEntity(Bytes, FVector(9.5, 4.0, 9.5));
	if (TestNotNull(TEXT("it unpacks"), Arrived))
	{
		TestEqual(TEXT("still a furnace cart"), Arrived->GetType(), Cart->GetType());
		TestEqual(TEXT("with its fuel"), static_cast<FMcMinecartEntity*>(Arrived)->Fuel, 1234);
		TestEqual(TEXT("at the new place"), Arrived->Body.Position.X, 9.5);
		TestEqual(TEXT("in the new host"), Dest.GetManager().GetEntities().Num(), 1);
	}
	if (FMcMob* Cow = Source.SpawnMob(FName(TEXT("minecraft:cow")), FVector(5.5, 1.0, 5.5)))
	{
		Cow->Health = 7.0f;
		FMcEntity* Moved = Dest.UnpackEntity(Source.PackEntity(*Cow), FVector(2.5, 1.0, 2.5));
		if (TestNotNull(TEXT("a cow unpacks"), Moved))
		{
			TestEqual(TEXT("with its health"), static_cast<FMcMob*>(Moved)->Health, 7.0f);
		}
	}
	return true;
}

#endif
