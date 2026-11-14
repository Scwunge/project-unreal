#include "McScreenLayout.h"

namespace
{
	using McGui::FPlayerMenuSlots;

	void AddPlayerSlots(McGui::FScreenLayout& Layout, int32 MainY, int32 HotbarY)
	{
		for (int32 Index = 0; Index < 27; ++Index)
		{
			Layout.Slots.Add(FIntPoint(8 + 18 * (Index % 9), MainY + 18 * (Index / 9)));
		}
		for (int32 Index = 0; Index < 9; ++Index)
		{
			Layout.Slots.Add(FIntPoint(8 + 18 * Index, HotbarY));
		}
	}
}

int32 McGui::FScreenLayout::FindSlot(FIntPoint Local) const
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FIntPoint& Pos = Slots[Index];
		if (Local.X >= Pos.X - 1 && Local.X < Pos.X + 17 && Local.Y >= Pos.Y - 1 && Local.Y < Pos.Y + 17)
		{
			return Index;
		}
	}
	return -1;
}

McGui::FScreenLayout McGui::MakeInventoryLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Inventory;
	Layout.Texture = TEXT("inventory.png");
	Layout.Slots.Add(FIntPoint(154, 28));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Layout.Slots.Add(FIntPoint(98 + 18 * (Index % 2), 18 + 18 * (Index / 2)));
	}
	AddPlayerSlots(Layout, 84, 142);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Layout.Slots.Add(FIntPoint(8, 8 + 18 * Index));
	}
	Layout.Slots.Add(FIntPoint(77, 62));
	check(Layout.Slots.Num() == FPlayerMenuSlots::Total);
	return Layout;
}

McGui::FScreenLayout McGui::MakeCraftingTableLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::CraftingTable;
	Layout.Texture = TEXT("crafting_table.png");
	Layout.Slots.Add(FIntPoint(124, 35));
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Layout.Slots.Add(FIntPoint(30 + 18 * (Index % 3), 17 + 18 * (Index / 3)));
	}
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

TArray<FString> McGui::Enchanting::GetGalacticNames(int32 Seed)
{
	static const TCHAR* const Words[] = {
		TEXT("the"), TEXT("elder"), TEXT("scrolls"), TEXT("klaatu"), TEXT("berata"), TEXT("niktu"), TEXT("xyzzy"), TEXT("bless"), TEXT("curse"),
		TEXT("light"), TEXT("darkness"), TEXT("fire"), TEXT("air"), TEXT("earth"), TEXT("water"), TEXT("hot"), TEXT("dry"), TEXT("cold"), TEXT("wet"),
		TEXT("ignite"), TEXT("snuff"), TEXT("embiggen"), TEXT("twist"), TEXT("shorten"), TEXT("stretch"), TEXT("fiddle"), TEXT("destroy"), TEXT("imbue"),
		TEXT("galvanize"), TEXT("enchant"), TEXT("free"), TEXT("limited"), TEXT("range"), TEXT("of"), TEXT("towards"), TEXT("inside"), TEXT("sphere"),
		TEXT("cube"), TEXT("self"), TEXT("other"), TEXT("ball"), TEXT("mental"), TEXT("physical"), TEXT("grow"), TEXT("shrink"), TEXT("demon"),
		TEXT("elemental"), TEXT("spirit"), TEXT("animal"), TEXT("creature"), TEXT("beast"), TEXT("humanoid"), TEXT("undead"), TEXT("fresh"), TEXT("stale"),
	};
	FRandomStream Random(Seed);
	TArray<FString> Names;
	for (int32 Option = 0; Option < 3; ++Option)
	{
		FString Name;
		const int32 Count = Random.RandRange(3, 4);
		for (int32 Word = 0; Word < Count; ++Word)
		{
			Name += (Word > 0 ? TEXT(" ") : TEXT("")) + FString(Words[Random.RandRange(0, UE_ARRAY_COUNT(Words) - 1)]);
		}
		Names.Add(Name);
	}
	return Names;
}

FString McGui::Anvil::GetFieldFor(const FString& CustomName, const FString& DefaultName)
{
	return CustomName.IsEmpty() ? DefaultName : CustomName;
}

FString McGui::Anvil::GetNameToSend(const FString& Field, const FString& DefaultName)
{
	return Field == DefaultName ? FString() : Field;
}

McGui::FScreenLayout McGui::MakeChestLayout(int32 Rows)
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Chest;
	Layout.Texture = TEXT("generic_54.png");
	Layout.Rows = FMath::Clamp(Rows, 1, 6);
	Layout.Size = FIntPoint(176, 114 + 18 * Layout.Rows);
	for (int32 Index = 0; Index < Layout.Rows * 9; ++Index)
	{
		Layout.Slots.Add(FIntPoint(8 + 18 * (Index % 9), 18 + 18 * (Index / 9)));
	}
	AddPlayerSlots(Layout, 18 * Layout.Rows + 31, 18 * Layout.Rows + 89);
	return Layout;
}

