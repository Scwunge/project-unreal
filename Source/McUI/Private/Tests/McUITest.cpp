#include "McContainer.h"
#include "McCraftingMenu.h"
#include "McFontRaster.h"
#include "McFurnace.h"
#include "McGuiLayout.h"
#include "McHUD.h"
#include "McItemDefinition.h"
#include "McItemStack.h"
#include "McItemRegistry.h"
#include "McMenuInput.h"
#include "McPlayerData.h"
#include "McResourcePacks.h"
#include "McRecipe.h"
#include "McScreenLayout.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHudLayoutTest, "McClient.McUI.HudLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcHudLayoutTest::RunTest(const FString& Parameters)
{
	using namespace McGui;
	const auto TestPoint = [this](const TCHAR* What, FIntPoint Actual, FIntPoint Expected)
	{
		TestTrue(FString::Printf(TEXT("%s: (%d, %d), expected (%d, %d)"), What, Actual.X, Actual.Y, Expected.X, Expected.Y), Actual == Expected);
	};
	// The vignette is full in the dark and gone in full light, in between as measured, and eases about 0.9 percent a tick.
	TestEqual(TEXT("dark vignette"), GetVignetteTarget(0.0f), 1.0f);
	TestEqual(TEXT("no vignette in full light"), GetVignetteTarget(15.0f), 0.0f);
	TestTrue(TEXT("light 13 measured"), FMath::IsNearlyEqual(GetVignetteTarget(13.0f), 0.378f, 1e-4f));
	TestTrue(TEXT("between levels it is interpolated"), FMath::IsNearlyEqual(GetVignetteTarget(10.5f), 0.5f * (0.621f + 0.572f), 1e-4f));
	TestTrue(TEXT("never stronger in more light"), GetVignetteTarget(8.0f) > GetVignetteTarget(9.0f) && GetVignetteTarget(3.0f) > GetVignetteTarget(4.0f));
	TestTrue(TEXT("a tick moves it a little"), FMath::IsNearlyEqual(EaseVignette(0.2f, 0.7f, 1.0f), 0.2f + 0.5f * 0.0087f, 1e-4f));
	TestTrue(TEXT("a hundred ticks about halfway"), FMath::IsNearlyEqual(EaseVignette(0.2f, 0.7f, 100.0f), 0.2f + 0.5f * (1.0f - FMath::Pow(1.0f - 0.0087f, 100.0f)), 1e-4f));

	// The GUI scale: the largest that keeps 320 x 240, or the setting if smaller.
	TestEqual(TEXT("1080p is scale 4"), GetGuiScale(1920, 1080), 4);
	TestEqual(TEXT("640 x 480 is scale 2"), GetGuiScale(640, 480), 2);
	TestEqual(TEXT("a small window is 1"), GetGuiScale(300, 200), 1);
	TestEqual(TEXT("a setting below the largest is kept"), GetGuiScale(1920, 1080, 2), 2);
	TestEqual(TEXT("a setting above is cut"), GetGuiScale(1920, 1080, 9), 4);

	FMcPlayerStats Stats;
	FHudLayout Layout = ComputeHud(320, 240, Stats);
	TestEqual(TEXT("hotbar x"), Layout.Hotbar.X, 69);
	TestEqual(TEXT("hotbar y"), Layout.Hotbar.Y, 218);
	TestPoint(TEXT("crosshair"), Layout.Crosshair, FIntPoint(152, 112));
	TestEqual(TEXT("nine item slots"), Layout.HotbarSlots.Num(), 9);
	TestPoint(TEXT("the first item is 3 in"), Layout.HotbarSlots[0], FIntPoint(72, 221));
	TestPoint(TEXT("the ninth is 160 on"), Layout.HotbarSlots[8], FIntPoint(232, 221));
	TestEqual(TEXT("ten hearts"), Layout.Hearts.Num(), 10);
	TestPoint(TEXT("the first heart"), Layout.Hearts[0].Pos, FIntPoint(69, 201));
	TestPoint(TEXT("8 pixels apart"), Layout.Hearts[9].Pos, FIntPoint(141, 201));
	TestPoint(TEXT("food runs from the right edge"), Layout.Food[0].Pos, FIntPoint(242, 201));
	TestPoint(TEXT("leftwards"), Layout.Food[9].Pos, FIntPoint(170, 201));
	TestFalse(TEXT("no armour, no bar"), Layout.bShowArmor);
	TestFalse(TEXT("no breath bar on dry land"), Layout.bShowAir);
	TestFalse(TEXT("no level at level 0"), Layout.bShowLevel);

	// Halves.
	Stats.Health = 7.0f;
	Layout = ComputeHud(320, 240, Stats);
	TestTrue(TEXT("three full hearts"), Layout.Hearts[0].Fill == EIcon::Full && Layout.Hearts[1].Fill == EIcon::Full && Layout.Hearts[2].Fill == EIcon::Full);
	TestTrue(TEXT("a half"), Layout.Hearts[3].Fill == EIcon::Half);
	TestTrue(TEXT("and empty ones"), Layout.Hearts[4].Fill == EIcon::Empty && Layout.Hearts[9].Fill == EIcon::Empty);
	Stats.Food = 5;
	Layout = ComputeHud(320, 240, Stats);
	TestTrue(TEXT("food halves the same way"), Layout.Food[1].Fill == EIcon::Full && Layout.Food[2].Fill == EIcon::Half && Layout.Food[3].Fill == EIcon::Empty);

	// The hurt flash.
	TestFalse(TEXT("no flash when not hurt"), IsHurtBlink(0));
	TestTrue(TEXT("flash"), IsHurtBlink(10));
	TestFalse(TEXT("then off"), IsHurtBlink(8));
	TestTrue(TEXT("then on"), IsHurtBlink(4));
	Stats.HurtTime = 10;
	Layout = ComputeHud(320, 240, Stats);
	TestTrue(TEXT("the hearts blink"), Layout.Hearts[0].bBlink);

	// Low health jiggles the hearts by a pixel.
	Stats.Health = 4.0f;
	Stats.HurtTime = 0;
	bool bAllInRange = true;
	for (int32 Shake = 0; Shake < 20; ++Shake)
	{
		for (const FBarIcon& Heart : ComputeHud(320, 240, Stats, Shake).Hearts)
		{
			bAllInRange &= Heart.Pos.Y == 201 || Heart.Pos.Y == 202;
		}
	}
	TestTrue(TEXT("hearts stay within a pixel"), bAllInRange);

	// More than ten hearts go up in rows, and the armour above them.
	Stats = FMcPlayerStats();
	Stats.MaxHealth = 40.0f;
	Stats.Health = 40.0f;
	Stats.Armor = 20;
	Layout = ComputeHud(320, 240, Stats);
	TestEqual(TEXT("twenty hearts"), Layout.Hearts.Num(), 20);
	TestPoint(TEXT("the eleventh starts a row above"), Layout.Hearts[10].Pos, FIntPoint(69, 191));
	TestTrue(TEXT("armour shows"), Layout.bShowArmor);
	TestPoint(TEXT("above the top row"), Layout.Armor[0].Pos, FIntPoint(69, 181));
	TestTrue(TEXT("all ten full"), Layout.Armor[9].Fill == EIcon::Full);

	Stats = FMcPlayerStats();
	Stats.Absorption = 4.0f;
	Layout = ComputeHud(320, 240, Stats);
	TestEqual(TEXT("two golden hearts"), Layout.Absorption.Num(), 2);
	TestPoint(TEXT("in the next row"), Layout.Absorption[0].Pos, FIntPoint(69, 191));

	// Breath: bubbles fall away as it runs out, and the last one bursts.
	Stats = FMcPlayerStats();
	Stats.Air = 300;
	TestFalse(TEXT("full breath, no bubbles"), ComputeHud(320, 240, Stats).bShowAir);
	Stats.Air = 299;
	Layout = ComputeHud(320, 240, Stats);
	TestTrue(TEXT("a breath short of full shows them"), Layout.bShowAir);
	TestEqual(TEXT("ten of them"), Layout.Air.Num(), 10);
	TestPoint(TEXT("above the food"), Layout.Air[0].Pos, FIntPoint(242, 191));
	Stats.Air = 100;
	TestEqual(TEXT("a third of the breath, four bubbles"), ComputeHud(320, 240, Stats).Air.Num(), 4);
	Stats.Air = 1;
	Layout = ComputeHud(320, 240, Stats);
	TestEqual(TEXT("one left"), Layout.Air.Num(), 1);
	TestTrue(TEXT("and it bursts"), Layout.Air[0].Fill == EIcon::Half);
	Stats.Air = 0;
	TestEqual(TEXT("none at 0"), ComputeHud(320, 240, Stats).Air.Num(), 0);

	// Experience.
	Stats = FMcPlayerStats();
	Stats.XpProgress = 0.5f;
	Stats.XpLevel = 5;
	Layout = ComputeHud(320, 240, Stats);
	TestEqual(TEXT("half the bar"), Layout.XpFill, 91);
	TestPoint(TEXT("the bar sits above the hotbar"), Layout.XpBar, FIntPoint(69, 211));
	TestTrue(TEXT("the level shows"), Layout.bShowLevel);
	TestPoint(TEXT("centred"), Layout.LevelText, FIntPoint(160, 205));
	Stats.XpProgress = 1.0f;
	TestEqual(TEXT("a full bar is 182"), ComputeHud(320, 240, Stats).XpFill, 182);

	// Durability bars and counts.
	TestFalse(TEXT("an undamaged item has no bar"), GetDurabilityBar(0, 100).bShow);
	TestFalse(TEXT("an item with no durability has none"), GetDurabilityBar(5, 0).bShow);
	TestEqual(TEXT("half damaged, half the 13 pixels"), GetDurabilityBar(50, 100).Width, 7);
	// Bundles (vanilla ref run bundle1): 1, 16, 32, 48, 63 and 64 of 64 sticks.
	TestFalse(TEXT("an empty bundle has no bar"), GetBundleBar(0.0).bShow);
	const double Fulls[] = {1.0 / 64, 16.0 / 64, 32.0 / 64, 48.0 / 64, 63.0 / 64, 1.0};
	const int32 Widths[] = {1, 4, 7, 10, 12, 13};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		TestEqual(TEXT("bundle bar width"), GetBundleBar(Fulls[Index]).Width, Widths[Index]);
	}
	// The bundle tooltip (ref runs bundletip, bundletip2): 4 columns, items in the last cells; the content
	// is 12 (title and gap) + the body + 4 + 13 high: 47 for the description (2 lines), 101 for 3 rows of cells.
	TestEqual(TEXT("always 4 columns"), GetBundleGrid(1).Columns, 4);
	TestEqual(TEXT("1 item one row"), GetBundleGrid(1).Rows, 1);
	TestEqual(TEXT("1 item 3 blanks"), GetBundleGrid(1).Blanks, 3);
	TestEqual(TEXT("5 items 2 rows"), GetBundleGrid(5).Rows, 2);
	TestEqual(TEXT("5 items 3 blanks"), GetBundleGrid(5).Blanks, 3);
	TestEqual(TEXT("12 items 3 rows"), GetBundleGrid(12).Rows, 3);
	TestEqual(TEXT("12 items no blanks"), GetBundleGrid(12).Blanks, 0);
	TestEqual(TEXT("13 items hide 5"), GetBundleGrid(13).Hidden, 5);
	TestEqual(TEXT("13 items show 8 after 3 blanks"), GetBundleGrid(13).Blanks, 3);
	TestEqual(TEXT("16 items hide 5"), GetBundleGrid(16).Hidden, 5);
	TestEqual(TEXT("16 items no blanks"), GetBundleGrid(16).Blanks, 0);
	TestEqual(TEXT("17 items hide 9"), GetBundleGrid(17).Hidden, 9);
	TestEqual(TEXT("17 items 3 blanks"), GetBundleGrid(17).Blanks, 3);
	TestEqual(TEXT("24 items hide 13"), GetBundleGrid(24).Hidden, 13);
	TestEqual(TEXT("12 items hide none"), GetBundleGrid(12).Hidden, 0);
	TestEqual(TEXT("10 items 4 columns"), GetBundleGrid(10).Columns, 4);
	TestEqual(TEXT("10 items 3 rows"), GetBundleGrid(10).Rows, 3);
	TestEqual(TEXT("10 items 2 blanks"), GetBundleGrid(10).Blanks, 2);
	TestEqual(TEXT("empty tooltip height"), GetBundleTipHeight(0, 2), 47);
	TestEqual(TEXT("three rows tooltip height"), GetBundleTipHeight(3, 0), 101);
	TestEqual(TEXT("blue while there is room"), GetBundleBar(0.5).Color, 0x7087FFu);
	TestEqual(TEXT("red when full"), GetBundleBar(1.0).Color, 0xFF5454u);
	TestEqual(TEXT("one point off, still full width"), GetDurabilityBar(1, 1561).Width, 13);
	TestEqual(TEXT("nearly broken, a pixel"), GetDurabilityBar(99, 100).Width, 0);
	const uint32 Half = GetDurabilityBar(50, 100).Color;
	TestTrue(TEXT("halfway is yellow"), ((Half >> 16) & 0xFF) > 200 && ((Half >> 8) & 0xFF) > 200 && (Half & 0xFF) < 10);
	const uint32 Fresh = GetDurabilityBar(1, 1000).Color;
	TestTrue(TEXT("nearly new is green"), ((Fresh >> 8) & 0xFF) > 200 && ((Fresh >> 16) & 0xFF) < 40);
	const uint32 Worn = GetDurabilityBar(990, 1000).Color;
	TestTrue(TEXT("nearly gone is red"), ((Worn >> 16) & 0xFF) > 200 && ((Worn >> 8) & 0xFF) < 40);
	TestFalse(TEXT("one item says nothing"), ShowsCount(1));
	TestTrue(TEXT("a stack does"), ShowsCount(64));

	// The hotbar wheel wraps.
	TestEqual(TEXT("scroll on"), UMcPlayerDataComponent::Scroll(3, 1), 4);
	TestEqual(TEXT("past the end"), UMcPlayerDataComponent::Scroll(8, 1), 0);
	TestEqual(TEXT("before the start"), UMcPlayerDataComponent::Scroll(0, -1), 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcScreenLayoutTest, "McClient.McUI.ScreenLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcScreenLayoutTest::RunTest(const FString& Parameters)
{
	using namespace McGui;
	const auto TestPoint = [this](const TCHAR* What, FIntPoint Actual, FIntPoint Expected)
	{
		TestTrue(FString::Printf(TEXT("%s: (%d, %d), expected (%d, %d)"), What, Actual.X, Actual.Y, Expected.X, Expected.Y), Actual == Expected);
	};
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const TSharedPtr<FMcContainer> Inventory = MakeShared<FMcContainer>(36);
	const FMcPlayerMenu Player(Inventory, MakeShared<FMcContainer>(4), MakeShared<FMcContainer>(1));
	const FScreenLayout PlayerLayout = MakeInventoryLayout();
	TestEqual(TEXT("the inventory screen has a position for every slot"), PlayerLayout.Slots.Num(), Player.NumSlots());
	TestEqual(TEXT("the crafting table too"), MakeCraftingTableLayout().Slots.Num(), FMcCraftingMenu(3, Inventory).NumSlots());
	const TArray<FString> Galactic = Enchanting::GetGalacticNames(1234);
	TestEqual(TEXT("the table writes three lines"), Galactic.Num(), 3);
	TestEqual(TEXT("and the same ones for the same seed"), Enchanting::GetGalacticNames(1234), Galactic);
	TestTrue(TEXT("and others for another seed"), Enchanting::GetGalacticNames(1235) != Galactic);
	for (const FString& Line : Galactic)
	{
		TArray<FString> Words;
		Line.ParseIntoArray(Words, TEXT(" "));
		TestTrue(TEXT("of three or four words"), Words.Num() == 3 || Words.Num() == 4);
	}
	TestEqual(TEXT("an unnamed item's field starts at its usual name"), Anvil::GetFieldFor(FString(), TEXT("Iron Sword")), FString(TEXT("Iron Sword")));
	TestEqual(TEXT("a named one's at its name"), Anvil::GetFieldFor(TEXT("Biter"), TEXT("Iron Sword")), FString(TEXT("Biter")));
	TestEqual(TEXT("the usual name sends no custom name"), Anvil::GetNameToSend(TEXT("Iron Sword"), TEXT("Iron Sword")), FString());
	TestEqual(TEXT("another name is sent"), Anvil::GetNameToSend(TEXT("Biter"), TEXT("Iron Sword")), FString(TEXT("Biter")));
	TestEqual(TEXT("the loom's first pattern"), Loom::FindPattern(FIntPoint(61, 14), 0, 32), 0);
	TestEqual(TEXT("one row down and across"), Loom::FindPattern(FIntPoint(60 + 14 + 1, 13 + 14 + 1), 0, 32), 5);
	TestEqual(TEXT("scrolled a row"), Loom::FindPattern(FIntPoint(61, 14), 1, 32), 4);
	TestEqual(TEXT("past the list is none"), Loom::FindPattern(FIntPoint(61 + 14, 14), 0, 1), -1);
	TestEqual(TEXT("32 patterns scroll 4 rows"), Loom::MaxScroll(32), 4);
	TestEqual(TEXT("16 fit"), Loom::MaxScroll(16), 0);
	TestEqual(TEXT("a dye's colour"), Loom::GetDyeColour(TEXT("red")), 0xB02E26u);
	TestEqual(TEXT("a small chest"), MakeChestLayout(3).Slots.Num(), FMcContainerMenu(MakeShared<FMcContainer>(27), Inventory).NumSlots());
	TestEqual(TEXT("a large chest"), MakeChestLayout(6).Slots.Num(), FMcContainerMenu(MakeShared<FMcContainer>(54), Inventory).NumSlots());
	TestEqual(TEXT("a furnace"), MakeFurnaceLayout().Slots.Num(), FMcFurnaceMenu(MakeShared<FMcFurnace>(), Inventory).NumSlots());
	TestEqual(TEXT("a small chest is 168 high"), MakeChestLayout(3).Size.Y, 168);
	TestEqual(TEXT("a large one 222"), MakeChestLayout(6).Size.Y, 222);

	TestEqual(TEXT("the result slot"), PlayerLayout.FindSlot(FIntPoint(155, 29)), 0);
	TestEqual(TEXT("its border catches the mouse too"), PlayerLayout.FindSlot(FIntPoint(153, 27)), 0);
	TestEqual(TEXT("just past the border doesn't"), PlayerLayout.FindSlot(FIntPoint(171, 29)), -1);
	TestEqual(TEXT("the helmet slot"), PlayerLayout.FindSlot(FIntPoint(10, 10)), FPlayerMenuSlots::ArmorFirst);
	TestEqual(TEXT("the boots"), PlayerLayout.FindSlot(FIntPoint(10, 10 + 54)), FPlayerMenuSlots::ArmorFirst + 3);
	TestEqual(TEXT("the offhand"), PlayerLayout.FindSlot(FIntPoint(80, 65)), FPlayerMenuSlots::Offhand);
	TestEqual(TEXT("the first inventory slot"), PlayerLayout.FindSlot(FIntPoint(9, 85)), FPlayerMenuSlots::MainFirst);
	TestEqual(TEXT("the last hotbar slot"), PlayerLayout.FindSlot(FIntPoint(8 + 8 * 18 + 2, 144)), FPlayerMenuSlots::HotbarFirst + 8);
	TestEqual(TEXT("nothing in the margin"), PlayerLayout.FindSlot(FIntPoint(3, 3)), -1);
	TestPoint(TEXT("centred on a 320 x 240 screen"), PlayerLayout.Origin(320, 240), FIntPoint(72, 37));

	TestEqual(TEXT("the furnace input"), MakeFurnaceLayout().FindSlot(FIntPoint(60, 20)), 0);
	TestEqual(TEXT("the furnace fuel"), MakeFurnaceLayout().FindSlot(FIntPoint(60, 56)), 1);
	TestEqual(TEXT("the furnace output"), MakeFurnaceLayout().FindSlot(FIntPoint(120, 40)), 2);
	TestEqual(TEXT("the chest's last container slot"), MakeChestLayout(3).FindSlot(FIntPoint(8 + 8 * 18 + 1, 18 + 2 * 18 + 1)), 26);
	TestEqual(TEXT("the chest's first player slot"), MakeChestLayout(3).FindSlot(FIntPoint(9, 86)), 27);

	TestEqual(TEXT("the arrow starts empty"), GetArrowFill(0.0f, 200), 0.0f);
	TestEqual(TEXT("half cooked"), GetArrowFill(100.0f, 200), 0.5f);
	TestEqual(TEXT("done is full"), GetArrowFill(250.0f, 200), 1.0f);
	TestEqual(TEXT("no recipe, no arrow"), GetArrowFill(10.0f, 0), 0.0f);
	TestEqual(TEXT("a fresh fuel is a full flame"), GetFlameFill(1600, 1600), 1.0f);
	TestEqual(TEXT("it shrinks"), GetFlameFill(400, 1600), 0.25f);
	TestEqual(TEXT("out"), GetFlameFill(0, 1600), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSlotClickTest, "McClient.McUI.SlotClicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSlotClickTest::RunTest(const FString& Parameters)
{
	using namespace McGui;
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto Stack = [](const TCHAR* Item, int32 Count) { return FMcItemStack(FName(Item), Count); };
	const auto Fresh = [](TSharedPtr<FMcContainer>& Inventory, TSharedPtr<FMcContainer>& Armor, TSharedPtr<FMcContainer>& Offhand)
	{
		Inventory = MakeShared<FMcContainer>(36);
		Armor = MakeShared<FMcContainer>(4);
		Offhand = MakeShared<FMcContainer>(1);
	};
	constexpr int32 Main = FPlayerMenuSlots::MainFirst;
	constexpr int32 Hot = FPlayerMenuSlots::HotbarFirst;
	TSharedPtr<FMcContainer> Inventory, Armor, Offhand;

	{
		// Left click picks up and places; right click splits and places one.
		Fresh(Inventory, Armor, Offhand);
		const TSharedPtr<FMcPlayerMenu> Menu = MakeShared<FMcPlayerMenu>(Inventory, Armor, Offhand);
		FMcMenuInput Input(Menu);
		Menu->SetStack(Main, Stack(TEXT("minecraft:stone"), 10));
		Input.MouseDown(Main, 0, false, 0.0);
		TestEqual(TEXT("a left click picks the stack up"), Menu->GetCarried().Count, 10);
		TestTrue(TEXT("and empties the slot"), Menu->GetStack(Main).IsEmpty());
		Input.MouseDown(Main + 1, 0, false, 1.0);
		Input.MouseUp(Main + 1, 0);
		TestEqual(TEXT("another left click places it"), Menu->GetStack(Main + 1).Count, 10);
		TestTrue(TEXT("the cursor is free"), Menu->GetCarried().IsEmpty());

		Input.MouseDown(Main + 1, 1, false, 2.0);
		TestEqual(TEXT("a right click takes half"), Menu->GetCarried().Count, 5);
		TestEqual(TEXT("and leaves half"), Menu->GetStack(Main + 1).Count, 5);
		Input.MouseDown(Main + 2, 1, false, 3.0);
		Input.MouseUp(Main + 2, 1);
		TestEqual(TEXT("right clicking an empty slot puts one down"), Menu->GetStack(Main + 2).Count, 1);
		TestEqual(TEXT("from the cursor"), Menu->GetCarried().Count, 4);
		Input.MouseDown(Main + 1, 0, false, 4.0);
		Input.MouseUp(Main + 1, 0);
		TestEqual(TEXT("a left click on the same kind merges"), Menu->GetStack(Main + 1).Count, 9);
		TestTrue(TEXT("all of it"), Menu->GetCarried().IsEmpty());

		// A different item swaps.
		Menu->SetStack(Main + 3, Stack(TEXT("minecraft:dirt"), 3));
		Input.MouseDown(Main + 1, 0, false, 5.0);
		Input.MouseDown(Main + 3, 0, false, 6.0);
		Input.MouseUp(Main + 3, 0);
		TestEqual(TEXT("clicking another item swaps them"), Menu->GetStack(Main + 3).Item, FName(TEXT("minecraft:stone")));
		TestEqual(TEXT("the dirt is in hand"), Menu->GetCarried().Item, FName(TEXT("minecraft:dirt")));
	}
	{
		// Shift-click moves between the hotbar and the main part; armour goes on; the result crafts.
		Fresh(Inventory, Armor, Offhand);
		const TSharedPtr<FMcPlayerMenu> Menu = MakeShared<FMcPlayerMenu>(Inventory, Armor, Offhand);
		FMcMenuInput Input(Menu);
		Menu->SetStack(Hot, Stack(TEXT("minecraft:stone"), 10));
		Input.MouseDown(Hot, 0, true, 0.0);
		TestEqual(TEXT("shift-click sends a hotbar stack up"), Menu->GetStack(Main).Count, 10);
		TestTrue(TEXT("out of the hotbar"), Menu->GetStack(Hot).IsEmpty());
		Input.MouseDown(Main, 0, true, 1.0);
		TestEqual(TEXT("and back down"), Menu->GetStack(Hot).Count, 10);

		Menu->SetStack(Main + 4, Stack(TEXT("minecraft:iron_chestplate"), 1));
		Input.MouseDown(Main + 4, 0, true, 2.0);
		TestEqual(TEXT("armour is put on"), Menu->GetStack(FPlayerMenuSlots::ArmorFirst + 1).Item, FName(TEXT("minecraft:iron_chestplate")));
		TestTrue(TEXT("leaving the slot"), Menu->GetStack(Main + 4).IsEmpty());
		Input.MouseDown(FPlayerMenuSlots::ArmorFirst + 1, 0, true, 3.0);
		TestTrue(TEXT("shift-click takes it off"), Menu->GetStack(FPlayerMenuSlots::ArmorFirst + 1).IsEmpty());
		TestEqual(TEXT("into the inventory"), Inventory->CountItem(FName(TEXT("minecraft:iron_chestplate"))), 1);

		// The wrong slot doesn't take it.
		Menu->SetStack(Main + 5, Stack(TEXT("minecraft:iron_chestplate"), 1));
		Input.MouseDown(Main + 5, 0, false, 4.0);
		Input.MouseDown(FPlayerMenuSlots::ArmorFirst, 0, false, 5.0);
		Input.MouseUp(FPlayerMenuSlots::ArmorFirst, 0);
		TestTrue(TEXT("a chestplate won't go on the head"), Menu->GetStack(FPlayerMenuSlots::ArmorFirst).IsEmpty());
		TestEqual(TEXT("it stays in hand"), Menu->GetCarried().Count, 1);
		Input.MouseDown(FPlayerMenuSlots::ArmorFirst + 1, 0, false, 6.0);
		Input.MouseUp(FPlayerMenuSlots::ArmorFirst + 1, 0);
		TestEqual(TEXT("but goes on the chest"), Menu->GetStack(FPlayerMenuSlots::ArmorFirst + 1).Count, 1);
		Menu->SetCarried(Stack(TEXT("minecraft:dirt"), 1));
		Input.MouseDown(FPlayerMenuSlots::Offhand, 0, false, 7.0);
		Input.MouseUp(FPlayerMenuSlots::Offhand, 0);
		TestEqual(TEXT("anything goes in the offhand"), Menu->GetStack(FPlayerMenuSlots::Offhand).Item, FName(TEXT("minecraft:dirt")));

		// Crafting: a log makes four planks; shift-click crafts until the grid runs out.
		Menu->SetStack(FPlayerMenuSlots::GridFirst, Stack(TEXT("minecraft:oak_log"), 2));
		TestEqual(TEXT("the grid shows a recipe"), Menu->GetStack(FPlayerMenuSlots::Result).Count, 4);
		Input.MouseDown(FPlayerMenuSlots::Result, 0, true, 8.0);
		TestEqual(TEXT("shift-click on the result crafts every log"), Inventory->CountItem(FName(TEXT("minecraft:oak_planks"))), 8);
		TestEqual(TEXT("using up the logs"), Menu->GetStack(FPlayerMenuSlots::GridFirst).Count, 0);
	}
	{
		// Number keys swap with the hotbar.
		Fresh(Inventory, Armor, Offhand);
		const TSharedPtr<FMcPlayerMenu> Menu = MakeShared<FMcPlayerMenu>(Inventory, Armor, Offhand);
		FMcMenuInput Input(Menu);
		Menu->SetStack(Main + 2, Stack(TEXT("minecraft:stone"), 5));
		Menu->SetStack(Hot + 1, Stack(TEXT("minecraft:dirt"), 7));
		Input.NumberKey(Main + 2, 1);
		TestEqual(TEXT("key 2 swaps with hotbar slot 2"), Menu->GetStack(Hot + 1).Item, FName(TEXT("minecraft:stone")));
		TestEqual(TEXT("the dirt moved up"), Menu->GetStack(Main + 2).Item, FName(TEXT("minecraft:dirt")));
		Input.NumberKey(Main + 2, 4);
		TestEqual(TEXT("an empty hotbar slot takes the stack"), Menu->GetStack(Hot + 4).Count, 7);
		TestTrue(TEXT("and the slot is empty"), Menu->GetStack(Main + 2).IsEmpty());
		Input.NumberKey(-1, 0);
		Input.NumberKey(Main, 12);
	}
	{
		// Dragging a stack across slots spreads it.
		Fresh(Inventory, Armor, Offhand);
		const TSharedPtr<FMcPlayerMenu> Menu = MakeShared<FMcPlayerMenu>(Inventory, Armor, Offhand);
		FMcMenuInput Input(Menu);
		Menu->SetCarried(Stack(TEXT("minecraft:stone"), 9));
		Input.MouseDown(Main, 0, false, 0.0);
		TestFalse(TEXT("nothing happens until the mouse moves or lets go"), Input.IsDragging());
		Input.MouseMove(Main);
		Input.MouseMove(Main + 1);
		TestTrue(TEXT("moving to another slot starts a drag"), Input.IsDragging());
		Input.MouseMove(Main + 2);
		Input.MouseUp(Main + 2, 0);
		TestEqual(TEXT("the stack is shared out evenly"), Menu->GetStack(Main).Count, 3);
		TestEqual(TEXT("the second"), Menu->GetStack(Main + 1).Count, 3);
		TestEqual(TEXT("the third"), Menu->GetStack(Main + 2).Count, 3);
		TestTrue(TEXT("none left over"), Menu->GetCarried().IsEmpty());
		TestFalse(TEXT("the drag is over"), Input.IsDragging());

		Menu->SetCarried(Stack(TEXT("minecraft:stone"), 9));
		Input.MouseDown(Main + 10, 1, false, 1.0);
		Input.MouseMove(Main + 11);
		Input.MouseMove(Main + 12);
		Input.MouseUp(Main + 12, 1);
		TestEqual(TEXT("a right drag puts one in each"), Menu->GetStack(Main + 10).Count, 1);
		TestEqual(TEXT("each"), Menu->GetStack(Main + 12).Count, 1);
		TestEqual(TEXT("and keeps the rest"), Menu->GetCarried().Count, 6);

		// Passing over the same slot again, or the result slot, changes nothing.
		Input.MouseDown(Main + 20, 0, false, 2.0);
		Input.MouseMove(FPlayerMenuSlots::Result);
		Input.MouseMove(Main + 21);
		Input.MouseMove(Main + 21);
		Input.MouseUp(Main + 21, 0);
		TestTrue(TEXT("the result slot takes no part in a drag"), Menu->GetStack(FPlayerMenuSlots::Result).IsEmpty());
		TestEqual(TEXT("the two others share it"), Menu->GetStack(Main + 20).Count + Menu->GetStack(Main + 21).Count, 6);
	}
	{
		// Double click gathers, Q throws, a click outside drops, closing gives back.
		Fresh(Inventory, Armor, Offhand);
		const TSharedPtr<FMcPlayerMenu> Menu = MakeShared<FMcPlayerMenu>(Inventory, Armor, Offhand);
		FMcMenuInput Input(Menu);
		Menu->SetStack(Main, Stack(TEXT("minecraft:stone"), 10));
		Menu->SetStack(Main + 1, Stack(TEXT("minecraft:stone"), 5));
		Input.MouseDown(Main, 0, false, 0.0);
		Input.MouseUp(Main, 0);
		Input.MouseDown(Main, 0, false, 0.1);
		Input.MouseUp(Main, 0);
		TestEqual(TEXT("a double click gathers the rest"), Menu->GetCarried().Count, 15);
		TestTrue(TEXT("from the other slot"), Menu->GetStack(Main + 1).IsEmpty());

		Menu->SetCarried(FMcItemStack());
		Menu->SetStack(Main + 5, Stack(TEXT("minecraft:stone"), 5));
		Input.DropKey(Main + 5, false);
		TestEqual(TEXT("Q drops one"), Menu->GetStack(Main + 5).Count, 4);
		Input.DropKey(Main + 5, true);
		TestTrue(TEXT("Ctrl-Q the lot"), Menu->GetStack(Main + 5).IsEmpty());
		const TArray<FMcItemStack> Thrown = Input.TakeDropped();
		TestEqual(TEXT("what was thrown is handed over"), Thrown.Num(), 2);
		TestEqual(TEXT("the first one"), Thrown[0].Count, 1);
		TestEqual(TEXT("then four"), Thrown[1].Count, 4);

		Menu->SetCarried(Stack(TEXT("minecraft:dirt"), 3));
		Input.MouseDown(FMcMenuInput::Outside, 0, false, 5.0);
		TestTrue(TEXT("a click outside the window drops the cursor stack"), Menu->GetCarried().IsEmpty());
		TestEqual(TEXT("which is handed over"), Input.GetDropped().Num(), 1);
		Input.TakeDropped();

		Menu->SetCarried(Stack(TEXT("minecraft:dirt"), 3));
		Menu->SetStack(FPlayerMenuSlots::GridFirst, Stack(TEXT("minecraft:stick"), 2));
		Input.Close();
		TestTrue(TEXT("closing empties the cursor"), Menu->GetCarried().IsEmpty());
		TestEqual(TEXT("it goes back in the inventory"), Inventory->CountItem(FName(TEXT("minecraft:dirt"))), 3);
		TestEqual(TEXT("so does the crafting grid"), Inventory->CountItem(FName(TEXT("minecraft:stick"))), 2);
		TestTrue(TEXT("nothing dropped"), Input.GetDropped().Num() == 0);
	}
	{
		// A chest and a furnace work the same way.
		Fresh(Inventory, Armor, Offhand);
		const TSharedPtr<FMcContainer> Chest = MakeShared<FMcContainer>(27);
		const TSharedPtr<FMcContainerMenu> ChestMenu = MakeShared<FMcContainerMenu>(Chest, Inventory);
		FMcMenuInput ChestInput(ChestMenu);
		Chest->Set(0, Stack(TEXT("minecraft:stone"), 20));
		ChestInput.MouseDown(0, 0, true, 0.0);
		TestEqual(TEXT("shift-click takes from the chest"), Inventory->CountItem(FName(TEXT("minecraft:stone"))), 20);
		TestTrue(TEXT("the chest slot is empty"), Chest->Get(0).IsEmpty());
		ChestInput.MouseDown(27 + 27 + 8, 0, true, 1.0);
		TestEqual(TEXT("and from the player to the chest"), Chest->CountItem(FName(TEXT("minecraft:stone"))), 20);

		const TSharedPtr<FMcFurnace> Furnace = MakeShared<FMcFurnace>();
		const TSharedPtr<FMcFurnaceMenu> FurnaceMenu = MakeShared<FMcFurnaceMenu>(Furnace, Inventory);
		FMcMenuInput FurnaceInput(FurnaceMenu);
		Inventory->Set(9, Stack(TEXT("minecraft:iron_ore"), 4));
		Inventory->Set(10, Stack(TEXT("minecraft:coal"), 2));
		FurnaceInput.MouseDown(3, 0, true, 2.0);
		TestEqual(TEXT("ore shift-clicks into the input"), Furnace->GetItems()->Get(FMcFurnace::InputSlot).Item, FName(TEXT("minecraft:iron_ore")));
		FurnaceInput.MouseDown(4, 0, true, 3.0);
		TestEqual(TEXT("fuel into the fuel slot"), Furnace->GetItems()->Get(FMcFurnace::FuelSlot).Item, FName(TEXT("minecraft:coal")));
		for (int32 Tick = 0; Tick < 210; ++Tick)
		{
			Furnace->Tick();
		}
		FurnaceInput.MouseDown(2, 0, false, 4.0);
		TestEqual(TEXT("and the output can be taken"), FurnaceMenu->GetCarried().Item, FName(TEXT("minecraft:iron_ingot")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCreativeScreenTest, "McClient.McUI.CreativeScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcCreativeScreenTest::RunTest(const FString& Parameters)
{
	using namespace McGui;
	const FScreenLayout Layout = MakeCreativeLayout();
	TestEqual(TEXT("size"), Layout.Size, FIntPoint(195, 136));
	TestEqual(TEXT("every menu slot is there"), Layout.Slots.Num(), FMcPlayerMenuSlots::Total);
	TestEqual(TEXT("the hotbar shows"), Layout.Slots[FMcPlayerMenuSlots::HotbarFirst], FIntPoint(9, 112));
	TestFalse(TEXT("the rest is hidden"), Layout.IsShown(FMcPlayerMenuSlots::MainFirst) || Layout.IsShown(FMcPlayerMenuSlots::ArmorFirst));
	TestEqual(TEXT("a hotbar click"), Layout.FindSlot(FIntPoint(10 + 18 * 8, 113)), FMcPlayerMenuSlots::HotbarFirst + 8);

	TestEqual(TEXT("first cell"), Creative::FindCell(FIntPoint(9, 18)), 0);
	TestEqual(TEXT("last cell"), Creative::FindCell(FIntPoint(9 + 18 * 8 + 15, 18 + 18 * 4 + 15)), 44);
	TestEqual(TEXT("left of the list"), Creative::FindCell(FIntPoint(7, 30)), -1);
	TestEqual(TEXT("below the list"), Creative::FindCell(FIntPoint(30, 18 + 18 * 5)), -1);
	TestEqual(TEXT("one screenful doesn't scroll"), Creative::MaxScroll(45), 0);
	TestEqual(TEXT("a row more scrolls one"), Creative::MaxScroll(46), 1);

	TestFalse(TEXT("no air"), Creative::IsListed(TEXT("minecraft:air")));
	TestFalse(TEXT("no command blocks"), Creative::IsListed(TEXT("minecraft:command_block")));
	TestTrue(TEXT("stone"), Creative::IsListed(TEXT("minecraft:stone")));
	const TArray<FName> Items = { TEXT("minecraft:oak_log"), TEXT("minecraft:oak_planks"), TEXT("minecraft:birch_log") };
	const auto Name = [](FName Item) { return Item.ToString().Replace(TEXT("minecraft:"), TEXT("")).Replace(TEXT("_"), TEXT(" ")); };
	TestEqual(TEXT("no search, everything"), Creative::Filter(Items, TEXT("  "), Name).Num(), 3);
	TestEqual(TEXT("one word"), Creative::Filter(Items, TEXT("LOG"), Name).Num(), 2);
	TestEqual(TEXT("every word"), Creative::Filter(Items, TEXT("log oak"), Name), TArray<FName>{ TEXT("minecraft:oak_log") });
	return true;
}

// The providers that make their characters on demand: a Unifont .hex line is cut to the columns it uses and drawn at half size;
// a TrueType font is rendered with its advance; the provider earlier in the definition has the character.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRasterFontsTest, "McClient.McUI.RasterFonts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcRasterFontsTest::RunTest(const FString& Parameters)
{
	// 0041: eight pixels wide, rows 4 to 11 with the columns 2 to 5 set; 4E00: sixteen wide, the top row all set.
	FString Hex = TEXT("0041:00000000") TEXT("3C3C3C3C3C3C3C3C") TEXT("00000000") TEXT("\n4E00:FFFF") + FString::ChrN(60, TEXT('0')) + TEXT("\n1F600:") + FString::ChrN(32, TEXT('F')) + TEXT("\n");
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(TCHAR_TO_ANSI(*Hex)), Hex.Len());

	FMcRasterFonts Fonts;
	Fonts.AddUnihex(2, Bytes, {});
	FMcRasterGlyph Glyph;
	if (TestTrue(TEXT("A is in the file"), Fonts.Make(TEXT('A'), MAX_int32, Glyph)))
	{
		TestEqual(TEXT("A is cut to its columns"), Glyph.Width, 4);
		TestEqual(TEXT("sixteen rows"), Glyph.Height, 16);
		TestEqual(TEXT("half size"), Glyph.DrawWidth, 2.0f);
		TestEqual(TEXT("drawn from the pen, as the vanilla client logs its ink box"), Glyph.OffsetX, 0.0f);
		TestEqual(TEXT("advance is half the columns and one"), Glyph.Advance, 3.0f);
		TestEqual(TEXT("a pixel of the glyph"), static_cast<int32>(Glyph.Bgra[(4 * Glyph.Width) * 4 + 3]), 255);
		TestEqual(TEXT("a pixel above it"), static_cast<int32>(Glyph.Bgra[3]), 0);
	}
	if (TestTrue(TEXT("a wide one"), Fonts.Make(TEXT('\x4E00'), MAX_int32, Glyph)))
	{
		TestEqual(TEXT("sixteen columns"), Glyph.Width, 16);
		TestEqual(TEXT("advance 9"), Glyph.Advance, 9.0f);
	}
	if (TestTrue(TEXT("a character beyond the basic plane"), Fonts.Make(0x1F600, MAX_int32, Glyph)))
	{
		TestEqual(TEXT("eight columns"), Glyph.Width, 8);
	}
	TestFalse(TEXT("not a character of the file"),Fonts.Make(TEXT('B'), MAX_int32, Glyph));
	TestFalse(TEXT("a provider after the limit is not asked"), Fonts.Make(TEXT('A'), 2, Glyph));

	const FString Roboto = FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf");
	TArray<uint8> File;
	if (TestTrue(TEXT("the engine's font is there"), FFileHelper::LoadFileToArray(File, *Roboto)))
	{
		FMcRasterFonts Type;
		Type.AddTrueType(0, File, 16.0f, 2.0f, FVector2D::ZeroVector, TEXT("B"));
		if (TestTrue(TEXT("a glyph of the ttf"), Type.Make(TEXT('A'), MAX_int32, Glyph)))
		{
			TestTrue(TEXT("it has a picture"), Glyph.Width > 0 && Glyph.Height > 0 && Glyph.Bgra.Num() == Glyph.Width * Glyph.Height * 4);
			TestTrue(TEXT("scaled down by the oversample"), FMath::IsNearlyEqual(Glyph.DrawWidth, Glyph.Width / 2.0f));
			TestTrue(TEXT("it advances"), Glyph.Advance > 3.0f && Glyph.Advance < 16.0f);
			TestTrue(TEXT("it sits on the baseline"), Glyph.OffsetY + Glyph.DrawHeight < 7.5f && Glyph.OffsetY + Glyph.DrawHeight > 6.0f);
		}
		TestFalse(TEXT("a skipped character"), Type.Make(TEXT('B'), MAX_int32, Glyph));
		if (TestTrue(TEXT("the space"), Type.Make(TEXT(' '), MAX_int32, Glyph)))
		{
			TestEqual(TEXT("has no picture"), Glyph.Width, 0);
			TestTrue(TEXT("but room"), Glyph.Advance > 0.0f);
		}
	}
	return true;
}

// The font against the vanilla client's own numbers: the advance, shadow offset and ink box of each character of a fixed string
// (ASCII, the accented and symbol sheets, Cyrillic, Greek, Unifont CJK and Hangul), read with the reference's
// "log glyphs" verb (scripts/respack_metrics.txt), and the width of the whole string.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFontMetricsTest, "McClient.McUI.FontMetrics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFontMetricsTest::RunTest(const FString& Parameters)
{
	struct FRow { int32 Char; float Advance; float Shadow; };
	static const FRow Vanilla[] = {
		{ 0x0041, 6.0f, 1.0f },
		{ 0x0062, 6.0f, 1.0f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x0057, 6.0f, 1.0f },
		{ 0x0069, 2.0f, 1.0f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x0030, 6.0f, 1.0f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x002E, 2.0f, 1.0f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x002C, 2.0f, 1.0f },
		{ 0x0069, 2.0f, 1.0f },
		{ 0x00DC, 6.0f, 1.0f },
		{ 0x006E, 6.0f, 1.0f },
		{ 0x00EF, 4.0f, 1.0f },
		{ 0x0063, 6.0f, 1.0f },
		{ 0x00F6, 6.0f, 1.0f },
		{ 0x0064, 6.0f, 1.0f },
		{ 0x00E9, 6.0f, 1.0f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x00F1, 6.0f, 1.0f },
		{ 0x00DF, 6.0f, 1.0f },
		{ 0x00E6, 10.0f, 1.0f },
		{ 0x00F8, 6.0f, 1.0f },
		{ 0x20AC, 7.0f, 1.0f },
		{ 0x00A3, 6.0f, 1.0f },
		{ 0x00A9, 8.0f, 1.0f },
		{ 0x00B1, 6.0f, 1.0f },
		{ 0x00D7, 6.0f, 1.0f },
		{ 0x2026, 8.0f, 1.0f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x041F, 6.0f, 1.0f },
		{ 0x0440, 6.0f, 1.0f },
		{ 0x0438, 6.0f, 1.0f },
		{ 0x0432, 6.0f, 1.0f },
		{ 0x0435, 6.0f, 1.0f },
		{ 0x0442, 6.0f, 1.0f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x03B1, 6.0f, 1.0f },
		{ 0x03B2, 6.0f, 1.0f },
		{ 0x03B3, 6.0f, 1.0f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x65E5, 9.0f, 0.5f },
		{ 0x672C, 9.0f, 0.5f },
		{ 0x8A9E, 9.0f, 0.5f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0xD55C, 8.0f, 0.5f },
		{ 0xAD6D, 8.0f, 0.5f },
		{ 0xC5B4, 8.0f, 0.5f },
		{ 0x0020, 4.0f, 1.0f },
		{ 0x2603, 8.0f, 1.0f },
		{ 0x2605, 8.0f, 1.0f },
		{ 0x2665, 6.0f, 1.0f }
	};
	AMcHUD* Hud = NewObject<AMcHUD>(GetTransientPackage());
	FString Text;
	for (const FRow& Row : Vanilla)
	{
		Text.AppendChar(static_cast<TCHAR>(Row.Char));
		TestEqual(*FString::Printf(TEXT("advance of U+%04X"), Row.Char), Hud->GetGlyphAdvance(Row.Char), Row.Advance);
		TestEqual(*FString::Printf(TEXT("shadow of U+%04X"), Row.Char), Hud->GetGlyphShadowOffset(Row.Char), Row.Shadow);
	}
	struct FInk { int32 Char; float Left; float Right; float Up; float Down; };
	static const FInk DefaultInk[] = {
		{ 0x0041, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0062, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0057, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0069, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0030, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x002E, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x002C, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00DC, 0.0f, 9.0f, -3.0f, 9.0f },
		{ 0x006E, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00EF, 0.0f, 9.0f, -3.0f, 9.0f },
		{ 0x0063, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00F6, 0.0f, 9.0f, -3.0f, 9.0f },
		{ 0x0064, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00E9, 0.0f, 9.0f, -3.0f, 9.0f },
		{ 0x00F1, 0.0f, 9.0f, -3.0f, 9.0f },
		{ 0x00DF, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00E6, 0.0f, 9.0f, -3.0f, 9.0f },
		{ 0x00F8, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x20AC, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00A3, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00A9, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00B1, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x00D7, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x2026, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x041F, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0440, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0438, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0432, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0435, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x0442, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x03B1, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x03B2, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x03B3, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x65E5, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x672C, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x8A9E, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0xD55C, 0.0f, 7.5f, 0.0f, 8.0f },
		{ 0xAD6D, 0.0f, 7.5f, 0.0f, 8.0f },
		{ 0xC5B4, 0.0f, 7.5f, 0.0f, 8.0f },
		{ 0x2603, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x2605, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x2665, 0.0f, 8.0f, 0.0f, 8.0f }
	};
	AMcHUD* InkHud = Hud;
	for (const FInk& Ink : DefaultInk)
	{
		float Left = 0, Right = 0, Up = 0, Down = 0;
		const bool bInk = InkHud->GetGlyphInk(Ink.Char, Left, Right, Up, Down);
		TestTrue(*FString::Printf(TEXT("U+%04X has a picture"), Ink.Char), bInk);
		const FString Name = FString::Printf(TEXT("U+%04X"), Ink.Char);
		TestTrue(*(Name + TEXT(" left ") + FString::SanitizeFloat(Left)), FMath::IsNearlyEqual(Left, Ink.Left, 0.01f));
		TestTrue(*(Name + TEXT(" right ") + FString::SanitizeFloat(Right)), FMath::IsNearlyEqual(Right, Ink.Right, 0.01f));
		TestTrue(*(Name + TEXT(" up ") + FString::SanitizeFloat(Up)), FMath::IsNearlyEqual(Up, Ink.Up, 0.01f));
		TestTrue(*(Name + TEXT(" down ") + FString::SanitizeFloat(Down)), FMath::IsNearlyEqual(Down, Ink.Down, 0.01f));
	}
	TestTrue(TEXT("the whole string is 308 wide, as the game measures it"), FMath::IsNearlyEqual(Hud->GetTextWidth(Text), 308.0f, 1.0f));

	// More characters the vanilla client was asked about (scripts/respack_unihex.txt): some come from Unifont and are cut to
	// their columns (Thai, braille), the rest from the sheets. Every picture is drawn from the pen, and its left is 0.
	struct FMore { int32 Char; float Advance; float Shadow; float Right; };
	static const FMore More[] = {
		{ 0x0250, 6.0f, 1.0f, 8.0f },
		{ 0x2200, 8.0f, 1.0f, 8.0f },
		{ 0x0E01, 4.0f, 0.5f, 3.0f },
		{ 0x0561, 6.0f, 1.0f, 8.0f },
		{ 0x283F, 3.0f, 0.5f, 2.5f },
		{ 0x10D0, 6.0f, 1.0f, 8.0f },
		{ 0x0500, 6.0f, 1.0f, 8.0f },
		{ 0x4E00, 9.0f, 0.5f, 8.0f },
		{ 0x3042, 9.0f, 0.5f, 8.0f },
		{ 0x0928, 9.0f, 0.5f, 8.0f }
	};
	for (const FMore& Row : More)
	{
		const FString Name = FString::Printf(TEXT("U+%04X"), Row.Char);
		float Left = 0, Right = 0, Up = 0, Down = 0;
		TestTrue(*(Name + TEXT(" has a picture")), Hud->GetGlyphInk(Row.Char, Left, Right, Up, Down));
		TestEqual(*(Name + TEXT(" advance")), Hud->GetGlyphAdvance(Row.Char), Row.Advance);
		TestEqual(*(Name + TEXT(" shadow")), Hud->GetGlyphShadowOffset(Row.Char), Row.Shadow);
		TestTrue(*(Name + TEXT(" left ") + FString::SanitizeFloat(Left)), FMath::IsNearlyEqual(Left, 0.0f, 0.01f));
		TestTrue(*(Name + TEXT(" right ") + FString::SanitizeFloat(Right)), FMath::IsNearlyEqual(Right, Row.Right, 0.01f));
		TestTrue(*(Name + TEXT(" up ") + FString::SanitizeFloat(Up)), FMath::IsNearlyEqual(Up, 0.0f, 0.01f));
		TestTrue(*(Name + TEXT(" down ") + FString::SanitizeFloat(Down)), FMath::IsNearlyEqual(Down, 8.0f, 0.01f));
	}
	return true;
}

// The same with the ttf pack the reference tests use (Cascadia Mono, size 10, oversample 4): the advances are whole
// pixels of the oversampled glyph. Skipped where that pack is not in the resourcepacks folder (it lives on the 1070).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFontMetricsTrueTypeTest, "McClient.McUI.FontMetricsTrueType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFontMetricsTrueTypeTest::RunTest(const FString& Parameters)
{
	bool bHasPack = false;
	for (const McPacks::FFolderEntry& Entry : McPacks::ScanFolder())
	{
		bHasPack |= Entry.Name == TEXT("zz_test_ttf") && Entry.Pack.IsValid();
	}
	if (!bHasPack)
	{
		AddInfo(TEXT("zz_test_ttf is not in the resourcepacks folder: skipped"));
		return true;
	}
	struct FRow { int32 Char; float Advance; float Shadow; };
	static const FRow Vanilla[] = {
		{ 0x0041, 5.75f, 1.0f },
		{ 0x0062, 5.75f, 1.0f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x0057, 5.75f, 1.0f },
		{ 0x0069, 5.75f, 1.0f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x0030, 5.75f, 1.0f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x002E, 5.75f, 1.0f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x002C, 5.75f, 1.0f },
		{ 0x0069, 5.75f, 1.0f },
		{ 0x00DC, 5.75f, 1.0f },
		{ 0x006E, 5.75f, 1.0f },
		{ 0x00EF, 5.75f, 1.0f },
		{ 0x0063, 5.75f, 1.0f },
		{ 0x00F6, 5.75f, 1.0f },
		{ 0x0064, 5.75f, 1.0f },
		{ 0x00E9, 5.75f, 1.0f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x00F1, 5.75f, 1.0f },
		{ 0x00DF, 5.75f, 1.0f },
		{ 0x00E6, 5.75f, 1.0f },
		{ 0x00F8, 5.75f, 1.0f },
		{ 0x20AC, 5.75f, 1.0f },
		{ 0x00A3, 5.75f, 1.0f },
		{ 0x00A9, 5.75f, 1.0f },
		{ 0x00B1, 5.75f, 1.0f },
		{ 0x00D7, 5.75f, 1.0f },
		{ 0x2026, 5.75f, 1.0f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x041F, 5.75f, 1.0f },
		{ 0x0440, 5.75f, 1.0f },
		{ 0x0438, 5.75f, 1.0f },
		{ 0x0432, 5.75f, 1.0f },
		{ 0x0435, 5.75f, 1.0f },
		{ 0x0442, 5.75f, 1.0f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x03B1, 5.75f, 1.0f },
		{ 0x03B2, 5.75f, 1.0f },
		{ 0x03B3, 5.75f, 1.0f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x65E5, 9.00f, 0.5f },
		{ 0x672C, 9.00f, 0.5f },
		{ 0x8A9E, 9.00f, 0.5f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0xD55C, 8.00f, 0.5f },
		{ 0xAD6D, 8.00f, 0.5f },
		{ 0xC5B4, 8.00f, 0.5f },
		{ 0x0020, 5.75f, 1.0f },
		{ 0x2603, 8.00f, 1.0f },
		{ 0x2605, 8.00f, 1.0f },
		{ 0x2665, 5.75f, 1.0f }
	};
	const TArray<FString> Before = McPacks::GetSelectedNames();
	McPacks::ApplySelection({ TEXT("zz_test_ttf") });
	AMcHUD* Hud = NewObject<AMcHUD>(GetTransientPackage());
	FString Text;
	for (const FRow& Row : Vanilla)
	{
		Text.AppendChar(static_cast<TCHAR>(Row.Char));
		TestEqual(*FString::Printf(TEXT("advance of U+%04X"), Row.Char), Hud->GetGlyphAdvance(Row.Char), Row.Advance);
		TestEqual(*FString::Printf(TEXT("shadow of U+%04X"), Row.Char), Hud->GetGlyphShadowOffset(Row.Char), Row.Shadow);
	}
	struct FInk { int32 Char; float Left; float Right; float Up; float Down; };
	static const FInk TrueTypeInk[] = {
		{ 0x0041, 0.0f, 5.75f, 0.0f, 7.0f },
		{ 0x0062, 0.75f, 5.5f, -0.25f, 7.0f },
		{ 0x0057, 0.0f, 6.0f, 0.0f, 7.0f },
		{ 0x0069, 0.75f, 5.5f, -0.5f, 7.0f },
		{ 0x0030, 0.5f, 5.5f, 0.0f, 7.0f },
		{ 0x002E, 2.0f, 3.75f, 5.5f, 7.0f },
		{ 0x002C, 2.0f, 3.75f, 5.75f, 9.25f },
		{ 0x00DC, 0.5f, 5.25f, -2.25f, 7.0f },
		{ 0x006E, 0.75f, 5.25f, 1.75f, 7.0f },
		{ 0x00EF, 0.75f, 5.5f, -0.5f, 7.0f },
		{ 0x0063, 0.5f, 5.5f, 1.75f, 7.0f },
		{ 0x00F6, 0.5f, 5.25f, -0.5f, 7.0f },
		{ 0x0064, 0.25f, 5.25f, -0.25f, 7.25f },
		{ 0x00E9, 0.5f, 5.25f, -1.0f, 7.0f },
		{ 0x00F1, 0.75f, 5.25f, -1.0f, 7.0f },
		{ 0x00DF, 0.25f, 5.75f, -0.25f, 7.0f },
		{ 0x00E6, 0.0f, 5.75f, 1.75f, 7.0f },
		{ 0x00F8, 0.5f, 5.25f, 1.25f, 7.5f },
		{ 0x20AC, 0.25f, 5.75f, 0.0f, 7.0f },
		{ 0x00A3, 0.5f, 5.5f, 0.0f, 7.0f },
		{ 0x00A9, 0.0f, 5.75f, 0.25f, 7.25f },
		{ 0x00B1, 0.25f, 5.5f, 1.0f, 6.25f },
		{ 0x00D7, 0.5f, 5.5f, 1.25f, 6.25f },
		{ 0x2026, 0.0f, 5.75f, 5.5f, 7.0f },
		{ 0x041F, 0.5f, 5.25f, 0.0f, 7.0f },
		{ 0x0440, 0.75f, 5.5f, 1.75f, 9.25f },
		{ 0x0438, 0.75f, 5.25f, 1.75f, 7.0f },
		{ 0x0432, 0.75f, 5.5f, 1.75f, 7.0f },
		{ 0x0435, 0.5f, 5.25f, 1.75f, 7.0f },
		{ 0x0442, 0.25f, 5.5f, 1.75f, 7.0f },
		{ 0x03B1, 0.25f, 5.75f, 1.75f, 7.25f },
		{ 0x03B2, 0.75f, 5.25f, 0.0f, 9.25f },
		{ 0x03B3, 0.0f, 5.75f, 1.75f, 9.25f },
		{ 0x65E5, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x672C, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x8A9E, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0xD55C, 0.0f, 7.5f, 0.0f, 8.0f },
		{ 0xAD6D, 0.0f, 7.5f, 0.0f, 8.0f },
		{ 0xC5B4, 0.0f, 7.5f, 0.0f, 8.0f },
		{ 0x2603, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x2605, 0.0f, 8.0f, 0.0f, 8.0f },
		{ 0x2665, 0.25f, 5.75f, 0.75f, 6.75f }
	};
	AMcHUD* InkHud = Hud;
	for (const FInk& Ink : TrueTypeInk)
	{
		float Left = 0, Right = 0, Up = 0, Down = 0;
		const bool bInk = InkHud->GetGlyphInk(Ink.Char, Left, Right, Up, Down);
		TestTrue(*FString::Printf(TEXT("U+%04X has a picture"), Ink.Char), bInk);
		const FString Name = FString::Printf(TEXT("U+%04X"), Ink.Char);
		TestTrue(*(Name + TEXT(" left ") + FString::SanitizeFloat(Left)), FMath::IsNearlyEqual(Left, Ink.Left, 0.3f));
		TestTrue(*(Name + TEXT(" right ") + FString::SanitizeFloat(Right)), FMath::IsNearlyEqual(Right, Ink.Right, 0.3f));
		TestTrue(*(Name + TEXT(" up ") + FString::SanitizeFloat(Up)), FMath::IsNearlyEqual(Up, Ink.Up, 0.3f));
		TestTrue(*(Name + TEXT(" down ") + FString::SanitizeFloat(Down)), FMath::IsNearlyEqual(Down, Ink.Down, 0.3f));
	}
	TestTrue(TEXT("the whole string is 326 wide, as the game measures it"), FMath::IsNearlyEqual(Hud->GetTextWidth(Text), 326.0f, 1.0f));
	McPacks::ApplySelection(Before);
	return true;
}

// The title screen's splash text comes from texts/splashes.txt, and a pack's copy replaces the game's; three days have a
// splash of their own.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSplashTest, "McClient.McUI.Splash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSplashTest::RunTest(const FString& Parameters)
{
	McPacks::SetStack(MakeShared<FMcPackStack>());
	const TArray<FString> Vanilla = AMcHUD::LoadSplashTexts();
	if (TestTrue(TEXT("the game has splashes"), Vanilla.Num() > 100))
	{
		TestFalse(TEXT("none of them empty"), Vanilla.ContainsByPredicate([](const FString& Line) { return Line.IsEmpty(); }));
	}

	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McSplashTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97}}"), *(Root / TEXT("pack.mcmeta")));
	FFileHelper::SaveStringToFile(TEXT("First splash \r\n\r\n  Second one\r\nThird!\r\n"), *(Root / TEXT("assets/minecraft/texts/splashes.txt")));
	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));
	const TArray<FString> FromPack = AMcHUD::LoadSplashTexts();
	if (TestEqual(TEXT("the pack's file replaces the game's"), FromPack.Num(), 3))
	{
		TestEqual(TEXT("lines are trimmed"), FromPack[0], FString(TEXT("First splash")));
		TestEqual(TEXT("empty lines are dropped"), FromPack[1], FString(TEXT("Second one")));
		TestEqual(TEXT("first by a low roll"), AMcHUD::PickSplash(FromPack, 6, 15, 0.0f), FString(TEXT("First splash")));
		TestEqual(TEXT("last by a high roll"), AMcHUD::PickSplash(FromPack, 6, 15, 0.999f), FString(TEXT("Third!")));
		TestEqual(TEXT("a roll of 1 stays in the list"), AMcHUD::PickSplash(FromPack, 6, 15, 1.0f), FString(TEXT("Third!")));
		TestEqual(TEXT("Christmas Eve"), AMcHUD::PickSplash(FromPack, 12, 24, 0.0f), FString(TEXT("Merry X-mas!")));
		TestEqual(TEXT("New Year's Day"), AMcHUD::PickSplash(FromPack, 1, 1, 0.0f), FString(TEXT("Happy new year!")));
		TestEqual(TEXT("Halloween"), AMcHUD::PickSplash(FromPack, 10, 31, 0.0f), FString(TEXT("OOoooOOOoooo! Spooky!")));
	}
	TestEqual(TEXT("no lines, no splash"), AMcHUD::PickSplash(TArray<FString>(), 6, 15, 0.5f), FString());
	McPacks::SetStack(MakeShared<FMcPackStack>());
	TestEqual(TEXT("without the pack the game's again"), AMcHUD::LoadSplashTexts().Num(), Vanilla.Num());
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// A sprite's .png.mcmeta "gui.scaling": stretch (the default), tile, and nine_slice with one border or four. And the title
// panorama, which the game reads from the asset index, is replaced by a pack's copy of the same path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackGuiTest, "McClient.McUI.PackGui",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPackGuiTest::RunTest(const FString& Parameters)
{
	using FScaling = AMcHUD::FGuiScaling;
	const FScaling None = AMcHUD::ParseGuiScaling(TEXT("{}"), 200.0f, 20.0f);
	TestTrue(TEXT("no gui section stretches"), None.Type == FScaling::EType::Stretch);
	const FScaling Plain = AMcHUD::ParseGuiScaling(TEXT("{\"gui\":{\"scaling\":{\"type\":\"stretch\"}}}"), 200.0f, 20.0f);
	TestTrue(TEXT("stretch"), Plain.Type == FScaling::EType::Stretch);
	const FScaling Tile = AMcHUD::ParseGuiScaling(TEXT("{\"gui\":{\"scaling\":{\"type\":\"tile\",\"width\":16,\"height\":8}}}"), 64.0f, 64.0f);
	TestTrue(TEXT("tile"), Tile.Type == FScaling::EType::Tile);
	TestEqual(TEXT("tile width"), Tile.Width, 16.0f);
	TestEqual(TEXT("tile height"), Tile.Height, 8.0f);
	const FScaling One = AMcHUD::ParseGuiScaling(TEXT("{\"gui\":{\"scaling\":{\"type\":\"minecraft:nine_slice\",\"width\":200,\"height\":20,\"border\":3}}}"), 400.0f, 40.0f);
	TestTrue(TEXT("nine slice, namespaced type"), One.Type == FScaling::EType::NineSlice);
	TestEqual(TEXT("declared size, not the picture's"), One.Width, 200.0f);
	TestEqual(TEXT("the one border is all four"), One.Left + One.Top + One.Right + One.Bottom, 12.0f);
	const FScaling Four = AMcHUD::ParseGuiScaling(TEXT("{\"gui\":{\"scaling\":{\"type\":\"nine_slice\",\"border\":{\"left\":1,\"top\":2,\"right\":3,\"bottom\":4}}}}"), 32.0f, 24.0f);
	TestEqual(TEXT("border left"), Four.Left, 1.0f);
	TestEqual(TEXT("border top"), Four.Top, 2.0f);
	TestEqual(TEXT("border right"), Four.Right, 3.0f);
	TestEqual(TEXT("border bottom"), Four.Bottom, 4.0f);
	TestEqual(TEXT("size defaults to the picture's"), Four.Width, 32.0f);

	McPacks::SetStack(MakeShared<FMcPackStack>());
	const FString Panorama = TEXT("resources:textures/gui/title/background/panorama_0.png");
	const FString GamePath = AMcHUD::SpritePath(Panorama);
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackGuiTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97}}"), *(Root / TEXT("pack.mcmeta")));
	const TArray<uint8> Bytes = { 0x89, 'P', 'N', 'G' };
	FFileHelper::SaveArrayToFile(Bytes, *(Root / TEXT("assets/minecraft/textures/gui/title/background/panorama_0.png")));
	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));
	const FString PackPath = AMcHUD::SpritePath(Panorama);
	TestNotEqual(TEXT("the pack's panorama replaces the game's"), PackPath, GamePath);
	TestTrue(TEXT("and is read from the pack"), PackPath.Contains(TEXT("McPackGuiTest")));
	McPacks::SetStack(MakeShared<FMcPackStack>());
	TestEqual(TEXT("without the pack the game's again"), AMcHUD::SpritePath(Panorama), GamePath);
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// An item definition picks its model from the stack: select and range_dispatch on custom_model_data, condition on damaged,
// composite giving two models, tints read, an unknown property taking the fallback.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcItemDefinitionTest, "McClient.McUI.ItemDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcItemDefinitionTest::RunTest(const FString& Parameters)
{
	const auto Parse = [](const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text)), Json);
		return Json;
	};
	const auto Models = [](const TArray<McItemDef::FModelRef>& Refs)
	{
		FString Text;
		for (const McItemDef::FModelRef& Ref : Refs)
		{
			Text += Ref.Model + TEXT(";");
		}
		return Text;
	};
	const auto WithData = [](const TCHAR* DataJson)
	{
		FMcItemStack Stack(TEXT("minecraft:stick"), 1);
		TSharedPtr<FJsonValue> Value;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(DataJson)), Value);
		Stack.Set(TEXT("minecraft:custom_model_data"), Value);
		return Stack;
	};
	const TSharedPtr<FJsonObject> Select = Parse(TEXT("{\"model\":{\"type\":\"minecraft:select\",\"property\":\"minecraft:custom_model_data\",\"cases\":[{\"when\":\"blue\",\"model\":{\"type\":\"minecraft:model\",\"model\":\"m:blue\",\"tints\":[{\"type\":\"minecraft:constant\",\"value\":255}]}},{\"when\":[\"a\",\"b\"],\"model\":{\"type\":\"minecraft:model\",\"model\":\"m:ab\"}}],"
		"\"fallback\":{\"type\":\"minecraft:range_dispatch\",\"property\":\"minecraft:custom_model_data\",\"entries\":[{\"threshold\":1,\"model\":{\"type\":\"minecraft:model\",\"model\":\"m:one\"}},{\"threshold\":2,\"model\":{\"type\":\"minecraft:model\",\"model\":\"m:two\"}}],\"fallback\":{\"type\":\"minecraft:model\",\"model\":\"m:none\"}}}}"));
	using McItemDef::EContext;
	TestEqual(TEXT("a string case"), Models(McItemDef::ResolveJson(Select, WithData(TEXT("{\"strings\":[\"blue\"]}")), EContext::Gui)), FString(TEXT("m:blue;")));
	const TArray<McItemDef::FModelRef> Blue = McItemDef::ResolveJson(Select, WithData(TEXT("{\"strings\":[\"blue\"]}")), EContext::Gui);
	TestTrue(TEXT("its constant tint, opaque blue"), Blue.Num() == 1 && Blue[0].Tints.Num() == 1 && Blue[0].Tints[0] == 0xFF0000FFu);
	TestEqual(TEXT("a case with a list"), Models(McItemDef::ResolveJson(Select, WithData(TEXT("{\"strings\":[\"b\"]}")), EContext::Gui)), FString(TEXT("m:ab;")));
	TestEqual(TEXT("no string: range_dispatch, float 1"), Models(McItemDef::ResolveJson(Select, WithData(TEXT("{\"floats\":[1.0]}")), EContext::Gui)), FString(TEXT("m:one;")));
	TestEqual(TEXT("float 1.5 takes the lower threshold"), Models(McItemDef::ResolveJson(Select, WithData(TEXT("{\"floats\":[1.5]}")), EContext::Gui)), FString(TEXT("m:one;")));
	TestEqual(TEXT("float 2"), Models(McItemDef::ResolveJson(Select, WithData(TEXT("{\"floats\":[2.0]}")), EContext::Gui)), FString(TEXT("m:two;")));
	TestEqual(TEXT("float below every threshold: the fallback"), Models(McItemDef::ResolveJson(Select, WithData(TEXT("{\"floats\":[0.5]}")), EContext::Gui)), FString(TEXT("m:none;")));
	TestEqual(TEXT("no data: the fallback"), Models(McItemDef::ResolveJson(Select, FMcItemStack(TEXT("minecraft:stick"), 1), EContext::Gui)), FString(TEXT("m:none;")));

	const TSharedPtr<FJsonObject> Condition = Parse(TEXT("{\"model\":{\"type\":\"minecraft:condition\",\"property\":\"minecraft:custom_model_data\",\"index\":1,\"on_true\":{\"type\":\"minecraft:composite\",\"models\":[{\"type\":\"minecraft:model\",\"model\":\"m:a\"},{\"type\":\"minecraft:model\",\"model\":\"m:b\"}]},\"on_false\":{\"type\":\"minecraft:empty\"}}}"));
	TestEqual(TEXT("a flag set: the composite's two models"), Models(McItemDef::ResolveJson(Condition, WithData(TEXT("{\"flags\":[false,true]}")), EContext::Gui)), FString(TEXT("m:a;m:b;")));
	TestEqual(TEXT("a flag unset: empty"), Models(McItemDef::ResolveJson(Condition, WithData(TEXT("{\"flags\":[true,false]}")), EContext::Gui)), FString());

	const TSharedPtr<FJsonObject> Context = Parse(TEXT("{\"model\":{\"type\":\"minecraft:select\",\"property\":\"minecraft:display_context\",\"cases\":[{\"when\":\"gui\",\"model\":{\"type\":\"minecraft:model\",\"model\":\"m:gui\"}}],\"fallback\":{\"type\":\"minecraft:model\",\"model\":\"m:other\"}}}"));
	TestEqual(TEXT("display_context gui"), Models(McItemDef::ResolveJson(Context, FMcItemStack(TEXT("minecraft:stick"), 1), EContext::Gui)), FString(TEXT("m:gui;")));
	TestEqual(TEXT("display_context hand"), Models(McItemDef::ResolveJson(Context, FMcItemStack(TEXT("minecraft:stick"), 1), EContext::FirstPersonRightHand)), FString(TEXT("m:other;")));

	const TSharedPtr<FJsonObject> Unknown = Parse(TEXT("{\"model\":{\"type\":\"minecraft:range_dispatch\",\"property\":\"minecraft:compass\",\"entries\":[{\"threshold\":0.5,\"model\":{\"type\":\"minecraft:model\",\"model\":\"m:never\"}}],\"fallback\":{\"type\":\"minecraft:model\",\"model\":\"m:fallback\"}}}"));
	TestEqual(TEXT("an unread property reads 0: the fallback"), Models(McItemDef::ResolveJson(Unknown, FMcItemStack(TEXT("minecraft:stick"), 1), EContext::Gui)), FString(TEXT("m:fallback;")));
	return true;
}

#endif
