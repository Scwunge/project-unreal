#include "McTextComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// JSON text reads into spans with inherited styles and writes out as legacy text; the vanilla client's tellraw shot
// (tc_chat) fixes the inheritance (an array's first element is the parent of the rest).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTextComponentTest, "McClient.McCore.TextComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcTextComponentTest::RunTest(const FString& Parameters)
{
	TArray<McText::FSpan> Spans;
	TestTrue(TEXT("a string"), McText::Parse(TEXT("\"plain string\""), Spans));
	TestEqual(TEXT("its text"), McText::ToPlain(Spans), FString(TEXT("plain string")));
	TestEqual(TEXT("no style"), McText::ToLegacy(Spans), FString(TEXT("plain string")));
	TestFalse(TEXT("not JSON"), McText::Parse(TEXT("{\"text\":"), Spans));

	TestTrue(TEXT("an array"), McText::Parse(TEXT("[{\"text\":\"Red \",\"color\":\"red\"},{\"text\":\"bold\",\"bold\":true,\"color\":\"#336699\"},{\"text\":\" italic\",\"italic\":true},{\"text\":\" gold\",\"color\":\"gold\"}]"), Spans));
	TestEqual(TEXT("four spans"), Spans.Num(), 4);
	if (Spans.Num() == 4)
	{
		TestEqual(TEXT("red"), Spans[0].Rgb, 0xFF5555);
		TestEqual(TEXT("a hex colour"), Spans[1].Rgb, 0x336699);
		TestTrue(TEXT("bold"), Spans[1].bBold && !Spans[0].bBold);
		// The first element is the parent of the rest: " italic" keeps its red, and the second's colour does not reach it.
		TestEqual(TEXT("italic inherits the parent's red"), Spans[2].Rgb, 0xFF5555);
		TestTrue(TEXT("italic"), Spans[2].bItalic && !Spans[2].bBold);
		TestEqual(TEXT("gold"), Spans[3].Rgb, 0xFFAA00);
	}
	TestEqual(TEXT("legacy text"), McText::ToLegacy(Spans),
		FString(TEXT("§cRed §r§x§3§3§6§6§9§9§lbold§r§c§o italic§r§6 gold")));

	TestTrue(TEXT("extra"), McText::Parse(TEXT("{\"text\":\"parent \",\"color\":\"green\",\"extra\":[{\"text\":\"child \"},{\"text\":\"over\",\"color\":\"aqua\",\"bold\":true},{\"text\":\" back\",\"bold\":false}]}"), Spans));
	TestEqual(TEXT("four spans from extra"), Spans.Num(), 4);
	if (Spans.Num() == 4)
	{
		TestEqual(TEXT("child takes the parent's colour"), Spans[1].Rgb, 0x55FF55);
		TestEqual(TEXT("over is aqua and bold"), Spans[2].Rgb, 0x55FFFF);
		TestTrue(TEXT("bold"), Spans[2].bBold);
		TestEqual(TEXT("back is green again (it sets only bold)"), Spans[3].Rgb, 0x55FF55);
	}

	TestTrue(TEXT("translate"), McText::Parse(TEXT("{\"translate\":\"chat.type.text\",\"with\":[\"Steve\",\"hello\"]}"), Spans));
	TestEqual(TEXT("filled in"), McText::ToPlain(Spans), FString(TEXT("<Steve> hello")));
	TestTrue(TEXT("a fallback for a missing key"), McText::Parse(TEXT("{\"translate\":\"zz.no.such\",\"fallback\":\"fb %s\",\"with\":[\"x\"]}"), Spans));
	TestEqual(TEXT("the fallback"), McText::ToPlain(Spans), FString(TEXT("fb x")));
	TestTrue(TEXT("a font"), McText::Parse(TEXT("{\"text\":\"a\",\"font\":\"uniform\"}"), Spans));
	TestEqual(TEXT("with its namespace"), Spans.Num() == 1 ? Spans[0].Font : FString(), FString(TEXT("minecraft:uniform")));
	TestTrue(TEXT("click and hover"), McText::Parse(TEXT("{\"text\":\"link\",\"click_event\":{\"action\":\"open_url\",\"url\":\"https://example.com\"},\"hover_event\":{\"action\":\"show_text\",\"value\":{\"text\":\"tip\",\"color\":\"red\"}}}"), Spans));
	if (Spans.Num() == 1)
	{
		TestEqual(TEXT("click action"), Spans[0].ClickAction, FString(TEXT("open_url")));
		TestEqual(TEXT("click value"), Spans[0].ClickValue, FString(TEXT("https://example.com")));
		TestEqual(TEXT("hover text"), Spans[0].HoverText, FString(TEXT("§ctip")));
	}
	TestEqual(TEXT("a number and a boolean"), McText::JsonToLegacy(TEXT("[1,true]")), FString(TEXT("1true")));
	return true;
}

#endif
