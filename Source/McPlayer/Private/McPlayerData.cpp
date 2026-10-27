#include "McPlayerData.h"

UMcPlayerDataComponent::UMcPlayerDataComponent()
	: Inventory(MakeShared<FMcContainer>(InventorySize))
	, Armor(MakeShared<FMcContainer>(NumArmorSlots))
	, Offhand(MakeShared<FMcContainer>(1))
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UMcPlayerDataComponent::Scroll(int32 Selected, int32 Steps)
{
	return ((Selected + Steps) % 9 + 9) % 9;
}
