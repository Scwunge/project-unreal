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
	enum class EScreen : uint8 { Inventory, CraftingTable, Chest, Furnace, Creative, Enchanting, Stonecutter, Smithing, Grindstone, Anvil, BrewingStand, Hopper, Dispenser };

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
		// Where a menu slot the screen doesn't show sits (the creative screen shows only the hotbar).
		static constexpr int32 Hidden = -10000;
		bool IsShown(int32 Slot) const { return Slots.IsValidIndex(Slot) && Slots[Slot].X != Hidden; }
		// The top left of the screen when it is centred in a Width x Height window.
		FIntPoint Origin(int32 Width, int32 Height) const { return FIntPoint((Width - Size.X) / 2, (Height - Size.Y) / 2); }
	};

	MCUI_API FScreenLayout MakeInventoryLayout();
	MCUI_API FScreenLayout MakeCraftingTableLayout();
	// Rows is 1 to 6 (a chest has 3 or 6).
	MCUI_API FScreenLayout MakeChestLayout(int32 Rows);
	MCUI_API FScreenLayout MakeFurnaceLayout();
	MCUI_API FScreenLayout MakeEnchantingLayout();
	MCUI_API FScreenLayout MakeStonecutterLayout();
	MCUI_API FScreenLayout MakeSmithingLayout();
	MCUI_API FScreenLayout MakeGrindstoneLayout();
	MCUI_API FScreenLayout MakeAnvilLayout();
	MCUI_API FScreenLayout MakeBrewingStandLayout();
	MCUI_API FScreenLayout MakeHopperLayout();
	MCUI_API FScreenLayout MakeDispenserLayout();

	namespace Stonecutter
	{
		// The recipes, 4 wide and 3 high (16 x 18 each), and the scroll bar's track beside them.
		constexpr int32 Columns = 4;
		constexpr int32 Rows = 3;
		constexpr int32 RecipesX = 52;
		constexpr int32 RecipesY = 14;
		constexpr int32 CellWidth = 16;
		constexpr int32 CellHeight = 18;
		constexpr int32 ScrollX = 119;
		constexpr int32 ScrollY = 15;
		constexpr int32 ScrollHeight = 54;

		// The recipe (an index into the menu's list) under a point from the screen's top left, with the list
		// scrolled down Scroll rows; -1 if none (Count recipes in all).
		MCUI_API int32 FindRecipe(FIntPoint Local, int32 Scroll, int32 Count);
		MCUI_API int32 MaxScroll(int32 Count);
	}

	namespace Enchanting
	{
		// The three options' buttons, one under another.
		constexpr int32 ButtonX = 60;
		constexpr int32 ButtonY = 14;
		constexpr int32 ButtonWidth = 108;
		constexpr int32 ButtonHeight = 19;

		// The option (0 to 2) under a point from the screen's top left, -1 if none.
		MCUI_API int32 FindButton(FIntPoint Local);
	}
	// The creative inventory's search tab (minecraft.wiki, "Creative inventory"): over the player's own menu, with only
	// the hotbar shown, under a list of every item 9 wide and 5 high, a search box and a scroll bar.
	MCUI_API FScreenLayout MakeCreativeLayout();

	namespace Creative
	{
		constexpr int32 Columns = 9;
		constexpr int32 Rows = 5;
		// Top left of the list's first item, and its cells' pitch.
		constexpr int32 ListX = 9;
		constexpr int32 ListY = 18;
		constexpr int32 Pitch = 18;
		// The scroll bar's track (the thumb is 12 x 15) and the search box's text.
		constexpr int32 ScrollX = 175;
		constexpr int32 ScrollY = 18;
		constexpr int32 ScrollHeight = 112;
		constexpr int32 SearchX = 82;
		constexpr int32 SearchY = 6;
		constexpr int32 SearchWidth = 89;
		constexpr int32 MaxSearch = 50;

		// The list cell under a point from the screen's top left (0 to 44, left to right then down), -1 if none.
		MCUI_API int32 FindCell(FIntPoint Local);
		// How far down the list can scroll, in rows, for Count items.
		MCUI_API int32 MaxScroll(int32 Count);
		// Items creative offers, in the registry's order: everything but air and the operator-only blocks and items.
		MCUI_API bool IsListed(FName Item);
		// Of Items, those whose name (NameOf) contains every word of Search, ignoring case; all of them for no words.
		MCUI_API TArray<FName> Filter(const TArray<FName>& Items, const FString& Search, TFunctionRef<FString(FName)> NameOf);
	}

	// How far the arrow and the flame are filled, 0 to 1, from the furnace (progress in the cook, burn left).
	MCUI_API float GetArrowFill(float Progress, int32 TotalTime);
	MCUI_API float GetFlameFill(int32 TimeRemaining, int32 TotalTime);
}
