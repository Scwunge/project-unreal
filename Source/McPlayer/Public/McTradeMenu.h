// The trading menu of a villager or wandering trader (minecraft.wiki, "Trading"): the trades in a list the player picks
// from, the one or two things it wants in two slots and what it gives in a third, then the player's slots. Picking a trade
// moves what it wants from the player's inventory into the slots; taking the result makes the trade. The menu keeps its
// own copy of the trader's offers (the server's goes to the client with the window), and tells whoever opened it each
// trade made, so the trader's stock, demand, experience and the player's standing follow.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McTrading.h"

class MCPLAYER_API FMcTradeMenu : public FMcMenu
{
public:
	static constexpr int32 WantsSlot = 0;
	static constexpr int32 ExtraSlot = 1;
	static constexpr int32 ResultSlot = 2;

	explicit FMcTradeMenu(const TSharedPtr<FMcContainer>& PlayerInventory);

	// What the trader sells and how it stands with the player; the experience bar's numbers (a wandering trader has no
	// levels).
	void SetTrader(const TArray<McTrading::FOffer>& InOffers, int32 InReputation, int32 InLevel, int32 InXp, bool bInLevels);
	// The trader's offers, level and experience after a trade (more offers on a new level); the pick stays.
	void UpdateTrader(const TArray<McTrading::FOffer>& InOffers, int32 InLevel, int32 InXp);
	const TArray<McTrading::FOffer>& GetOffers() const { return Offers; }
	int32 GetSelected() const { return Selected; }
	int32 GetReputation() const { return Reputation; }
	int32 GetLevel() const { return Level; }
	int32 GetXp() const { return Xp; }
	bool HasLevels() const { return bLevels; }
	// What the trade costs this player now (its wants' count with demand and reputation).
	int32 GetCost(int32 Index) const;

	// Told after each trade made: the offer's index, and the experience the trader earns from it. Only the server sets it.
	TFunction<void(int32 Index, int32 TraderXp)> OnTrade;

	virtual bool ClickButton(int32 Id) override;
	virtual void SerializeState(FArchive& Ar) override;
	virtual int32 TakeExperience() override;

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override { return Slot != ResultSlot; }
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != ResultSlot; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != ResultSlot; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void SlotChanged(int32 Slot) override;
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;

private:
	// The result the slots make for the selected trade (empty if they do not pay for it or it is out of stock).
	void Refresh();
	// Puts back what is in the two slots, then moves what the selected trade wants from the player's slots into them.
	void Fill();
	void ReturnInputs();
	bool Pays(const FMcItemStack& Wanted, int32 Count, const FMcItemStack& Held) const;

	TSharedPtr<FMcContainer> Table;
	TSharedPtr<FMcContainer> Player;
	TArray<McTrading::FOffer> Offers;
	int32 Selected = -1;
	int32 Reputation = 0;
	int32 Level = 1;
	int32 Xp = 0;
	bool bLevels = true;
	int32 PendingPlayerXp = 0;
};
