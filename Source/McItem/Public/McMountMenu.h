// The window of a tamed horse, donkey or mule (vanilla shot: the saddle slot at the top left, the armour slot under it, a picture of the animal,
// and for a donkey or mule with a chest 15 slots in 5 columns and 3 rows; then the player's slots). The container holds the saddle (slot 0),
// the armour (slot 1) and the chest's slots from 2 on.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"

class MCITEM_API FMcMountMenu : public FMcMenu
{
public:
	FMcMountMenu(const TSharedPtr<FMcContainer>& InContainer, const TSharedPtr<FMcContainer>& PlayerInventory);

	static constexpr int32 SaddleSlot = 0;
	static constexpr int32 ArmorSlot = 1;
	static constexpr int32 ChestStart = 2;
	static constexpr int32 ChestSlots = 15;

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;
	virtual int32 GetSlotLimit(int32 Slot) const override;

private:
	TSharedPtr<FMcContainer> Container;
};
