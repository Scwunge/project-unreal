#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McLightEngine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLightEngineTest, "McClient.McWorld.LightEngine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLightEngineTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()) || !TestTrue(TEXT("light tables load"), FMcBlockLight::Get().EnsureLoaded()))
	{
		return false;
	}
	const TSharedRef<const FMcLightPalette> Palette = FMcLightPalette::Build();
	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Glass = State(TEXT("minecraft:glass"));
	const FMcStateId Water = State(TEXT("minecraft:water[level=0]"));
	const FMcStateId Torch = State(TEXT("minecraft:torch"));

	FMcLightRegion Region;
	Region.MinY = 0;
	Region.Height = 32;
	Region.Blocks.SetNumZeroed(FMcLightRegion::Size * FMcLightRegion::Size * Region.Height);
	auto Set = [&](int32 X, int32 Y, int32 Z, FMcStateId Id) { Region.Blocks[Region.Index(X, Y, Z)] = Id; };

	// A stone floor at y = 0 everywhere.
	for (int32 Z = 0; Z < FMcLightRegion::Size; ++Z)
	{
		for (int32 X = 0; X < FMcLightRegion::Size; ++X)
		{
			Set(X, 0, Z, Stone);
		}
	}
	// A closed stone box from (20, 1, 20) to (28, 5, 28) with a torch inside at (24, 2, 24).
	for (int32 Y = 1; Y <= 5; ++Y)
	{
		for (int32 Z = 20; Z <= 28; ++Z)
		{
			for (int32 X = 20; X <= 28; ++X)
			{
				const bool bWall = X == 20 || X == 28 || Z == 20 || Z == 28 || Y == 5;
				Set(X, Y, Z, bWall ? Stone : 0);
			}
		}
	}
	Set(24, 2, 24, Torch);
	// A glass roof over (5, 3, 5), and a water column at (10, 1..3, 10).
	Set(5, 3, 5, Glass);
	Set(10, 1, 10, Water);
	Set(10, 2, 10, Water);
	Set(10, 3, 10, Water);
	// A stone roof over (40, 3, 40), open at the sides.
	Set(40, 3, 40, Stone);

	Region.Compute(*Palette);

	TestEqual(TEXT("open air has full sky"), Region.GetSkyLight(2, 1, 2), static_cast<uint8>(15));
	TestEqual(TEXT("sky passes glass undimmed"), Region.GetSkyLight(5, 2, 5), static_cast<uint8>(15));
	TestEqual(TEXT("water top: 14"), Region.GetSkyLight(10, 3, 10), static_cast<uint8>(14));
	TestEqual(TEXT("a one-wide water column is lit from the air beside it: 14"), Region.GetSkyLight(10, 1, 10), static_cast<uint8>(14));
	TestEqual(TEXT("under a lone roof: lit from the side, 14"), Region.GetSkyLight(40, 2, 40), static_cast<uint8>(14));
	TestEqual(TEXT("inside the closed box: no sky"), Region.GetSkyLight(22, 2, 22), static_cast<uint8>(0));
	TestEqual(TEXT("stone holds no light"), Region.GetSkyLight(20, 2, 22), static_cast<uint8>(0));
	TestEqual(TEXT("above the region: full sky"), Region.GetSkyLight(0, 40, 0), static_cast<uint8>(15));

	TestEqual(TEXT("torch cell: 14"), Region.GetBlockLight(24, 2, 24), static_cast<uint8>(14));
	TestEqual(TEXT("next to the torch: 13"), Region.GetBlockLight(25, 2, 24), static_cast<uint8>(13));
	TestEqual(TEXT("diagonal: 12 (light goes round corners one block at a time)"), Region.GetBlockLight(25, 2, 25), static_cast<uint8>(12));
	TestEqual(TEXT("box corner, 7 steps away: 7"), Region.GetBlockLight(21, 1, 21), static_cast<uint8>(7));
	TestEqual(TEXT("no torch light outside the box"), Region.GetBlockLight(30, 2, 24), static_cast<uint8>(0));
	return true;
}

#endif
