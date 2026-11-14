// Containers and menus as plain classes, with no UI (minecraft.wiki, "Inventory" and "Controls").
//
// A container is a row of item stacks (a chest, the player's inventory, a furnace's three slots). A menu is what a
// player has open: a list of slots, each one a place in some container, plus the stack on the cursor. The player
// sends it clicks, and it moves items about by the game's rules: left and right clicks, shift-click, number keys,
// double click, drops and the drag across slots (quick craft). Subclasses say what each slot accepts and what
// shift-click does.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

class MCITEM_API FMcContainer
{
public:
	explicit FMcContainer(int32 NumSlots = 0) { Items.SetNum(NumSlots); }

	int32 Num() const { return Items.Num(); }
	const FMcItemStack& Get(int32 Slot) const { return Items[Slot]; }
	FMcItemStack& GetMutable(int32 Slot) { return Items[Slot]; }
	void Set(int32 Slot, const FMcItemStack& Stack);
	bool IsEmpty() const;
	void Clear();
	const TArray<FMcItemStack>& GetAll() const { return Items; }
	TArray<FMcItemStack>& GetAll() { return Items; }

	// Adds as much as fits (merging first, then filling empty slots) and returns what's left over.
	FMcItemStack AddItem(const FMcItemStack& Stack);
	// Whether all of the stack would fit.
	bool CanAddItem(const FMcItemStack& Stack) const;
	// Removes up to Amount of the item from anywhere; returns how many it removed.
	int32 RemoveItem(FName Item, int32 Amount);
	int32 CountItem(FName Item) const;

private:
	TArray<FMcItemStack> Items;
};

enum class EMcClickType : uint8
{
	// Left (button 0) and right (button 1) click.
	Pickup,
	// Shift-click.
	QuickMove,
	// A number key (button 0-8: a hotbar slot) or F (button 40: the offhand).
	Swap,
	// Middle click, in creative: a full stack onto the cursor.
	Clone,
	// Q (button 0: one) or Ctrl-Q (button 1: the stack).
	Throw,
	// Drag across slots, in stages: see BeginQuickCraft.
	QuickCraft,
	// Double click: gathers matching items onto the cursor.
	PickupAll,
};

// The place on a menu where the player's own slots start, for menus that have them.
class MCITEM_API FMcMenu
{
public:
	virtual ~FMcMenu() = default;

	struct FSlot
	{
		TSharedPtr<FMcContainer> Container;
		int32 Index = 0;
	};

	int32 NumSlots() const { return Slots.Num(); }
	const FMcItemStack& GetStack(int32 Slot) const { return Slots[Slot].Container->Get(Slots[Slot].Index); }
	void SetStack(int32 Slot, const FMcItemStack& Stack);
	const FMcItemStack& GetCarried() const { return Carried; }
	void SetCarried(const FMcItemStack& Stack) { Carried = Stack; }

	// Handles a click. Slot is -999 for outside the window (which drops the cursor stack on a Pickup). Stacks the
	// click threw out are appended to Dropped; the caller makes them into item entities.
	void Click(int32 Slot, int32 Button, EMcClickType Type, TArray<FMcItemStack>& Dropped, bool bCreative = false);

	// The drag, as the mouse does it: begin with the button (0 splits the stack evenly across the slots dragged
	// over, 1 puts one in each, 2 in creative fills each to a full stack), add each slot dragged over, then end.
	void BeginQuickCraft(int32 Button);
	void AddQuickCraftSlot(int32 Slot);
	void EndQuickCraft(bool bCreative = false);
	bool IsQuickCrafting() const { return bDragging; }
	// While dragging over two slots or more: what each slot dragged over would hold if the button were let go now, and
	// how many the cursor would keep (the screen draws these). False when nothing would be spread.
	bool PreviewQuickCraft(TMap<int32, FMcItemStack>& OutSlots, int32& OutCarried, bool bCreative = false) const;

	// Gives the cursor stack back to the player, e.g. when the menu closes: into the player's slots, else dropped.
	void Close(TArray<FMcItemStack>& Dropped);

