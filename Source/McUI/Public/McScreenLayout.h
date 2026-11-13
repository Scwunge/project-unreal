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
	enum class EScreen : uint8 { Inventory, CraftingTable, Chest, Furnace, Creative, Enchanting, Stonecutter, Smithing, Grindstone, Cartography, Anvil, BrewingStand, Hopper, Dispenser, Loom, Trading, Horse };

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
		// How far the screen is moved right (the recipe book beside it takes the room: 77 when it is open).
		int32 ShiftX = 0;
		// The top left of the screen when it is centred in a Width x Height window.
		FIntPoint Origin(int32 Width, int32 Height) const { return FIntPoint((Width - Size.X) / 2 + ShiftX, (Height - Size.Y) / 2); }
	};

	MCUI_API FScreenLayout MakeInventoryLayout();
	MCUI_API FScreenLayout MakeCraftingTableLayout();
	// Rows is 1 to 6 (a chest has 3 or 6).
	MCUI_API FScreenLayout MakeChestLayout(int32 Rows);
	MCUI_API FScreenLayout MakeFurnaceLayout();
	MCUI_API FScreenLayout MakeEnchantingLayout();
	MCUI_API FScreenLayout MakeStonecutterLayout();
	MCUI_API FScreenLayout MakeLoomLayout();
	MCUI_API FScreenLayout MakeSmithingLayout();
	MCUI_API FScreenLayout MakeGrindstoneLayout();
	MCUI_API FScreenLayout MakeCartographyLayout();
	MCUI_API FScreenLayout MakeAnvilLayout();
	MCUI_API FScreenLayout MakeBrewingStandLayout();
	MCUI_API FScreenLayout MakeHopperLayout();
	MCUI_API FScreenLayout MakeDispenserLayout();
	// A tamed horse, donkey or mule: Slots is the container's (the saddle, the armour, the chest) then the player's. The armour slot is shown for
	// a horse only, the chest's 5 x 3 slots when there are 17 container slots.
	MCUI_API FScreenLayout MakeHorseLayout(int32 ContainerSlots, bool bArmor);
	// A trader's window: 276 wide, the offers in a list on the left and the player's slots on the right.
	MCUI_API FScreenLayout MakeTradingLayout();

	namespace Trading
	{
		// The offers, one to a row (88 x 20), seven showing, and the scroll bar's track beside them.
		constexpr int32 Rows = 7;
		constexpr int32 ListX = 5;
		constexpr int32 ListY = 18;
		constexpr int32 RowWidth = 88;
		constexpr int32 RowHeight = 20;
		constexpr int32 ScrollX = 94;
		constexpr int32 ScrollY = 18;
		constexpr int32 ScrollHeight = 140;
		// The experience bar over the right part.
		constexpr int32 BarX = 136;
		constexpr int32 BarY = 16;
		constexpr int32 BarWidth = 102;

		// The offer (an index into the menu list) under a point from the screen top left, with the list scrolled down
		// Scroll rows; -1 if none (Count offers in all).
		MCUI_API int32 FindRow(FIntPoint Local, int32 Scroll, int32 Count);
		MCUI_API int32 MaxScroll(int32 Count);
		// How full the experience bar is, 0 to 1, for a villager of Level with Xp (a master has it full).
		MCUI_API float GetBarFill(int32 Level, int32 Xp);
	}

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

	namespace Loom
	{
		// The patterns, 4 wide and 4 high (14 square each), and the scroll bar's track beside them.
		constexpr int32 Columns = 4;
		constexpr int32 Rows = 4;
		constexpr int32 PatternsX = 60;
		constexpr int32 PatternsY = 13;
		constexpr int32 CellSize = 14;
		constexpr int32 ScrollX = 119;
		constexpr int32 ScrollY = 13;
		constexpr int32 ScrollHeight = 56;

		// The pattern (an index into the menu's list) under a point from the screen's top left, with the list
		// scrolled down Scroll rows; -1 if none (Count patterns in all).
		MCUI_API int32 FindPattern(FIntPoint Local, int32 Scroll, int32 Count);
		MCUI_API int32 MaxScroll(int32 Count);
		// The colour of a dye or banner (a name such as "light_blue"), as RGB.
		MCUI_API uint32 GetDyeColour(const FString& Name);
	}

	namespace Anvil
	{
		// What the name field starts with for an item on the left: its custom name, else its usual name.
		MCUI_API FString GetFieldFor(const FString& CustomName, const FString& DefaultName);
		// What is sent to the menu for what the field holds: the usual name (or nothing) means no custom name.
		MCUI_API FString GetNameToSend(const FString& Field, const FString& DefaultName);
	}

	namespace Enchanting
	{
		// The three made-up lines the table writes in the standard galactic alphabet, one for each option: three or four
		// of its words each, from the player's enchantment seed (so they change only when the seed does).
		MCUI_API TArray<FString> GetGalacticNames(int32 Seed);
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

	// The recipe book beside the inventory and the crafting table, measured from the reference's shots (ref_book_inv_open0,
	// ref_rt_book_b at GUI scale 3). Positions are from the panel's top left (the panel sits 147 left of the screen, level
	// with its top) unless it says from the screen.
	namespace RecipeBook
	{
		constexpr int32 Shift = 77;
		constexpr int32 PanelWidth = 147;
		constexpr int32 PanelHeight = 166;
		// The tabs hang off the left edge, 35 x 27, one every 27; the chosen one sticks out 2 more.
		constexpr int32 TabX = -30;
		constexpr int32 TabY = 3;
		constexpr int32 TabWidth = 35;
		constexpr int32 TabHeight = 27;
		// The recipes, 5 wide and 4 high, 25 square each.
		constexpr int32 Columns = 5;
		constexpr int32 Rows = 4;
		constexpr int32 PerPage = Columns * Rows;
		constexpr int32 CellX = 11;
		constexpr int32 CellY = 31;
		constexpr int32 CellSize = 25;
		// The search box, the "craftable only" switch and the page arrows.
		constexpr int32 SearchX = 25;
		constexpr int32 SearchY = 13;
		constexpr int32 SearchWidth = 81;
		constexpr int32 SearchHeight = 14;
		constexpr int32 FilterX = 110;
		constexpr int32 FilterY = 12;
		constexpr int32 FilterWidth = 26;
		constexpr int32 FilterHeight = 16;
		constexpr int32 BackX = 38;
		constexpr int32 ForwardX = 93;
		constexpr int32 PageY = 137;
		constexpr int32 PageButtonWidth = 12;
		constexpr int32 PageButtonHeight = 17;
		constexpr int32 MaxSearch = 50;

		// The tabs of the crafting book, in the order they are drawn; a tab shows only when it has recipes (the first always).
		enum class ETab : uint8 { Search, Equipment, Building, Misc, Redstone };
		constexpr int32 NumTabs = 5;
		// The tab a recipe's "category" (the game's data: building, redstone, equipment, none for the rest) belongs to.
		MCUI_API ETab GetTab(const FString& Category);
		// From the screen: where the toggle button is (20 x 18).
		MCUI_API FIntPoint GetButtonPos(EScreen Kind);
		// The recipe slot (0 to 19) under a point from the panel's top left, -1 if none.
		MCUI_API int32 FindCell(FIntPoint Panel);
		// The tab (an index into the shown tabs) under a point from the panel's top left, -1 if none.
		MCUI_API int32 FindTab(FIntPoint Panel, int32 NumShown);
		MCUI_API int32 GetPageCount(int32 Recipes);
		// Whether a point from the panel's top left is on the panel or on one of its tabs (clicks there are the book's).
		MCUI_API bool IsOnBook(FIntPoint Panel, int32 NumShown);
	}

	// How far the arrow and the flame are filled, 0 to 1, from the furnace (progress in the cook, burn left).
	MCUI_API float GetArrowFill(float Progress, int32 TotalTime);
	MCUI_API float GetFlameFill(int32 TimeRemaining, int32 TotalTime);
}
