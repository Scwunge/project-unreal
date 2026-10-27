#include "McContainer.h"
#include "McCraftingMenu.h"
#include "McFurnace.h"
#include "McGuiLayout.h"
#include "McItemRegistry.h"
#include "McMenuInput.h"
#include "McPlayerData.h"
#include "McRecipe.h"
#include "McScreenLayout.h"
#include "Misc/AutomationTest.h"

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

#endif