	// A button of the menu's own besides its slots (a stonecutter's recipe): the player pressed Id. False if it
	// does nothing now.
	virtual bool ClickButton(int32 Id) { return false; }
	// What the menu keeps besides its slots that the player's screen needs (the recipe chosen), read or written by
	// whether the archive is loading. Called after the slots when a window is synced.
	virtual void SerializeState(FArchive& Ar) {}
	// Experience points the player earned from the menu since last asked (a grindstone's), which the caller
	// gives them.
	virtual int32 TakeExperience() { return 0; }
	// The bundle sound the last click made (item.bundle.insert, insert_fail or remove_one), or none; read once.
	// The stack of a bundle picked with the wheel (an index of its contents; -1 for none), which a right click takes out.
	// Set by the screen before each click.
	void SetBundlePick(int32 Index) { BundlePick = Index; }
	FName TakeBundleSound() { const FName Out = BundleSound; BundleSound = NAME_None; return Out; }
	// Levels the player spent in the menu since last asked (an anvil's), which the caller takes from them.
	virtual int32 TakeLevelsSpent() { return 0; }
	// The player's levels and game mode, for menus whose slots depend on them (an anvil's result). Set before
	// each click.
	virtual void SetPlayerLevels(int32 Levels, bool bCreative) {}
	// A text field of the menu's own changed (an anvil's name). False if the text isn't allowed.
	virtual bool SetText(const FString& Text) { return false; }

protected:
	FName BundleSound;
	int32 BundlePick = -1;
	// ---- What subclasses decide ----
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const { return true; }
	virtual bool MayPickup(int32 Slot) const { return true; }
	// The most the slot holds (a furnace's output takes the stack size, an armour slot just one).
	virtual int32 GetSlotLimit(int32 Slot) const { return 64; }
	// Shift-click on the slot: moves its stack somewhere sensible and returns what was moved (empty if nothing
	// could be), so a repeat can tell it's done. The base moves between the first part of the menu and the player's.
	virtual FMcItemStack QuickMoveStack(int32 Slot);
	// The menu is closing; give back what it holds apart from the cursor stack (a crafting grid's items).
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) {}
	// A slot's stack changed from a click; recompute what depends on it (the crafting result).
	virtual void SlotChanged(int32 Slot) {}
	// The player took Taken out of the slot (the crafting result consumes its ingredients here).
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) {}
	// Whether double click gathers from this slot (not from the result slot).
	virtual bool CanTakeForPickupAll(int32 Slot) const { return true; }
	// Whether a drag may put items into the slot.
	virtual bool CanDragTo(int32 Slot) const { return true; }

	// Adds a slot at the end for container Index.
	int32 AddSlot(const TSharedPtr<FMcContainer>& Container, int32 Index);
	// The usual player slots: 27 of inventory then 9 of hotbar.
	void AddPlayerSlots(const TSharedPtr<FMcContainer>& PlayerInventory);

	int32 EffectiveLimit(int32 Slot, const FMcItemStack& Stack) const { return FMath::Min(GetSlotLimit(Slot), Stack.GetMaxStackSize()); }
	// Moves as much of Stack as fits into slots [Start, End), merging with equal stacks first then taking empty
	// ones, from the back if bReverse; changes Stack to what's left. True if anything moved.
	bool MoveItemStackTo(FMcItemStack& Stack, int32 Start, int32 End, bool bReverse);

	TArray<FSlot> Slots;
	FMcItemStack Carried;
	// Where the player's slots start (the end of the menu's own).
	int32 PlayerStart = 0;

private:
	void Pickup(int32 Slot, int32 Button, TArray<FMcItemStack>& Dropped);
	void SwapWithHotbar(int32 Slot, int32 Button, TArray<FMcItemStack>& Dropped);
	void PickupAll(int32 Slot, int32 Button);

	bool bDragging = false;
	int32 DragButton = 0;
	TArray<int32> DragSlots;
};

// A chest, barrel, dispenser or any plain container laid out in rows, with the player's inventory below it.
class MCITEM_API FMcContainerMenu : public FMcMenu
{
public:
	FMcContainerMenu(const TSharedPtr<FMcContainer>& InContainer, const TSharedPtr<FMcContainer>& PlayerInventory);
	// A double chest: the slots of First, then of Second.
	FMcContainerMenu(const TSharedPtr<FMcContainer>& First, const TSharedPtr<FMcContainer>& Second, const TSharedPtr<FMcContainer>& PlayerInventory);

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;

public:
	// A shulker box's menu: another box can't go inside.
	bool bNoShulkerBoxes = false;

private:
	TSharedPtr<FMcContainer> Container;
	TSharedPtr<FMcContainer> SecondContainer;
};
