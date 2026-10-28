#include "McBrewingStand.h"
#include "McItemRegistry.h"
#include "McRecipe.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBrewingStandTest, "McClient.McItem.BrewingStand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBrewingStandTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()) || !TestTrue(TEXT("recipes load"), FMcRecipeBook::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto Potion = [](const TCHAR* Name)
	{
		FMcItemStack Stack(TEXT("minecraft:potion"));
		const TSharedRef<FJsonObject> Contents = MakeShared<FJsonObject>();
		Contents->SetStringField(TEXT("potion"), Name);
		Stack.Set(TEXT("minecraft:potion_contents"), MakeShared<FJsonValueObject>(Contents));
		return Stack;
	};
	const auto PotionOf = [](const FMcItemStack& Stack)
	{
		const TSharedPtr<FJsonValue> Value = Stack.Get(TEXT("minecraft:potion_contents"));
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Name;
		if (Value.IsValid() && Value->TryGetObject(Object))
		{
			(*Object)->TryGetStringField(TEXT("potion"), Name);
		}
		return Name;
	};

	const TSharedPtr<FMcBrewingStand> Stand = MakeShared<FMcBrewingStand>();
	const TSharedPtr<FMcContainer> Inventory = MakeShared<FMcContainer>(36);
	FMcBrewingMenu Menu(Stand, Inventory);
	TArray<FMcItemStack> Dropped;

	// What goes where: bottles one each in the bottom three, blaze powder as fuel, reagents on top.
	Inventory->Set(0, FMcItemStack(TEXT("minecraft:blaze_powder"), 2));
	Menu.Click(5 + 27, 0, EMcClickType::QuickMove, Dropped);
	TestEqual(TEXT("powder goes in as fuel"), Stand->GetItems()->Get(FMcBrewingStand::FuelSlot).Count, 2);
	Menu.SetCarried(FMcItemStack(TEXT("minecraft:dirt")));
	Menu.Click(FMcBrewingStand::IngredientSlot, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("dirt isn't an ingredient"), Stand->GetItems()->Get(FMcBrewingStand::IngredientSlot).IsEmpty());
	Menu.Click(0, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("nor a bottle"), Stand->GetItems()->Get(0).IsEmpty());
	Menu.SetCarried(FMcItemStack());

	// No brew without something it makes: water and dirt.
	Menu.SetStack(0, Potion(TEXT("minecraft:water")));
	Menu.SetStack(2, Potion(TEXT("minecraft:water")));
	TestFalse(TEXT("nothing to brew"), Stand->CanBrew());
	Stand->Tick();
	TestFalse(TEXT("so it doesn't start"), Stand->IsBrewing());

	// Nether wart on water: a powder lights it, the brew takes 400 ticks and makes both awkward.
	Menu.SetStack(FMcBrewingStand::IngredientSlot, FMcItemStack(TEXT("minecraft:nether_wart"), 3));
	TestTrue(TEXT("can brew"), Stand->CanBrew());
	Stand->Tick();
	TestTrue(TEXT("started"), Stand->IsBrewing());
	TestEqual(TEXT("a powder used"), Stand->GetItems()->Get(FMcBrewingStand::FuelSlot).Count, 1);
	TestEqual(TEXT("19 brews left of it"), Stand->GetFuel(), FMcBrewingStand::FuelPerPowder - 1);
	for (int32 Tick = 0; Tick < FMcBrewingStand::BrewTime - 1; ++Tick)
	{
		Stand->Tick();
	}
	TestEqual(TEXT("one tick to go"), Stand->GetBrewTimeLeft(), 1);
	Stand->Tick();
	TestFalse(TEXT("done"), Stand->IsBrewing());
	TestEqual(TEXT("first bottle awkward"), PotionOf(Stand->GetItems()->Get(0)), FString(TEXT("minecraft:awkward")));
	TestEqual(TEXT("third bottle awkward"), PotionOf(Stand->GetItems()->Get(2)), FString(TEXT("minecraft:awkward")));
	TestTrue(TEXT("the empty one stays empty"), Stand->GetItems()->Get(1).IsEmpty());
	TestEqual(TEXT("one wart used"), Stand->GetItems()->Get(FMcBrewingStand::IngredientSlot).Count, 2);

	// Nether wart does nothing more to awkward potions; taking the ingredient away stops a brew.
	Stand->Tick();
	TestFalse(TEXT("wart on awkward does nothing"), Stand->IsBrewing());
	Menu.SetStack(0, Potion(TEXT("minecraft:water")));
	Stand->Tick();
	TestTrue(TEXT("the new water starts one"), Stand->IsBrewing());
	Menu.SetStack(FMcBrewingStand::IngredientSlot, FMcItemStack());
	Stand->Tick();
	TestFalse(TEXT("stopped"), Stand->IsBrewing());
	TestEqual(TEXT("the water is still water"), PotionOf(Stand->GetItems()->Get(0)), FString(TEXT("minecraft:water")));

	// It saves and loads as it was.
	Menu.SetStack(FMcBrewingStand::IngredientSlot, FMcItemStack(TEXT("minecraft:nether_wart")));
	Stand->Tick();
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("saves"), Stand->Serialize(Writer));
	FMcBrewingStand Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("loads"), Loaded.Serialize(Reader));
	TestEqual(TEXT("brew time kept"), Loaded.GetBrewTimeLeft(), Stand->GetBrewTimeLeft());
	TestEqual(TEXT("fuel kept"), Loaded.GetFuel(), Stand->GetFuel());
	Loaded.Tick();
	TestTrue(TEXT("and carries on brewing"), Loaded.IsBrewing());

	// Shift-clicking a bottle from the stand puts it back in the inventory.
	Menu.Click(0, 0, EMcClickType::QuickMove, Dropped);
	TestTrue(TEXT("bottle out"), Stand->GetItems()->Get(0).IsEmpty());
	TestEqual(TEXT("into the inventory"), Inventory->CountItem(TEXT("minecraft:potion")), 1);
	return true;
}

#endif
