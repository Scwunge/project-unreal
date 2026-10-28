#include "McHopper.h"
#include "McBrewingStand.h"
#include "McFurnace.h"
#include "McItemRegistry.h"
#include "McRecipe.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHopperTest, "McClient.McItem.Hopper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcHopperTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()) || !TestTrue(TEXT("recipes load"), FMcRecipeBook::Get().EnsureLoaded()))
	{
		return false;
	}
	using McHopper::EFace;
	const TSharedPtr<FMcContainer> Chest = MakeShared<FMcContainer>(27);
	const TSharedPtr<FMcContainer> Hopper = MakeShared<FMcContainer>(McHopper::NumSlots);
	const FMcSlotAccess ChestAccess = McHopper::ForContainer(Chest);
	const FMcSlotAccess HopperAccess = McHopper::ForContainer(Hopper);

	// One item at a time, from the first slot that has any.
	Chest->Set(3, FMcItemStack(TEXT("minecraft:cobblestone"), 2));
	Chest->Set(5, FMcItemStack(TEXT("minecraft:dirt"), 1));
	TestTrue(TEXT("one moves"), McHopper::MoveOne(ChestAccess, HopperAccess));
	TestEqual(TEXT("a cobblestone left in the chest"), Chest->Get(3).Count, 1);
	TestEqual(TEXT("in the hopper's first slot"), Hopper->Get(0), FMcItemStack(TEXT("minecraft:cobblestone")));
	McHopper::MoveOne(ChestAccess, HopperAccess);
	TestEqual(TEXT("it stacks"), Hopper->Get(0).Count, 2);
	TestTrue(TEXT("the slot emptied"), Chest->Get(3).IsEmpty());
	McHopper::MoveOne(ChestAccess, HopperAccess);
	TestEqual(TEXT("then the dirt, in the next slot"), Hopper->Get(1).Item, FName(TEXT("minecraft:dirt")));
	TestFalse(TEXT("nothing left"), McHopper::MoveOne(ChestAccess, HopperAccess));

	// A full target takes nothing; an item that doesn't stack still goes in an empty slot.
	for (int32 Slot = 0; Slot < McHopper::NumSlots; ++Slot)
	{
		Hopper->Set(Slot, FMcItemStack(TEXT("minecraft:stone"), 64));
	}
	Chest->Set(0, FMcItemStack(TEXT("minecraft:stone"), 1));
	TestFalse(TEXT("full"), McHopper::MoveOne(ChestAccess, HopperAccess));
	Hopper->Set(4, FMcItemStack());
	Chest->Set(0, FMcItemStack(TEXT("minecraft:iron_sword")));
	TestTrue(TEXT("a sword goes in the empty slot"), McHopper::MoveOne(ChestAccess, HopperAccess));
	Hopper->Clear();

	// Furnaces: top into the input, side into the fuel (fuels only), bottom out of the output.
	const TSharedPtr<FMcFurnace> Furnace = MakeShared<FMcFurnace>(EMcFurnaceKind::Furnace);
	Hopper->Set(0, FMcItemStack(TEXT("minecraft:raw_iron"), 2));
	TestTrue(TEXT("ore in from above"), McHopper::MoveOne(HopperAccess, McHopper::ForFurnace(Furnace, EFace::Top)));
	TestEqual(TEXT("into the input"), Furnace->GetItems()->Get(FMcFurnace::InputSlot).Item, FName(TEXT("minecraft:raw_iron")));
	TestFalse(TEXT("ore isn't fuel"), McHopper::MoveOne(HopperAccess, McHopper::ForFurnace(Furnace, EFace::Side)));
	Hopper->Set(1, FMcItemStack(TEXT("minecraft:coal"), 1));
	TestTrue(TEXT("coal in from the side"), McHopper::MoveOne(HopperAccess, McHopper::ForFurnace(Furnace, EFace::Side)));
	TestEqual(TEXT("into the fuel"), Furnace->GetItems()->Get(FMcFurnace::FuelSlot).Item, FName(TEXT("minecraft:coal")));
	Furnace->GetItems()->Set(FMcFurnace::OutputSlot, FMcItemStack(TEXT("minecraft:iron_ingot"), 3));
	Hopper->Clear();
	TestTrue(TEXT("out from below"), McHopper::MoveOne(McHopper::ForFurnace(Furnace, EFace::Bottom), HopperAccess));
	TestEqual(TEXT("an ingot"), Hopper->Get(0).Item, FName(TEXT("minecraft:iron_ingot")));
	TestEqual(TEXT("two left"), Furnace->GetItems()->Get(FMcFurnace::OutputSlot).Count, 2);
	TestEqual(TEXT("the input isn't taken from below"), Furnace->GetItems()->Get(FMcFurnace::InputSlot).Count, 1);

	// Brewing stands: top into the ingredient (reagents only), side into the fuel and one bottle a slot.
	const TSharedPtr<FMcBrewingStand> Stand = MakeShared<FMcBrewingStand>();
	Hopper->Clear();
	Hopper->Set(0, FMcItemStack(TEXT("minecraft:dirt"), 1));
	TestFalse(TEXT("dirt isn't a reagent"), McHopper::MoveOne(HopperAccess, McHopper::ForBrewingStand(Stand, EFace::Top)));
	Hopper->Set(0, FMcItemStack(TEXT("minecraft:nether_wart"), 1));
	TestTrue(TEXT("wart in from above"), McHopper::MoveOne(HopperAccess, McHopper::ForBrewingStand(Stand, EFace::Top)));
	TestEqual(TEXT("into the ingredient slot"), Stand->GetItems()->Get(FMcBrewingStand::IngredientSlot).Item, FName(TEXT("minecraft:nether_wart")));
	FMcItemStack Bottles(TEXT("minecraft:glass_bottle"), 5);
	TestTrue(TEXT("bottles in from the side"), McHopper::Insert(McHopper::ForBrewingStand(Stand, EFace::Side), Bottles));
	TestEqual(TEXT("one to each of three slots"), Bottles.Count, 2);
	TestEqual(TEXT("one in the middle slot"), Stand->GetItems()->Get(1).Count, 1);
	FMcItemStack Powder(TEXT("minecraft:blaze_powder"), 3);
	McHopper::Insert(McHopper::ForBrewingStand(Stand, EFace::Side), Powder);
	TestEqual(TEXT("powder into the fuel"), Stand->GetItems()->Get(FMcBrewingStand::FuelSlot).Count, 3);
	Hopper->Clear();
	TestTrue(TEXT("a bottle out from below"), McHopper::MoveOne(McHopper::ForBrewingStand(Stand, EFace::Bottom), HopperAccess));
	TestTrue(TEXT("from the first slot"), Stand->GetItems()->Get(0).IsEmpty());
	TestEqual(TEXT("the ingredient stays"), Stand->GetItems()->Get(FMcBrewingStand::IngredientSlot).Count, 1);
	return true;
}

#endif
