// The anvil (minecraft.wiki, "Anvil mechanics"): slot 0 the item, slot 1 what goes onto it, slot 2 the result,
// then the player's slots.
//
// - Onto a damaged item can go its repair material (each one mends a quarter of its durability, for a level each)
//   or another of the same item (their durability added with 12% more, for 2 levels).
// - The second item's or an enchanted book's enchantments are added: equal levels make one higher, else the
//   higher stays. Each costs its anvil cost (halved from a book) times the level it ends at. An enchantment that
//   clashes with one already there is left off for a level.
// - A new name costs a level.
// - The item's prior-work penalty (repair_cost) is added. The result's becomes twice the higher of the two plus 1.
// - A cost of 40 or more is too expensive, except in creative; a rename alone is capped at 39.
//
// Taking the result costs the player the levels (GetCost); the menu needs the player's levels to allow it.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"

class MCITEM_API FMcAnvilMenu : public FMcMenu
{
public:
	static constexpr int32 LeftSlot = 0;
	static constexpr int32 RightSlot = 1;
	static constexpr int32 ResultSlot = 2;
	static constexpr int32 MaxNameLength = 50;
	static constexpr int32 TooExpensive = 40;

	explicit FMcAnvilMenu(const TSharedPtr<FMcContainer>& PlayerInventory);

	// The name typed for the result (empty to keep or take off the custom name).
	const FString& GetItemName() const { return ItemName; }
	// Levels the result costs; 0 when there is none.
	int32 GetCost() const { return Cost; }
	// Whether the inputs would make something, but it costs too much.
	bool IsTooExpensive() const { return bTooExpensive; }

	// The name field changed. False if it was too long.
	virtual bool SetText(const FString& Text) override;
	virtual void SetPlayerLevels(int32 Levels, bool bCreative) override;
	virtual int32 TakeLevelsSpent() override;
	virtual void SerializeState(FArchive& Ar) override;

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override { return Slot != ResultSlot; }
	virtual bool MayPickup(int32 Slot) const override;
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != ResultSlot; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != ResultSlot; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void SlotChanged(int32 Slot) override;
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;

private:
	void Refresh();

	TSharedPtr<FMcContainer> Table;
	TSharedPtr<FMcContainer> Player;
	FString ItemName;
	int32 Cost = 0;
	bool bTooExpensive = false;
	// How many of the right-hand stack a material repair uses; 0 when taking the result uses all of it.
	int32 MaterialUsed = 0;
	int32 PlayerLevels = 0;
	bool bPlayerCreative = false;
	int32 Spent = 0;
};
