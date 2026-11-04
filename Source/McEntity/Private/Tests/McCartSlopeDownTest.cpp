// A cart down a slope onto flat rails (vehicle_log23.txt; vehicle_log12.txt's slope was built a block too high): one going 0.3 east
// along a flat piece and over the top, one let go at rest on the slope.

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McVehicle.h"
#include "Misc/AutomationTest.h"

namespace
{
	const double Flying[][2] = { {8.800,-54.938},{9.088,-54.938},{9.364,-54.938},{9.630,-54.938},{9.885,-54.938},{10.129,-55.067},{10.378,-55.316},{10.638,-55.575},{10.908,-55.845},{11.188,-56.125},{11.479,-56.416},{11.781,-56.718},{12.093,-57.031},{12.416,-57.354},{12.751,-57.688},{13.097,-58.034},{13.454,-58.391},{13.822,-58.759},{14.202,-59.139},{14.593,-59.530},{14.993,-59.930},{15.393,-60.330},{15.793,-60.730},{16.193,-60.938},{16.593,-60.938},{16.993,-60.938},{17.389,-60.938},{17.769,-60.938},{18.134,-60.938},{18.484,-60.938} };
	const double Rolling[][2] = { {12.508,-57.445},{12.524,-57.461},{12.547,-57.485},{12.579,-57.516},{12.619,-57.556},{12.667,-57.604},{12.723,-57.661},{12.788,-57.725},{12.861,-57.799},{12.943,-57.880},{13.033,-57.971},{13.132,-58.070},{13.240,-58.178},{13.357,-58.294},{13.483,-58.420},{13.618,-58.555},{13.762,-58.699},{13.915,-58.852},{14.077,-59.015},{14.249,-59.187},{14.431,-59.368},{14.622,-59.560},{14.823,-59.761},{15.034,-59.971},{15.254,-60.192},{15.485,-60.423},{15.726,-60.663},{15.977,-60.914},{16.238,-60.938},{16.490,-60.938} };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCartSlopeDownTest, "McClient.McEntity.CartSlopeDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcCartSlopeDownTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 12; ++Z) { for (int32 X = -1; X <= 3; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
	const auto Set = [&](int32 X, int32 Y, int32 Z, const TCHAR* Text) { World.SetBlock(FIntVector(X, Y + 62, Z), Registry.ParseState(Text).Get(0)); };
	for (const int32 Z : { 114, 120 })
	{
		for (int32 X = -2; X <= 42; ++X) { Set(X, -62, Z, TEXT("minecraft:stone")); }
		for (int32 X = 10; X <= 14; ++X) { for (int32 Y = -61; Y <= -57 - (X - 10); ++Y) { Set(X, Y, Z, TEXT("minecraft:stone")); } }
		for (int32 X = 0; X <= 9; ++X) { Set(X, -56, Z, TEXT("minecraft:stone")); Set(X, -55, Z, TEXT("minecraft:rail[shape=east_west]")); }
		for (int32 X = 10; X <= 15; ++X) { Set(X, -56 - (X - 10), Z, TEXT("minecraft:rail[shape=ascending_west]")); }
		for (int32 X = 16; X <= 40; ++X) { Set(X, -61, Z, TEXT("minecraft:rail[shape=east_west]")); }
	}
	FMcEntityManager Manager(World);
	FMcMinecartEntity* Fast = new FMcMinecartEntity();
	Fast->Body.Position = FVector(8.5, -54.9 + 62.0, 114.5);
	Fast->Body.Velocity = FVector(0.3, 0.0, 0.0);
	Manager.Add(TUniquePtr<FMcEntity>(Fast));
	FMcMinecartEntity* Slow = new FMcMinecartEntity();
	Slow->Body.Position = FVector(12.5, -57.9 + 62.0, 120.5);
	Manager.Add(TUniquePtr<FMcEntity>(Slow));
	int32 BadFast = 0, BadSlow = 0;
	for (int32 Tick = 0; Tick < 30; ++Tick)
	{
		Manager.Tick();
		const FVector A(Flying[Tick][0], Flying[Tick][1] + 62.0, 114.5);
		const FVector B(Rolling[Tick][0], Rolling[Tick][1] + 62.0, 120.5);
		if (FVector::Dist(A, Fast->Body.Position) > 2e-3 && ++BadFast <= 3)
		{
			AddError(FString::Printf(TEXT("down the slope, tick %d: want %.3f %.3f, got %.3f %.3f"), Tick + 1, A.X, A.Y, Fast->Body.Position.X, Fast->Body.Position.Y));
		}
		if (FVector::Dist(B, Slow->Body.Position) > 2e-3 && ++BadSlow <= 3)
		{
			AddError(FString::Printf(TEXT("rolling from rest, tick %d: want %.3f %.3f, got %.3f %.3f"), Tick + 1, B.X, B.Y, Slow->Body.Position.X, Slow->Body.Position.Y));
		}
	}
	return true;
}
