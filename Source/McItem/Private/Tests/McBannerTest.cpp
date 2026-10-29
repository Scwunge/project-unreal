#include "McBanner.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBannerTest, "McClient.McItem.Banners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBannerTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("the base colour is the item's name less banner"), McBanner::GetBaseColour(TEXT("minecraft:light_blue_banner")), FString(TEXT("light_blue")));
	const TArray<McBanner::FLayer> Layers = { { TEXT("stripe_downright"), TEXT("red") }, { TEXT("border"), TEXT("blue") } };
	FMcItemStack Banner(TEXT("minecraft:white_banner"));
	Banner.Set(TEXT("minecraft:banner_patterns"), McBanner::MakeComponent(Layers));
	const TArray<McBanner::FLayer> Read = McBanner::GetLayers(Banner);
	if (TestEqual(TEXT("the item's patterns read back"), Read.Num(), 2))
	{
		TestEqual(TEXT("in order"), Read[0].Pattern, FString(TEXT("stripe_downright")));
		TestEqual(TEXT("with their dyes"), Read[1].Colour, FString(TEXT("blue")));
	}
	const FString Line = McBanner::Encode(TEXT("white"), true, Read);
	TestEqual(TEXT("a placed flag is one line"), Line, FString(TEXT("white;w;stripe_downright=red,border=blue")));
	FString Base;
	bool bWall = false;
	TArray<McBanner::FLayer> Back;
	TestTrue(TEXT("which reads back"), McBanner::Decode(Line, Base, bWall, Back));
	TestTrue(TEXT("as a wall flag"), bWall && Base == TEXT("white") && Back.Num() == 2 && Back[1].Pattern == TEXT("border"));
	TestTrue(TEXT("a plain banner has no layers"), McBanner::Decode(McBanner::Encode(TEXT("red"), false, {}), Base, bWall, Back) && Back.IsEmpty() && !bWall);
	TestFalse(TEXT("nonsense doesn't"), McBanner::Decode(TEXT("nonsense"), Base, bWall, Back));
	TestTrue(TEXT("red is not white"), McBanner::GetDyeColour(TEXT("red")) != McBanner::GetDyeColour(TEXT("white")));
	return true;
}

#endif
