#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McVehicle.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// vehicle_log12.txt's carts (carts12b.txt, one row a tick, the log's y plus 62): a cart at 0.4 round a rail curve, up and
// down a six block slope, up powered ascending rails, onto stone at the end of its rails and off a ledge.
static const double Log12_256[][3] = {
{17.90000,-60.93750,100.50000},{18.28400,-60.93750,100.50000},{18.65264,-60.93750,100.50000},{19.00653,-60.93750,100.50000},
{19.34627,-60.93750,100.50000},{19.67242,-60.93750,100.50000},{19.98553,-60.93750,100.50000},{20.28610,-60.93750,100.50000},
{20.34709,-60.93750,100.84709},{20.54297,-60.93750,101.04297},{20.50000,-60.93750,101.30890},{20.50000,-60.93750,101.56420},
{20.50000,-60.93750,101.80928},{20.50000,-60.93750,102.04456},{20.50000,-60.93750,102.27043},{20.50000,-60.93750,102.48727},
{20.50000,-60.93750,102.69543},{20.50000,-60.93750,102.89526},{20.50000,-60.93750,103.08710},{20.50000,-60.93750,103.27127},
{20.50000,-60.93750,103.44807},{20.50000,-60.93750,103.61780},{20.50000,-60.93750,103.78074},{20.50000,-60.93750,103.93716},
{20.50000,-60.93750,104.08733},{20.50000,-60.93750,104.23149},{20.50000,-60.93750,104.36988},{20.50000,-60.93750,104.50274},
{20.50000,-60.93750,104.63028},{20.50000,-60.93750,104.75272},{20.50000,-60.93750,104.87026},{20.50000,-60.93750,104.98310},
{20.50000,-60.93750,105.09143},{20.50000,-60.93750,105.19543},{20.50000,-60.93750,105.29526},{20.50000,-60.93750,105.39110},
{20.50000,-60.93750,105.48311},{20.50000,-60.93750,105.57144},{20.50000,-60.93750,105.65623},{20.50000,-60.93750,105.73763},
};
static const double Log12_257[][3] = {
{8.90000,-60.93750,114.50000},{9.28400,-60.93750,114.50000},{9.65264,-60.93750,114.50000},{10.00653,-60.93097,114.50000},
{10.33813,-60.59937,114.50000},{10.63208,-60.30542,114.50000},{10.89175,-60.04575,114.50000},{11.12024,-59.81726,114.50000},
{11.32036,-59.61714,114.50000},{11.49465,-59.44285,114.50000},{11.64545,-59.29205,114.50000},{11.77486,-59.16264,114.50000},
{11.88481,-59.05269,114.50000},{11.97705,-58.96045,114.50000},{12.05318,-58.88432,114.50000},{12.11464,-58.82286,114.50000},
{12.16276,-58.77474,114.50000},{12.19874,-58.73876,114.50000},{12.22366,-58.71384,114.50000},{12.23853,-58.69897,114.50000},
{12.24425,-58.69325,114.50000},{12.24165,-58.69585,114.50000},{12.23120,-58.70630,114.50000},{12.21284,-58.72466,114.50000},
{12.18648,-58.75102,114.50000},{12.15204,-58.78546,114.50000},{12.10945,-58.82805,114.50000},{12.05862,-58.87888,114.50000},
{11.99947,-58.93803,114.50000},{11.93191,-59.00559,114.50000},{11.85587,-59.08163,114.50000},{11.77125,-59.16625,114.50000},
{11.67797,-59.25953,114.50000},{11.57595,-59.36155,114.50000},{11.46510,-59.47240,114.50000},{11.34533,-59.59217,114.50000},
{11.21654,-59.72096,114.50000},{11.07866,-59.85884,114.50000},{10.93158,-60.00592,114.50000},{10.77522,-60.16228,114.50000},
};
static const double Log12_258[][3] = {
{8.90000,-54.93750,128.50000},{9.28400,-54.93750,128.50000},{9.65264,-54.93750,128.50000},{10.00653,-54.93750,128.50000},
{10.34627,-54.93750,128.50000},{10.67242,-54.93750,128.50000},{10.98553,-54.93750,128.50000},{11.28610,-55.22360,128.50000},
{11.59678,-55.53428,128.50000},{11.91837,-55.85587,128.50000},{12.25099,-56.18849,128.50000},{12.59475,-56.53225,128.50000},
{12.94976,-56.88726,128.50000},{13.31613,-57.25363,128.50000},{13.69398,-57.63148,128.50000},{14.08342,-58.02092,128.50000},
{14.48342,-58.42092,128.50000},{14.88342,-58.82092,128.50000},{15.28342,-59.22092,128.50000},{15.68342,-59.62092,128.50000},
{16.08342,-60.02092,128.50000},{16.48342,-60.42092,128.50000},{16.88342,-60.82092,128.50000},{17.28342,-60.93750,128.50000},
{17.68342,-60.93750,128.50000},{18.08342,-60.93750,128.50000},{18.48342,-60.93750,128.50000},{18.88342,-60.93750,128.50000},
{19.27507,-60.93750,128.50000},{19.65106,-60.93750,128.50000},{20.01201,-60.93750,128.50000},{20.35851,-60.93750,128.50000},
{20.69116,-60.93750,128.50000},{21.01050,-60.93750,128.50000},{21.31707,-60.93750,128.50000},{21.61138,-60.93750,128.50000},
{21.89391,-60.93750,128.50000},{22.16514,-60.93750,128.50000},{22.42552,-60.93750,128.50000},{22.67549,-60.93750,128.50000},
};
static const double Log12_259[][3] = {
{8.70000,-60.93750,142.50000},{8.95200,-60.93750,142.50000},{9.25392,-60.93750,142.50000},{9.60376,-60.93750,142.50000},
{9.99961,-60.93750,142.50000},{10.39961,-60.53789,142.50000},{10.79961,-60.13789,142.50000},{11.19961,-59.73789,142.50000},
{11.59961,-59.33789,142.50000},{11.99961,-58.93789,142.50000},{12.39961,-58.53789,142.50000},{12.79961,-58.13789,142.50000},
{13.19961,-57.73789,142.50000},{13.59961,-57.33789,142.50000},{13.99961,-56.93789,142.50000},{14.39961,-56.53789,142.50000},
{14.79961,-56.13789,142.50000},{15.19961,-55.73789,142.50000},{15.59961,-55.33789,142.50000},{15.99961,-54.93789,142.50000},
{16.39961,-54.93750,142.50000},{16.79961,-54.93750,142.50000},{17.19961,-54.93750,142.50000},{17.59961,-54.93750,142.50000},
{17.99961,-54.93750,142.50000},{18.39961,-54.93750,142.50000},{18.79961,-54.93750,142.50000},{19.19961,-54.93750,142.50000},
{19.59961,-54.93750,142.50000},{19.99961,-54.93750,142.50000},{20.39961,-54.93750,142.50000},{20.79961,-54.93750,142.50000},
{21.19961,-54.93750,142.50000},{21.59341,-54.93750,142.50000},{21.97146,-54.93750,142.50000},{22.33438,-54.93750,142.50000},
{22.68279,-54.93750,142.50000},{23.01726,-54.93750,142.50000},{23.33835,-54.93750,142.50000},{23.64660,-54.93750,142.50000},
};
static const double Log12_261[][3] = {
{17.90000,-60.93750,170.50000},{18.28400,-60.93750,170.50000},{18.65264,-60.93750,170.50000},{19.00653,-60.93750,170.50000},
{19.34627,-60.93750,170.50000},{19.67242,-60.93750,170.50000},{19.98553,-60.93750,170.50000},{20.28610,-61.00000,170.50000},
{20.57466,-61.00000,170.50000},{20.71894,-61.00000,170.50000},{20.79108,-61.00000,170.50000},{20.82715,-61.00000,170.50000},
{20.84518,-61.00000,170.50000},{20.85420,-61.00000,170.50000},{20.85871,-61.00000,170.50000},{20.86096,-61.00000,170.50000},
{20.86209,-61.00000,170.50000},{20.86265,-61.00000,170.50000},{20.86265,-61.00000,170.50000},{20.86265,-61.00000,170.50000},
{20.86265,-61.00000,170.50000},{20.86265,-61.00000,170.50000},{20.86265,-61.00000,170.50000},{20.86265,-61.00000,170.50000},
};
static const double Log12_262[][3] = {
{17.90000,-54.93750,184.50000},{18.28400,-54.93750,184.50000},{18.65264,-54.93750,184.50000},{19.00653,-54.93750,184.50000},
{19.34627,-54.93750,184.50000},{19.67242,-54.93750,184.50000},{19.98553,-54.93750,184.50000},{20.28610,-55.00000,184.50000},
{20.57466,-55.00000,184.50000},{20.71894,-55.02000,184.50000},{20.85600,-55.07900,184.50000},{20.98621,-55.17505,184.50000},
{21.10991,-55.30630,184.50000},{21.22743,-55.47098,184.50000},{21.33907,-55.66743,184.50000},{21.44513,-55.89406,184.50000},
{21.54588,-56.14936,184.50000},{21.64160,-56.43189,184.50000},{21.73253,-56.74030,184.50000},{21.81891,-57.07328,184.50000},
{21.90098,-57.42962,184.50000},{21.97894,-57.80814,184.50000},{22.05300,-58.20773,184.50000},{22.12336,-58.62734,184.50000},
{22.19021,-59.06598,184.50000},{22.25371,-59.52268,184.50000},{22.31403,-59.99654,184.50000},{22.37134,-60.48672,184.50000},
{22.42579,-60.99238,184.50000},{22.47751,-61.00000,184.50000},{22.50337,-61.00000,184.50000},{22.51630,-61.00000,184.50000},
{22.52276,-61.00000,184.50000},{22.52600,-61.00000,184.50000},{22.52761,-61.00000,184.50000},{22.52842,-61.00000,184.50000},
{22.52883,-61.00000,184.50000},{22.52883,-61.00000,184.50000},{22.52883,-61.00000,184.50000},{22.52883,-61.00000,184.50000},
};
static const double Log12_260[][3] = {
{2.90000,-60.93750,156.50000},{3.28400,-60.93750,156.50000},{3.65264,-60.93750,156.50000},{4.00653,-60.93750,156.50000},
{4.34627,-60.93750,156.50000},{4.67242,-60.93750,156.50000},{4.98553,-60.93750,156.50000},{5.28610,-60.93750,156.50000},
{5.57466,-60.93750,156.50000},{5.85167,-60.93750,156.50000},{6.11761,-60.93750,156.50000},{6.37290,-60.93750,156.50000},
{6.61799,-60.93750,156.50000},{6.85327,-60.93750,156.50000},{7.07914,-60.93750,156.50000},{7.29597,-60.93750,156.50000},
{7.50413,-60.93750,156.50000},{7.70397,-60.93750,156.50000},{7.89581,-60.93750,156.50000},{8.07998,-60.93750,156.50000},
{8.25678,-60.93750,156.50000},{8.42651,-60.93750,156.50000},{8.58945,-60.93750,156.50000},{8.74587,-60.93750,156.50000},
{8.89603,-60.93750,156.50000},{9.04019,-60.93750,156.50000},{9.17858,-60.93750,156.50000},{9.31144,-60.93750,156.50000},
{9.43898,-60.93750,156.50000},{9.56142,-60.93750,156.50000},{9.67897,-60.93750,156.50000},{9.79181,-60.93750,156.50000},
{9.90014,-60.93750,156.50000},{10.00413,-60.93750,156.50000},{10.10397,-60.93750,156.50000},{10.19981,-60.93750,156.50000},
{10.29181,-60.93750,156.50000},{10.38014,-60.93750,156.50000},{10.46494,-60.93750,156.50000},{10.54634,-60.93750,156.50000},
{10.62448,-60.93750,156.50000},{10.69951,-60.93750,156.50000},{10.77153,-60.93750,156.50000},{10.84066,-60.93750,156.50000},
{10.90704,-60.93750,156.50000},{10.97076,-60.93750,156.50000},{11.03193,-60.93750,156.50000},{11.09065,-60.93750,156.50000},
{11.14702,-60.93750,156.50000},{11.20114,-60.93750,156.50000},{11.25310,-60.93750,156.50000},{11.30297,-60.93750,156.50000},
{11.35085,-60.93750,156.50000},{11.39682,-60.93750,156.50000},{11.44095,-60.93750,156.50000},{11.48331,-60.93750,156.50000},
{11.52398,-60.93750,156.50000},{11.56302,-60.93750,156.50000},{11.60050,-60.93750,156.50000},{11.63648,-60.93750,156.50000},

};
static const double Log13_172[][4] = {
{1,2.80000,-60.93750,102.50000},{2,3.09910,-60.93750,102.50000},{3,3.39730,-60.93750,102.50000},{4,3.69461,-60.93750,102.50000},
{5,3.99103,-60.93750,102.50000},{6,4.28655,-60.93750,102.50000},{7,4.58119,-60.93750,102.50000},{8,4.87495,-60.93750,102.50000},
{9,5.16783,-60.93750,102.50000},{10,5.45982,-60.93750,102.50000},{11,5.75094,-60.93750,102.50000},{12,6.04119,-60.93750,102.50000},
{13,6.33057,-60.93750,102.50000},{14,6.61907,-60.93750,102.50000},{15,6.90672,-60.93750,102.50000},{16,7.19350,-60.93750,102.50000},
{17,7.47942,-60.93750,102.50000},{18,7.76448,-60.93750,102.50000},{19,8.04869,-60.93750,102.50000},{20,8.33204,-60.93750,102.50000},
{21,8.61454,-60.93750,102.50000},{22,8.89620,-60.93750,102.50000},{23,9.17701,-60.93750,102.50000},{24,9.45698,-60.93750,102.50000},
{25,9.73611,-60.93750,102.50000},{26,10.01440,-60.93750,102.50000},{27,10.29186,-60.93750,102.50000},{28,10.66069,-60.93750,102.50000},
{29,11.01477,-60.93750,102.50000},{30,11.35469,-60.93750,102.50000},{31,11.68100,-60.93750,102.50000},{32,12.29501,-60.93750,102.50000},
{33,12.58371,-60.93750,102.50000},{34,12.86087,-60.93750,102.50000},{35,13.12694,-60.93750,102.50000},{36,13.38237,-60.93750,102.50000},
{37,13.62759,-60.93750,102.50000},{38,13.86299,-60.93750,102.50000},{39,14.08898,-60.93750,102.50000},{40,14.30592,-60.93750,102.50000},
{41,14.51419,-60.93750,102.50000},{42,14.71413,-60.93750,102.50000},{43,14.71413,-60.93750,102.50000},{44,14.90607,-60.93750,102.50000},
{45,15.26723,-60.93750,102.50000},};
static const double Log13_174[][4] = {
{1,2.80000,-60.93750,106.50000},{2,3.09910,-60.93750,106.50000},{3,3.39730,-60.93750,106.50000},{4,3.69461,-60.93750,106.50000},
{5,3.99103,-60.93750,106.50000},{6,4.28655,-60.93750,106.50000},{7,4.58119,-60.93750,106.50000},{8,4.87495,-60.93750,106.50000},
{9,5.16783,-60.93750,106.50000},{10,5.45982,-60.93750,106.50000},{11,5.75094,-60.93750,106.50000},{12,6.04119,-60.93750,106.50000},
{13,6.33057,-60.93750,106.50000},{14,6.61907,-60.93750,106.50000},{15,6.90672,-60.93750,106.50000},{16,7.19350,-60.93750,106.50000},
{17,7.47942,-60.93750,106.50000},{18,7.76448,-60.93750,106.50000},{19,8.04869,-60.93750,106.50000},{20,8.33204,-60.93750,106.50000},
{21,8.61454,-60.93750,106.50000},{22,8.89620,-60.93750,106.50000},{23,9.17701,-60.93750,106.50000},{24,9.45698,-60.93750,106.50000},
{25,9.73611,-60.93750,106.50000},{26,10.01440,-60.93750,106.50000},{27,10.29186,-60.93750,106.50000},{28,10.56848,-60.93750,106.50000},
{29,10.84428,-60.93750,106.50000},{30,11.11924,-60.93750,106.50000},{31,11.39339,-60.93750,106.50000},{32,11.93921,-60.93750,106.50000},
{33,12.21089,-60.93750,106.50000},{34,12.48176,-60.93750,106.50000},{35,12.75181,-60.93750,106.50000},{36,13.02105,-60.93750,106.50000},
{37,13.28949,-60.93750,106.50000},{38,13.55712,-60.93750,106.50000},{39,13.82395,-60.93750,106.50000},{40,14.08998,-60.93750,106.50000},
{41,14.35521,-60.93750,106.50000},{42,14.61964,-60.93750,106.50000},{43,14.61964,-60.93750,106.50000},{44,14.88329,-60.93750,106.50000},
{45,15.40820,-60.93750,106.50000},};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCartRailLogTest, "McClient.McEntity.CartRailLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcCartRailLogTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };

	// Runs one row of the log: sets the rails up with Build, summons the cart and compares every tick.
	const auto Replay = [&](const TCHAR* Name, TFunctionRef<void(FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>&)> Build,
		FVector Start, double Push, const double (*Expected)[3], int32 Ticks, double Tolerance)
	{
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 12; ++Z)
		{
			for (int32 X = -1; X <= 3; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		const TFunction<void(int32, int32, int32, const TCHAR*)> Set = [&](int32 X, int32 Y, int32 Z, const TCHAR* Text)
		{
			World.SetBlock(FIntVector(X, Y + 62, Z), State(Text));
		};
		Build(World, Set);
		FMcEntityManager Manager(World);
		FMcMinecartEntity* Cart = new FMcMinecartEntity();
		Cart->Body.Position = FVector(Start.X, Start.Y + 62.0, Start.Z);
		Cart->Body.Velocity = FVector(Push, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Cart));
		int32 Bad = 0;
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			Manager.Tick();
			const FVector Want(Expected[Tick][0], Expected[Tick][1] + 62.0, Expected[Tick][2]);
			const double Off = FVector::Dist(Want, Cart->Body.Position);
			if (Off > Tolerance && ++Bad <= 3)
			{
				AddError(FString::Printf(TEXT("%s tick %d: want %.4f %.4f %.4f, got %.4f %.4f %.4f"), Name, Tick + 1, Want.X, Want.Y, Want.Z,
					Cart->Body.Position.X, Cart->Body.Position.Y, Cart->Body.Position.Z));
			}
		}
		if (Bad > 3)
		{
			AddError(FString::Printf(TEXT("%s: %d ticks off in all"), Name, Bad));
		}
	};
	const auto Floor = [](FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>&) {};
	(void)Floor;

	Replay(TEXT("curve"), [&](FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>& Set)
	{
		for (int32 X = -2; X <= 22; ++X) { for (int32 Z = 98; Z <= 114; ++Z) { Set(X, -62, Z, TEXT("minecraft:stone")); } }
		for (int32 X = 0; X <= 19; ++X) { Set(X, -61, 100, TEXT("minecraft:rail[shape=east_west]")); }
		Set(20, -61, 100, TEXT("minecraft:rail[shape=south_west]"));
		for (int32 Z = 101; Z <= 112; ++Z) { Set(20, -61, Z, TEXT("minecraft:rail[shape=north_south]")); }
	}, FVector(17.5, -61.0, 100.5), 0.4, Log12_256, 40, 1e-3);

	const auto Slab = [](const TFunction<void(int32, int32, int32, const TCHAR*)>& Set, int32 Z)
	{
		for (int32 X = -2; X <= 42; ++X) { Set(X, -62, Z, TEXT("minecraft:stone")); }
	};
	Replay(TEXT("slope up"), [&](FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>& Set)
	{
		Slab(Set, 114);
		for (int32 X = 0; X <= 9; ++X) { Set(X, -61, 114, TEXT("minecraft:rail[shape=east_west]")); }
		for (int32 X = 10; X <= 15; ++X)
		{
			for (int32 Y = -62; Y <= -62 + (X - 10); ++Y) { Set(X, Y, 114, TEXT("minecraft:stone")); }
			Set(X, -61 + (X - 10), 114, TEXT("minecraft:rail[shape=ascending_east]"));
		}
		for (int32 X = 16; X <= 40; ++X) { Set(X, -56, 114, TEXT("minecraft:stone")); Set(X, -55, 114, TEXT("minecraft:rail[shape=east_west]")); }
	}, FVector(8.5, -61.0, 114.5), 0.4, Log12_257, 40, 1e-3);
	Replay(TEXT("slope down"), [&](FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>& Set)
	{
		Slab(Set, 128);
		// (The log's slope starts a block too high: its first rail at x=10 rises to the west and the cart rides flat across
		// it, so it is a flat rail here, and the slope's bottom is a block above the flat rails, which are left out.)
		for (int32 X = 0; X <= 10; ++X) { for (int32 Y = -62; Y <= -56; ++Y) { Set(X, Y, 128, TEXT("minecraft:stone")); } Set(X, -55, 128, TEXT("minecraft:rail[shape=east_west]")); }
		for (int32 X = 11; X <= 15; ++X)
		{
			for (int32 Y = -62; Y <= -56 - (X - 10); ++Y) { Set(X, Y, 128, TEXT("minecraft:stone")); }
			Set(X, -55 - (X - 10), 128, TEXT("minecraft:rail[shape=ascending_west]"));
		}
		for (int32 X = 16; X <= 40; ++X) { Set(X, -61, 128, TEXT("minecraft:rail[shape=east_west]")); }
	}, FVector(8.5, -55.0, 128.5), 0.4, Log12_258, 20, 1e-3);
	Replay(TEXT("powered slope"), [&](FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>& Set)
	{
		Slab(Set, 142);
		for (int32 X = 0; X <= 9; ++X) { Set(X, -62, 142, TEXT("minecraft:redstone_block")); Set(X, -61, 142, TEXT("minecraft:powered_rail[powered=true,shape=east_west]")); }
		for (int32 X = 10; X <= 15; ++X)
		{
			for (int32 Y = -62; Y <= -62 + (X - 10) - 1; ++Y) { Set(X, Y, 142, TEXT("minecraft:stone")); }
			Set(X, -62 + (X - 10), 142, TEXT("minecraft:redstone_block"));
			Set(X, -61 + (X - 10), 142, TEXT("minecraft:powered_rail[powered=true,shape=ascending_east]"));
		}
		for (int32 X = 16; X <= 40; ++X) { Set(X, -56, 142, TEXT("minecraft:stone")); Set(X, -55, 142, TEXT("minecraft:rail[shape=east_west]")); }
	}, FVector(8.5, -61.0, 142.5), 0.2, Log12_259, 40, 1e-3);
	Replay(TEXT("rail end onto stone"), [&](FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>& Set)
	{
		Slab(Set, 170);
		for (int32 X = 0; X <= 19; ++X) { Set(X, -61, 170, TEXT("minecraft:rail[shape=east_west]")); }
	}, FVector(17.5, -61.0, 170.5), 0.4, Log12_261, 24, 1e-3);
	Replay(TEXT("off a ledge"), [&](FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>& Set)
	{
		Slab(Set, 184);
		for (int32 X = 0; X <= 19; ++X) { for (int32 Y = -62; Y <= -56; ++Y) { Set(X, Y, 184, TEXT("minecraft:stone")); } Set(X, -55, 184, TEXT("minecraft:rail[shape=east_west]")); }
	}, FVector(17.5, -55.0, 184.5), 0.4, Log12_262, 40, 1e-3);
	Replay(TEXT("detector row"), [&](FMcBlockWorld&, const TFunction<void(int32, int32, int32, const TCHAR*)>& Set)
	{
		Slab(Set, 156);
		for (int32 X = 0; X <= 40; ++X) { Set(X, -61, 156, X == 10 ? TEXT("minecraft:detector_rail[powered=false,shape=east_west]") : TEXT("minecraft:rail[shape=east_west]")); }
	}, FVector(2.5, -61.0, 156.5), 0.4, Log12_260, 60, 1e-3);

	// ---- The detector rail of that row: powered the tick the cart's box first overlaps its sensor (the middle three fifths,
	// half a block up; the log's box reads powered=true from the cart at 9.79, 0.49 short of the rail's far edge), looked at
	// again 20 ticks later while the cart is over it and off at the first look with none (powered for 40 ticks in all) ----
	{
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 1; ++Z) { for (int32 X = -1; X <= 2; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
		for (int32 X = 0; X <= 40; ++X)
		{
			World.SetBlock(FIntVector(X, 0, 0), State(TEXT("minecraft:stone")));
			World.SetBlock(FIntVector(X, 1, 0), State(X == 10 ? TEXT("minecraft:detector_rail[powered=false,shape=east_west]") : TEXT("minecraft:rail[shape=east_west]")));
		}
		FMcEntityManager Manager(World);
		int32 Now = 0;
		int32 On = -1;
		int32 Off = -1;
		int32 Flips = 0;
		Manager.OnDetector = [&](const FIntVector& Pos, bool bOn)
		{
			++Flips;
			(bOn ? On : Off) = Now;
		};
		FMcMinecartEntity* Cart = new FMcMinecartEntity();
		Cart->Body.Position = FVector(2.5, 1.0, 0.5);
		Cart->Body.Velocity = FVector(0.4, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Cart));
		int32 Overlap = -1;
		for (Now = 1; Now <= 90; ++Now)
		{
			Manager.Tick();
			if (Overlap < 0 && Cart->Body.GetBox().Max.X > 10.2)
			{
				Overlap = Now;
			}
		}
		TestEqual(TEXT("the detector comes on the tick the cart reaches its sensor"), On, Overlap);
		TestEqual(TEXT("and goes off 40 ticks later, after one look that found the cart still over it"), Off - On, 40);
		TestEqual(TEXT("no other changes"), Flips, 2);
	}

	// ---- vehicle_log13.txt: a cart with a pig in it at 0.4 over an activator rail at x=10 (powered, with a redstone block
	// under it, or not) ----
	for (const bool bPowered : { true, false })
	{
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 1; ++Z) { for (int32 X = -1; X <= 2; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
		for (int32 X = 0; X <= 40; ++X)
		{
			for (int32 Z = -2; Z <= 3; ++Z) { World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone"))); }
			World.SetBlock(FIntVector(X, 1, 0), State(X != 10 ? TEXT("minecraft:rail[shape=east_west]") : bPowered ? TEXT("minecraft:activator_rail[shape=east_west,powered=true]") : TEXT("minecraft:activator_rail[shape=east_west,powered=false]")));
		}
		FMcEntityManager Manager(World);
		FMcMinecartEntity* Cart = new FMcMinecartEntity();
		// (A cart only takes a mob in while it moves, so it comes up to 2.5 at 0.4 with the pig already there.)
		Cart->Body.Position = FVector(2.1, 1.0, 0.5);
		Cart->Body.Velocity = FVector(0.4 / 0.96, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Cart));
		FMcMob* Pig = new FMcMob(*FMcMobType::Find(TEXT("minecraft:pig")));
		Pig->Body.Position = FVector(2.5, 1.0, 0.5);
		Manager.Add(TUniquePtr<FMcEntity>(Pig));
		for (int32 Tick = 0; Tick < 3 && Pig->RidingId == 0; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("the pig gets in"), Pig->RidingId, Cart->Id);
		const double (*Want)[4] = bPowered ? Log13_172 : Log13_174;
		int32 Bad = 0;
		// Rows are by tick (the log has a tick or two missing).
		// (The log's later rows are off by the tick its server lagged on.)
		for (int32 Now = 1, Row = 0; Row < 31; ++Row)
		{
			for (; Now <= static_cast<int32>(Want[Row][0]); ++Now)
			{
				Manager.Tick();
				
				if (bPowered && Now == 27)
				{
					TestEqual(TEXT("the pig is out the tick after the cart is in the activator's cell"), Pig->RidingId, 0);
					TestNearlyEqual(TEXT("pig x, the middle of the cell"), Pig->Body.Position.X, 10.5, 0.05);
					TestNearlyEqual(TEXT("pig z, a cell south of a cart going east"), Pig->Body.Position.Z, 1.5, 0.05);
					TestNearlyEqual(TEXT("pig y, on the ground"), Pig->Body.Position.Y, 1.0, 1e-6);
				}
			}
			const FVector Expect(Want[Row][1], Want[Row][2] + 62.0, Want[Row][3] - (bPowered ? 102.0 : 106.0));
			if (FVector::Dist(Expect, Cart->Body.Position) > 1e-3 && ++Bad <= 3)
			{
				AddError(FString::Printf(TEXT("activator %d tick %d: want %.4f %.4f got %.4f %.4f"), bPowered, Now - 1, Expect.X, Expect.Y, Cart->Body.Position.X, Cart->Body.Position.Y));
			}
		}
		if (!bPowered)
		{
			TestEqual(TEXT("an unpowered activator rail leaves the pig in"), Pig->RidingId, Cart->Id);
		}
	}

	// ---- vehicle_log13.txt: hopper carts. One at 0.2 over a powered activator rail at x=10 picks up nothing, not the stacks
	// lying on the rails past it either (still empty 90 ticks on); one at rest takes five stacks dropped together a stack a
	// tick ----
	{
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 1; ++Z) { for (int32 X = -1; X <= 2; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
		for (int32 X = 0; X <= 40; ++X)
		{
			for (int32 Z = -2; Z <= 3; ++Z) { World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone"))); }
			World.SetBlock(FIntVector(X, 1, 0), State(X != 10 ? TEXT("minecraft:rail[shape=east_west]") : TEXT("minecraft:activator_rail[shape=east_west,powered=true]")));
		}
		FMcEntityManager Manager(World);
		FMcMinecartEntity* Moving = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Hopper);
		Moving->Body.Position = FVector(6.5, 1.0, 0.5);
		Moving->Body.Velocity = FVector(0.2, 0.0, 0.0);
		Manager.Add(TUniquePtr<FMcEntity>(Moving));
		Manager.DropItem(FVector(14.5, 1.5, 0.5), FMcItemStack(TEXT("minecraft:cobblestone"), 20), 0).Body.Velocity = FVector::ZeroVector;
		Manager.DropItem(FVector(18.5, 1.5, 0.5), FMcItemStack(TEXT("minecraft:dirt"), 20), 0).Body.Velocity = FVector::ZeroVector;
		for (int32 Tick = 0; Tick < 90; ++Tick)
		{
			Manager.Tick();
		}
		TestTrue(TEXT("the hopper cart went past the stacks"), Moving->Body.Position.X > 19.0);
		TestEqual(TEXT("and took neither"), Moving->GetInventory()->CountItem(TEXT("minecraft:cobblestone")) + Moving->GetInventory()->CountItem(TEXT("minecraft:dirt")), 0);
		Moving->bRemoved = true;
		Manager.Tick();

		FMcMinecartEntity* Rest = new FMcMinecartEntity(FMcMinecartEntity::EVariant::Hopper);
		Rest->Body.Position = FVector(30.5, 1.0, 0.5);
		Manager.Add(TUniquePtr<FMcEntity>(Rest));
		Manager.Tick();
		for (const TCHAR* Name : { TEXT("minecraft:cobblestone"), TEXT("minecraft:dirt"), TEXT("minecraft:stone"), TEXT("minecraft:sand"), TEXT("minecraft:gravel") })
		{
			Manager.DropItem(FVector(30.5, 1.0, 0.5), FMcItemStack(Name, 10), 0).Body.Velocity = FVector::ZeroVector;
		}
		int32 Total = 0;
		TArray<int32> Counts;
		for (int32 Tick = 0; Tick < 8; ++Tick)
		{
			Manager.Tick();
			int32 Held = 0;
			for (const TCHAR* Name : { TEXT("minecraft:cobblestone"), TEXT("minecraft:dirt"), TEXT("minecraft:stone"), TEXT("minecraft:sand"), TEXT("minecraft:gravel") })
			{
				Held += Rest->GetInventory()->CountItem(Name);
			}
			Counts.Add(Held);
			Total = Held;
		}
		TestEqual(TEXT("it ends with all five"), Total, 50);
		for (int32 Index = 1; Index < Counts.Num(); ++Index)
		{
			TestTrue(*FString::Printf(TEXT("never more than one stack a tick (tick %d)"), Index), Counts[Index] - Counts[Index - 1] <= 10);
		}
	}
	return true;
}

#endif