#include "Misc/AutomationTest.h"
#include "McContainer.h"
#include "McMapStore.h"
#include "McWorkstationMenus.h"

// The cartography table's rules from minecraft.wiki "Cartography Table".
namespace
{
	FMcItemStack MapItem(int32 Id)
	{
		FMcItemStack Stack(FName(TEXT("minecraft:filled_map")), 1);
		McMapStore::SetId(Stack, Id);
		return Stack;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCartographyRulesTest, "Mc.Cartography.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcCartographyRulesTest::RunTest(const FString&)
{
	McMapStore::Reset();
	const int32 Id = McMapStore::Create(0, 0, 0, TEXT("minecraft:overworld"));
	const FMcItemStack Map = MapItem(Id);
	const FMcItemStack Paper(FName(TEXT("minecraft:paper")), 1);
	const FMcItemStack Empty(FName(TEXT("minecraft:map")), 1);
	const FMcItemStack Pane(FName(TEXT("minecraft:glass_pane")), 1);
	using Op = FMcCartographyMenu::EOperation;

	TestTrue(TEXT("paper zooms"), FMcCartographyMenu::GetOperation(Map, Paper) == Op::Zoom);
	TestTrue(TEXT("an empty map copies"), FMcCartographyMenu::GetOperation(Map, Empty) == Op::Copy);
	TestTrue(TEXT("a pane locks"), FMcCartographyMenu::GetOperation(Map, Pane) == Op::Lock);
	TestTrue(TEXT("a stick does nothing"), FMcCartographyMenu::GetOperation(Map, FMcItemStack(FName(TEXT("minecraft:stick")), 1)) == Op::None);
	TestTrue(TEXT("nothing without a material"), FMcCartographyMenu::GetOperation(Map, FMcItemStack()) == Op::None);

	const FMcItemStack Zoomed = FMcCartographyMenu::GetPreview(Map, Paper);
	TestEqual(TEXT("zoomed out one scale"), McMapStore::Find(McMapStore::GetId(Zoomed))->Scale, 1);
	TestEqual(TEXT("a copy is two maps"), FMcCartographyMenu::GetPreview(Map, Empty).Count, 2);
	TestEqual(TEXT("sharing the id"), McMapStore::GetId(FMcCartographyMenu::GetPreview(Map, Empty)), Id);

	// A locked map cannot be locked or zoomed again, but can be copied.
	McMapStore::Lock(Id);
	TestTrue(TEXT("locked: no zoom"), FMcCartographyMenu::GetOperation(Map, Paper) == Op::None);
	TestTrue(TEXT("locked: no second lock"), FMcCartographyMenu::GetOperation(Map, Pane) == Op::None);
	TestTrue(TEXT("locked: copy"), FMcCartographyMenu::GetOperation(Map, Empty) == Op::Copy);

	// The largest scale does not zoom.
	const int32 Big = McMapStore::Create(4, 0, 0, TEXT("minecraft:overworld"));
	TestTrue(TEXT("scale 4: no zoom"), FMcCartographyMenu::GetOperation(MapItem(Big), Paper) == Op::None);
	McMapStore::Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCartographyClicksTest, "Mc.Cartography.Clicks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcCartographyClicksTest::RunTest(const FString&)
{
	McMapStore::Reset();
	const int32 Id = McMapStore::Create(0, 0, 0, TEXT("minecraft:overworld"));
	const TSharedPtr<FMcContainer> Inventory = MakeShared<FMcContainer>(41);
	FMcCartographyMenu Menu(Inventory);
	TArray<FMcItemStack> Dropped;
	const int32 PlayerStart = 3;

	// A map goes only in the map slot, paper only in the material slot.
	Inventory->Set(0, MapItem(Id));
	Inventory->Set(1, FMcItemStack(FName(TEXT("minecraft:glass_pane")), 3));
	Inventory->Set(2, FMcItemStack(FName(TEXT("minecraft:dirt")), 3));
	// Find the slots holding them (the menu lists the player's slots after its own).
	int32 MapAt = INDEX_NONE, PaneAt = INDEX_NONE, DirtAt = INDEX_NONE;
	for (int32 Slot = PlayerStart; Slot < Menu.NumSlots(); ++Slot)
	{
		const FName Item = Menu.GetStack(Slot).Item;
		if (Item == FName(TEXT("minecraft:filled_map"))) { MapAt = Slot; }
		if (Item == FName(TEXT("minecraft:glass_pane"))) { PaneAt = Slot; }
		if (Item == FName(TEXT("minecraft:dirt"))) { DirtAt = Slot; }
	}
	TestTrue(TEXT("found the items"), MapAt != INDEX_NONE && PaneAt != INDEX_NONE && DirtAt != INDEX_NONE);

	Menu.Click(DirtAt, 0, EMcClickType::Pickup, Dropped);
	Menu.Click(FMcCartographyMenu::MaterialSlot, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("dirt is refused"), Menu.GetStack(FMcCartographyMenu::MaterialSlot).IsEmpty());
	Menu.Click(DirtAt, 0, EMcClickType::Pickup, Dropped);

	Menu.Click(MapAt, 0, EMcClickType::Pickup, Dropped);
	Menu.Click(FMcCartographyMenu::MapSlot, 0, EMcClickType::Pickup, Dropped);
	Menu.Click(PaneAt, 0, EMcClickType::Pickup, Dropped);
	Menu.Click(FMcCartographyMenu::MaterialSlot, 0, EMcClickType::Pickup, Dropped);
	TestEqual(TEXT("a locked copy is offered"), Menu.GetStack(FMcCartographyMenu::ResultSlot).Item, FName(TEXT("minecraft:filled_map")));

	// Taking it makes a new locked map and leaves the original unlocked; one pane of three is used.
	Menu.Click(FMcCartographyMenu::ResultSlot, 0, EMcClickType::Pickup, Dropped);
	const int32 NewId = McMapStore::GetId(Menu.GetCarried());
	TestTrue(TEXT("a new map"), NewId != INDEX_NONE && NewId != Id);
	TestTrue(TEXT("the copy is locked"), McMapStore::Find(NewId) && McMapStore::Find(NewId)->bLocked);
	TestFalse(TEXT("the original is not"), McMapStore::Find(Id)->bLocked);
	TestTrue(TEXT("the map slot is used up"), Menu.GetStack(FMcCartographyMenu::MapSlot).IsEmpty());
	TestEqual(TEXT("two panes left"), Menu.GetStack(FMcCartographyMenu::MaterialSlot).Count, 2);
	TestTrue(TEXT("no result without a map"), Menu.GetStack(FMcCartographyMenu::ResultSlot).IsEmpty());
	McMapStore::Reset();
	return true;
}
