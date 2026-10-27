// The menu of the player's own inventory screen, and the checks the server makes on clicks that clients send for the
// menu they have open (minecraft.wiki, "Inventory#Controls").
//
// The menu is the crafting menu's result and 2 x 2 grid and the 36 slots, then the four armour slots and the offhand.
// Armour slots take only armour of their kind, one piece. Both sides build it: the client predicts its clicks on one,
// the server applies the same clicks to its own.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McCraftingMenu.h"

// The slot numbers of a player-inventory menu.
struct FMcPlayerMenuSlots
{
	static constexpr int32 Result = 0;
	static constexpr int32 GridFirst = 1;           // 2 x 2, 1 to 4
	static constexpr int32 MainFirst = 5;           // 27 of them
	static constexpr int32 HotbarFirst = 32;        // 9
	static constexpr int32 ArmorFirst = 41;         // head, chest, legs, feet
	static constexpr int32 Offhand = 45;
	static constexpr int32 Total = 46;
};

class MCPLAYER_API FMcPlayerMenu : public FMcCraftingMenu
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

// What the server accepts of the clicks a client sends. Pure checks, so they can be tested without a network.
namespace McMenuRpc
{
	enum class EResult : uint8
	{
		Ok,
		// The click is for a window the server doesn't have open (closed, replaced, or never opened).
		WrongWindow,
		BadSlot,
		BadButton,
		BadType,
		// Something only creative mode may do.
		NotAllowed,
	};

	// The slot number that means "outside the window".
	constexpr int32 OutsideSlot = -999;
	// A drag can't cross more slots than any menu has.
	constexpr int32 MaxDragSlots = 64;

	// A click of EMcClickType Type (as its number, which is what arrives over the wire) on Slot with Button, for window
	// ClaimedWindow when the server has ServerWindow open (-1 for none).
	MCPLAYER_API EResult ValidateClick(const FMcMenu& Menu, int32 ServerWindow, int32 ClaimedWindow, int32 Slot, int32 Button, int32 Type, bool bCreative);
	// A drag across Slots with Button (0 spreads evenly, 1 one each, 2 fills them in creative).
	MCPLAYER_API EResult ValidateDrag(const FMcMenu& Menu, int32 ServerWindow, int32 ClaimedWindow, int32 Button, const TArray<int32>& Slots, bool bCreative);
}