McGui::FScreenLayout McGui::MakeFurnaceLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Furnace;
	Layout.Texture = TEXT("furnace.png");
	Layout.Slots.Add(FIntPoint(56, 17));
	Layout.Slots.Add(FIntPoint(56, 53));
	Layout.Slots.Add(FIntPoint(116, 35));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeEnchantingLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Enchanting;
	Layout.Texture = TEXT("enchanting_table.png");
	Layout.Slots.Add(FIntPoint(15, 47));
	Layout.Slots.Add(FIntPoint(35, 47));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeStonecutterLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Stonecutter;
	Layout.Texture = TEXT("stonecutter.png");
	Layout.Slots.Add(FIntPoint(20, 33));
	Layout.Slots.Add(FIntPoint(143, 33));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeTradingLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Trading;
	Layout.Texture = TEXT("villager.png");
	Layout.Size = FIntPoint(276, 166);
	Layout.Slots.Add(FIntPoint(136, 37));
	Layout.Slots.Add(FIntPoint(162, 37));
	Layout.Slots.Add(FIntPoint(220, 37));
	for (int32 Index = 0; Index < 27; ++Index)
	{
		Layout.Slots.Add(FIntPoint(108 + 18 * (Index % 9), 84 + 18 * (Index / 9)));
	}
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Layout.Slots.Add(FIntPoint(108 + 18 * Index, 142));
	}
	return Layout;
}

int32 McGui::Trading::FindRow(FIntPoint Local, int32 Scroll, int32 Count)
{
	const int32 X = Local.X - ListX;
	const int32 Y = Local.Y - ListY;
	if (X < 0 || Y < 0 || X >= RowWidth || Y >= Rows * RowHeight)
	{
		return -1;
	}
	const int32 Index = Scroll + Y / RowHeight;
	return Index < Count ? Index : -1;
}

int32 McGui::Trading::MaxScroll(int32 Count)
{
	return FMath::Max(0, Count - Rows);
}

float McGui::Trading::GetBarFill(int32 Level, int32 Xp)
{
	if (Level >= 5)
	{
		return 1.0f;
	}
	static const int32 LevelXp[] = { 0, 10, 70, 150, 250 };
	const int32 From = LevelXp[FMath::Clamp(Level, 1, 5) - 1];
	const int32 To = LevelXp[FMath::Clamp(Level, 1, 5)];
	return FMath::Clamp(static_cast<float>(Xp - From) / static_cast<float>(To - From), 0.0f, 1.0f);
}

McGui::FScreenLayout McGui::MakeLoomLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Loom;
	Layout.Texture = TEXT("loom.png");
	Layout.Slots.Add(FIntPoint(13, 26));
	Layout.Slots.Add(FIntPoint(33, 26));
	Layout.Slots.Add(FIntPoint(23, 45));
	Layout.Slots.Add(FIntPoint(143, 57));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeSmithingLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Smithing;
	Layout.Texture = TEXT("smithing.png");
	Layout.Slots.Add(FIntPoint(8, 48));
	Layout.Slots.Add(FIntPoint(26, 48));
	Layout.Slots.Add(FIntPoint(44, 48));
	Layout.Slots.Add(FIntPoint(98, 48));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeAnvilLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Anvil;
	Layout.Texture = TEXT("anvil.png");
	Layout.Slots.Add(FIntPoint(27, 47));
	Layout.Slots.Add(FIntPoint(76, 47));
	Layout.Slots.Add(FIntPoint(134, 47));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeBrewingStandLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::BrewingStand;
	Layout.Texture = TEXT("brewing_stand.png");
	Layout.Slots.Add(FIntPoint(56, 51));
	Layout.Slots.Add(FIntPoint(79, 58));
	Layout.Slots.Add(FIntPoint(102, 51));
	Layout.Slots.Add(FIntPoint(79, 17));
	Layout.Slots.Add(FIntPoint(17, 17));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeHopperLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Hopper;
	Layout.Texture = TEXT("hopper.png");
	Layout.Size = FIntPoint(176, 133);
	for (int32 Slot = 0; Slot < 5; ++Slot)
	{
		Layout.Slots.Add(FIntPoint(44 + 18 * Slot, 20));
	}
	AddPlayerSlots(Layout, 51, 109);
	return Layout;
}

