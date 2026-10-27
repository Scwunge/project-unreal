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
