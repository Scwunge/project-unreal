// The container screens: where each slot of a menu sits on its background picture, in GUI pixels from the top
// left of the picture, and what else is drawn on it (minecraft.wiki, "Inventory", "Crafting Table", "Chest",
// "Furnace"). The slot lists follow the order of the McItem menu each screen shows, so Slots[i] is menu slot i.
// Pure data and maths: nothing here draws.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McCraftingMenu.h"
#include "McFurnace.h"

namespace McGui
{
	enum class EScreen : uint8 { Inventory, CraftingTable, Chest, Furnace };

	// The slot a player-inventory menu has beyond what the crafting menu has: its slot numbers.
	struct FPlayerMenuSlots
	{
		static constexpr int32 Result = 0;
		static constexpr int32 GridFirst = 1;           // 2 x 2, 1 to 4
		static constexpr int32 MainFirst = 5;           // 27 of them
		static constexpr int32 HotbarFirst = 32;        // 9
		static constexpr int32 ArmorFirst = 41;         // head, chest, legs, feet
		static constexpr int32 Offhand = 45;
		static constexpr int32 Total = 46;
	};

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

// The menu of the player's own inventory screen: the crafting menu's result and 2 x 2 grid and the 36 slots, then
// the four armour slots and the offhand. Armour slots take only armour of their kind, one piece.
class MCUI_API FMcPlayerMenu : public FMcCraftingMenu
{
public:
	FMcPlayerMenu(const TSharedPtr<FMcContainer>& InInventory, const TSharedPtr<FMcContainer>& InArmor, const TSharedPtr<FMcContainer>& InOffhand);

	// The armour slot (0 head to 3 feet) an item goes in, -1 if it isn't worn.
	static int32 GetArmorSlotFor(const FMcItemStack& Stack);

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;
	virtual int32 GetSlotLimit(int32 Slot) const override;
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual bool CanDragTo(int32 Slot) const override;

private:
	TSharedPtr<FMcContainer> Inventory;
};