McGui::FScreenLayout McGui::MakeHorseLayout(int32 ContainerSlots, bool bArmor)
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Horse;
	Layout.Texture = TEXT("horse.png");
	// vanilla shot of a donkey: the saddle at the top left, the armour under it, the chest 5 x 3 from x 80.
	Layout.Slots.Add(FIntPoint(8, 18));
	Layout.Slots.Add(bArmor ? FIntPoint(8, 36) : FIntPoint(FScreenLayout::Hidden, FScreenLayout::Hidden));
	for (int32 Slot = 2; Slot < ContainerSlots; ++Slot)
	{
		Layout.Slots.Add(FIntPoint(80 + 18 * ((Slot - 2) % 5), 18 + 18 * ((Slot - 2) / 5)));
	}
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeDispenserLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Dispenser;
	Layout.Texture = TEXT("dispenser.png");
	for (int32 Slot = 0; Slot < 9; ++Slot)
	{
		Layout.Slots.Add(FIntPoint(62 + 18 * (Slot % 3), 17 + 18 * (Slot / 3)));
	}
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

// Slots measured from the texture: the two inputs' 18 pixel boxes start at (14, 14) and (14, 51), the result's 24 pixel
// well at (141, 35).
McGui::FScreenLayout McGui::MakeCartographyLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Cartography;
	Layout.Texture = TEXT("cartography_table.png");
	Layout.Slots.Add(FIntPoint(15, 15));
	Layout.Slots.Add(FIntPoint(15, 52));
	Layout.Slots.Add(FIntPoint(145, 39));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

McGui::FScreenLayout McGui::MakeGrindstoneLayout()
{
	FScreenLayout Layout;
	Layout.Kind = EScreen::Grindstone;
	Layout.Texture = TEXT("grindstone.png");
	Layout.Slots.Add(FIntPoint(49, 19));
	Layout.Slots.Add(FIntPoint(49, 40));
	Layout.Slots.Add(FIntPoint(129, 34));
	AddPlayerSlots(Layout, 84, 142);
	return Layout;
}

int32 McGui::Stonecutter::FindRecipe(FIntPoint Local, int32 Scroll, int32 Count)
{
	const int32 X = Local.X - RecipesX;
	const int32 Y = Local.Y - RecipesY;
	if (X < 0 || Y < 0 || X >= Columns * CellWidth || Y >= Rows * CellHeight)
	{
		return -1;
	}
	const int32 Index = (Scroll + Y / CellHeight) * Columns + X / CellWidth;
	return Index < Count ? Index : -1;
}

int32 McGui::Stonecutter::MaxScroll(int32 Count)
{
	return FMath::Max(0, (Count + Columns - 1) / Columns - Rows);
}

int32 McGui::Loom::FindPattern(FIntPoint Local, int32 Scroll, int32 Count)
{
	const int32 X = Local.X - PatternsX;
	const int32 Y = Local.Y - PatternsY;
	if (X < 0 || Y < 0 || X >= Columns * CellSize || Y >= Rows * CellSize)
	{
		return -1;
	}
	const int32 Index = (Scroll + Y / CellSize) * Columns + X / CellSize;
	return Index < Count ? Index : -1;
}

int32 McGui::Loom::MaxScroll(int32 Count)
{
	return FMath::Max(0, (Count + Columns - 1) / Columns - Rows);
}

uint32 McGui::Loom::GetDyeColour(const FString& Name)
{
	static const TMap<FString, uint32> Colours = {
		{ TEXT("white"), 0xF9FFFE }, { TEXT("orange"), 0xF9801D }, { TEXT("magenta"), 0xC74EBD }, { TEXT("light_blue"), 0x3AB3DA },
		{ TEXT("yellow"), 0xFED83D }, { TEXT("lime"), 0x80C71F }, { TEXT("pink"), 0xF38BAA }, { TEXT("gray"), 0x474F52 },
		{ TEXT("light_gray"), 0x9D9D97 }, { TEXT("cyan"), 0x169C9C }, { TEXT("purple"), 0x8932B8 }, { TEXT("blue"), 0x3C44AA },
		{ TEXT("brown"), 0x835432 }, { TEXT("green"), 0x5E7C16 }, { TEXT("red"), 0xB02E26 }, { TEXT("black"), 0x1D1D21 },
	};
	const uint32* Found = Colours.Find(Name);
	return Found ? *Found : 0xFFFFFF;
}

int32 McGui::Enchanting::FindButton(FIntPoint Local)
{
	const int32 Y = Local.Y - ButtonY;
	if (Local.X < ButtonX || Local.X >= ButtonX + ButtonWidth || Y < 0 || Y >= 3 * ButtonHeight)
	{
		return -1;
	}
	return Y / ButtonHeight;
}

