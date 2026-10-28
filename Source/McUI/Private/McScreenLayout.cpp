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
