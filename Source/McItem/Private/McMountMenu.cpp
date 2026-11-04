#include "McMountMenu.h"

FMcMountMenu::FMcMountMenu(const TSharedPtr<FMcContainer>& InContainer, const TSharedPtr<FMcContainer>& PlayerInventory)
	: Container(InContainer)
{
	for (int32 Index = 0; Index < Container->Num(); ++Index)
	{
		AddSlot(Container, Index);
	}
	AddPlayerSlots(PlayerInventory);
}

bool FMcMountMenu::MayPlace(int32 Slot, const FMcItemStack& Stack) const
{
	if (Slot == SaddleSlot)
	{
		return Stack.Item == TEXT("minecraft:saddle");
	}
	if (Slot == ArmorSlot)
	{
		return Stack.Item.ToString().EndsWith(TEXT("_horse_armor"));
	}
	return true;
}

int32 FMcMountMenu::GetSlotLimit(int32 Slot) const
{
	return Slot == SaddleSlot || Slot == ArmorSlot ? 1 : 64;
}
