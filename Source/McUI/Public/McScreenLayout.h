// The container screens: where each slot of a menu sits on its background picture, in GUI pixels from the top
// left of the picture, and what else is drawn on it (minecraft.wiki, "Inventory", "Crafting Table", "Chest",
// "Furnace"). The slot lists follow the order of the McItem menu each screen shows, so Slots[i] is menu slot i.
// Pure data and maths: nothing here draws.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McCraftingMenu.h"
#include "McFurnace.h"
#include "McPlayerMenu.h"

namespace McGui
{
	enum class EScreen : uint8 { Inventory, CraftingTable, Chest, Furnace };

	using FPlayerMenuSlots = ::FMcPlayerMenuSlots;

	struct FScreenLayout
	{
		EScreen Kind = EScreen::Inventory;
		// The background picture (a path under textures/gui/container) and the size of the part of it that is the screen.
		FString Texture;
		FIntPoint Size = FIntPoint(176, 166);
		// Top left of the 16 x 16 item area of each menu slot.
		TArray<FIntPoint> Slots;
		// Chests only: the rows of the container, which the picture is cut by.
		int32 Rows = 0;
		// Furnace only: where the flame and the arrow go.
		FIntPoint Flame = FIntPoint(56, 36);
		FIntPoint Arrow = FIntPoint(79, 34);

		// The slot under a point given from the screen's top left, -1 if none. Slots catch an 18 x 18 square (the
		// 16 x 16 item and the border around it).
		int32 FindSlot(FIntPoint Local) const;
		// The top left of the screen when it is centred in a Width x Height window.
		FIntPoint Origin(int32 Width, int32 Height) const { return FIntPoint((Width - Size.X) / 2, (Height - Size.Y) / 2); }
	};

	MCUI_API FScreenLayout MakeInventoryLayout();
	MCUI_API FScreenLayout MakeCraftingTableLayout();
	// Rows is 1 to 6 (a chest has 3 or 6).
	MCUI_API FScreenLayout MakeChestLayout(int32 Rows);
	MCUI_API FScreenLayout MakeFurnaceLayout();

	// How far the arrow and the flame are filled, 0 to 1, from the furnace (progress in the cook, burn left).
	MCUI_API float GetArrowFill(float Progress, int32 TotalTime);
	MCUI_API float GetFlameFill(int32 TimeRemaining, int32 TotalTime);
}
