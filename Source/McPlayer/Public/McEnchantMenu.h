// The enchanting table's menu (minecraft.wiki, "Enchanting Table"): the item to enchant and the lapis lazuli,
// then the player's slots. What it offers comes from McEnchanting, from the bookshelves around the table and the
// player's enchantment seed, so the client works out the same offers as the server from the same two numbers.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McEnchanting.h"

class MCPLAYER_API FMcEnchantMenu : public FMcMenu
{
public:
	static constexpr int32 ItemSlot = 0;
	static constexpr int32 LapisSlot = 1;

	FMcEnchantMenu(const TSharedPtr<FMcContainer>& PlayerInventory, int32 InBookshelves, int32 InSeed);

	int32 GetBookshelves() const { return Bookshelves; }
	int32 GetSeed() const { return Seed; }
	// A new seed (after the player enchants), and the offers worked out again.
	void SetSeed(int32 InSeed);
	const McEnchanting::FOffers& GetOffers() const { return Offers; }

	// Whether option Slot (0 to 2) can be taken by a player with Levels: there must be an offer, Slot + 1 lapis, and
	// the cost in levels. Creative players need none of the lapis or levels.
	bool CanEnchant(int32 Slot, int32 Levels, bool bCreative) const;
	// Takes option Slot: enchants the item and uses Slot + 1 lapis (none in creative). The levels to take from the
	// player (Slot + 1, none in creative) come back in OutLevelsSpent. False, changing nothing, if it can't.
	bool Enchant(int32 Slot, int32 Levels, bool bCreative, int32& OutLevelsSpent);

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;
	virtual int32 GetSlotLimit(int32 Slot) const override { return Slot == ItemSlot ? 1 : 64; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;
	virtual void SlotChanged(int32 Slot) override;

private:
	void Refresh();

	TSharedPtr<FMcContainer> Table;
	TSharedPtr<FMcContainer> Player;
	int32 Bookshelves = 0;
	int32 Seed = 0;
	McEnchanting::FOffers Offers;
	// The item the offers are for, to tell when it changes.
	FName OffersFor;
};