McGui::FScreenLayout McGui::MakeCreativeLayout()
{
	FScreenLayout Layout = MakeInventoryLayout();
	Layout.Kind = EScreen::Creative;
	Layout.Texture = TEXT("creative_inventory/tab_item_search.png");
	Layout.Size = FIntPoint(195, 136);
	for (int32 Slot = 0; Slot < Layout.Slots.Num(); ++Slot)
	{
		const bool bHotbar = Slot >= FPlayerMenuSlots::HotbarFirst && Slot < FPlayerMenuSlots::HotbarFirst + 9;
		Layout.Slots[Slot] = bHotbar ? FIntPoint(Creative::ListX + Creative::Pitch * (Slot - FPlayerMenuSlots::HotbarFirst), 112) : FIntPoint(FScreenLayout::Hidden, FScreenLayout::Hidden);
	}
	return Layout;
}

int32 McGui::Creative::FindCell(FIntPoint Local)
{
	const int32 X = Local.X - (ListX - 1);
	const int32 Y = Local.Y - (ListY - 1);
	if (X < 0 || Y < 0 || X >= Columns * Pitch || Y >= Rows * Pitch)
	{
		return -1;
	}
	return (Y / Pitch) * Columns + X / Pitch;
}

int32 McGui::Creative::MaxScroll(int32 Count)
{
	return FMath::Max(0, (Count + Columns - 1) / Columns - Rows);
}

bool McGui::Creative::IsListed(FName Item)
{
	static const TSet<FName> Unlisted = {
		TEXT("minecraft:air"), TEXT("minecraft:barrier"), TEXT("minecraft:light"), TEXT("minecraft:structure_void"),
		TEXT("minecraft:structure_block"), TEXT("minecraft:jigsaw"), TEXT("minecraft:command_block"),
		TEXT("minecraft:chain_command_block"), TEXT("minecraft:repeating_command_block"),
		TEXT("minecraft:command_block_minecart"), TEXT("minecraft:debug_stick"), TEXT("minecraft:knowledge_book"),
		TEXT("minecraft:petrified_oak_slab"), TEXT("minecraft:test_block"), TEXT("minecraft:test_instance_block"),
	};
	return !Unlisted.Contains(Item);
}

TArray<FName> McGui::Creative::Filter(const TArray<FName>& Items, const FString& Search, TFunctionRef<FString(FName)> NameOf)
{
	TArray<FString> Words;
	Search.ToLower().ParseIntoArrayWS(Words);
	if (Words.Num() == 0)
	{
		return Items;
	}
	TArray<FName> Out;
	for (const FName Item : Items)
	{
		const FString Name = NameOf(Item).ToLower();
		if (!Words.ContainsByPredicate([&](const FString& Word) { return !Name.Contains(Word); }))
		{
			Out.Add(Item);
		}
	}
	return Out;
}

float McGui::GetArrowFill(float Progress, int32 TotalTime)
{
	return TotalTime > 0 ? FMath::Clamp(Progress / TotalTime, 0.0f, 1.0f) : 0.0f;
}

float McGui::GetFlameFill(int32 TimeRemaining, int32 TotalTime)
{
	if (TimeRemaining <= 0)
	{
		return 0.0f;
	}
	return TotalTime > 0 ? FMath::Clamp(static_cast<float>(TimeRemaining) / TotalTime, 0.0f, 1.0f) : 1.0f;
}

McGui::RecipeBook::ETab McGui::RecipeBook::GetTab(const FString& Category)
{
	if (Category == TEXT("equipment"))
	{
		return ETab::Equipment;
	}
	if (Category == TEXT("building"))
	{
		return ETab::Building;
	}
	if (Category == TEXT("redstone"))
	{
		return ETab::Redstone;
	}
	return ETab::Misc;
}

FIntPoint McGui::RecipeBook::GetButtonPos(EScreen Kind)
{
	return Kind == EScreen::CraftingTable ? FIntPoint(5, 49) : FIntPoint(104, 61);
}

int32 McGui::RecipeBook::FindCell(FIntPoint Panel)
{
	const int32 X = Panel.X - CellX;
	const int32 Y = Panel.Y - CellY;
	if (X < 0 || Y < 0 || X >= Columns * CellSize || Y >= Rows * CellSize)
	{
		return -1;
	}
	return (Y / CellSize) * Columns + X / CellSize;
}

int32 McGui::RecipeBook::FindTab(FIntPoint Panel, int32 NumShown)
{
	if (Panel.X < TabX || Panel.X >= TabX + TabWidth || Panel.Y < TabY)
	{
		return -1;
	}
	const int32 Index = (Panel.Y - TabY) / TabHeight;
	return Index < NumShown ? Index : -1;
}

int32 McGui::RecipeBook::GetPageCount(int32 Recipes)
{
	return FMath::Max(1, FMath::DivideAndRoundUp(Recipes, PerPage));
}

bool McGui::RecipeBook::IsOnBook(FIntPoint Panel, int32 NumShown)
{
	return (Panel.X >= 0 && Panel.Y >= 0 && Panel.X < PanelWidth && Panel.Y < PanelHeight) || FindTab(Panel, NumShown) >= 0;
}
