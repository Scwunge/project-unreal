// The mouse and keys of an open container screen, turned into clicks on its McItem menu (minecraft.wiki,
// "Inventory#Controls"). It knows nothing of drawing: the screen tells it which slot the mouse is over (-1 over
// none, -999 outside the window) and what happened. Stacks that clicks throw out collect in TakeDropped, for
// the caller to make into item entities.
//
// Left or right click picks up, places, swaps or (right) splits; shift-click quick-moves; a number key swaps
// the slot with that hotbar slot; Q throws one (Ctrl-Q the stack); a second left click soon after the first
// gathers matching items; and holding a button over one slot with a stack on the cursor and moving across more
// slots spreads the stack (left: evenly, right: one in each) when the button is let go.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"

class MCUI_API FMcMenuInput
{
public:
	static constexpr int32 Outside = -999;
	static constexpr double DoubleClickSeconds = 0.25;

	explicit FMcMenuInput(const TSharedPtr<FMcMenu>& InMenu, bool bInCreative = false) : Menu(InMenu), bCreative(bInCreative) {}

	FMcMenu& GetMenu() { return *Menu; }
	const FMcMenu& GetMenu() const { return *Menu; }

	// Button 0 is left, 1 right, 2 middle. Time is in seconds, for telling a double click.
	void MouseDown(int32 Slot, int32 Button, bool bShift, double Time);
	void MouseMove(int32 Slot);
	void MouseUp(int32 Slot, int32 Button);
	// A key 1 to 9 (Number 0 to 8) with the mouse over Slot.
	void NumberKey(int32 Slot, int32 Number);
	// Q over Slot.
	void DropKey(int32 Slot, bool bCtrl);
	// The screen closes: the cursor stack and the grid's items go back to the player, or drop.
	void Close();

	// Told of every click and every finished drag as it is applied to the menu, so the owner can send it to the server.
	TFunction<void(int32 Slot, int32 Button, EMcClickType Type)> OnClick;
	TFunction<void(int32 Button, const TArray<int32>& Slots)> OnDrag;

	bool IsDragging() const { return bDragActive; }
	TArray<FMcItemStack> TakeDropped() { return MoveTemp(Dropped); }
	const TArray<FMcItemStack>& GetDropped() const { return Dropped; }

private:
	void FinishDrag();
	void DoClick(int32 Slot, int32 Button, EMcClickType Type);

	TSharedPtr<FMcMenu> Menu;
	bool bCreative = false;
	TArray<FMcItemStack> Dropped;
	// The slot and button held down with a stack on the cursor, waiting to see whether it becomes a drag.
	int32 PressSlot = INDEX_NONE;
	int32 PressButton = 0;
	bool bDragActive = false;
	TArray<int32> DragSlots;
	int32 LastClickSlot = INDEX_NONE;
	double LastClickTime = -1000.0;
};
