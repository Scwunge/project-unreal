#include "McPlayerMenu.h"
#include "McItemRegistry.h"

FMcPlayerMenu::FMcPlayerMenu(const TSharedPtr<FMcContainer>& InInventory, const TSharedPtr<FMcContainer>& InArmor, const TSharedPtr<FMcContainer>& InOffhand)
	: FMcCraftingMenu(2, InInventory)
	, Inventory(InInventory)
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		AddSlot(InArmor, Index);
	}
	AddSlot(InOffhand, 0);
	check(NumSlots() == FMcPlayerMenuSlots::Total);
}

int32 FMcPlayerMenu::GetArmorSlotFor(const FMcItemStack& Stack)
{
	FMcArmorData Armor;
	if (Stack.IsEmpty() || !FMcItemRegistry::Get().GetArmor(Stack.Item, Armor))
	{
		return -1;
	}
	static const TCHAR* Names[4] = { TEXT("head"), TEXT("chest"), TEXT("legs"), TEXT("feet") };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Armor.Slot == Names[Index])
		{
			return Index;
		}
	}
	return -1;
}

bool FMcPlayerMenu::MayPlace(int32 Slot, const FMcItemStack& Stack) const
{
	if (Slot == FMcPlayerMenuSlots::Result)
	{
		return false;
	}
	if (Slot >= FMcPlayerMenuSlots::ArmorFirst && Slot < FMcPlayerMenuSlots::Offhand)
	{
		return GetArmorSlotFor(Stack) == Slot - FMcPlayerMenuSlots::ArmorFirst;
	}
	return true;
}

int32 FMcPlayerMenu::GetSlotLimit(int32 Slot) const
{
	return Slot >= FMcPlayerMenuSlots::ArmorFirst && Slot < FMcPlayerMenuSlots::Offhand ? 1 : 64;
}

bool FMcPlayerMenu::CanDragTo(int32 Slot) const
{
	return Slot != 0 && !(Slot >= FMcPlayerMenuSlots::ArmorFirst && Slot < FMcPlayerMenuSlots::Offhand);
}

FMcItemStack FMcPlayerMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	const int32 PlayerEnd = PlayerStart + 36;
	if (Slot == FMcPlayerMenuSlots::Result)
	{
		// One craft, if all of it fits in the inventory.
		if (!Inventory->CanAddItem(Original))
		{
			return FMcItemStack();
		}
		FMcItemStack Rest = Original;
		MoveItemStackTo(Rest, PlayerStart, PlayerEnd, true);
		TArray<FMcItemStack> Dropped;
		SetStack(0, FMcItemStack());
		FMcItemStack Taken = Original;
		OnTake(0, Taken, Dropped);
		for (const FMcItemStack& Extra : Dropped)
		{
			Inventory->AddItem(Extra);
		}
		return Original;
	}
	FMcItemStack Rest = Original;
	bool bMoved;
	if (Slot < PlayerStart)
	{
		bMoved = MoveItemStackTo(Rest, PlayerStart, PlayerEnd, true);
	}
	else if (Slot >= PlayerEnd)
	{
		bMoved = MoveItemStackTo(Rest, PlayerStart, PlayerEnd, false);
	}
	else
	{
		const int32 Worn = GetArmorSlotFor(Original);
		if (Worn >= 0 && GetStack(FMcPlayerMenuSlots::ArmorFirst + Worn).IsEmpty())
		{
			// A piece of armour goes on, one of them.
			FMcItemStack Piece = Rest.Split(1);
			SetStack(FMcPlayerMenuSlots::ArmorFirst + Worn, Piece);
			SetStack(Slot, Rest.IsEmpty() ? FMcItemStack() : Rest);
			return Original;
		}
		// The main part and the hotbar trade.
		const int32 HotbarStart = PlayerStart + 27;
		bMoved = Slot >= HotbarStart ? MoveItemStackTo(Rest, PlayerStart, HotbarStart, false) : MoveItemStackTo(Rest, HotbarStart, PlayerEnd, false);
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest);
	return Original;
}

// ---- What the server accepts ----

McMenuRpc::EResult McMenuRpc::ValidateClick(const FMcMenu& Menu, int32 ServerWindow, int32 ClaimedWindow, int32 Slot, int32 Button, int32 Type, bool bCreative)
{
	if (ServerWindow < 0 || ClaimedWindow != ServerWindow)
	{
		return EResult::WrongWindow;
	}
	if (Type < 0 || Type > static_cast<int32>(EMcClickType::PickupAll) || Type == static_cast<int32>(EMcClickType::QuickCraft))
	{
		// Drags arrive as a whole, with ValidateDrag.
		return EResult::BadType;
	}
	const EMcClickType Kind = static_cast<EMcClickType>(Type);
	const bool bOnSlot = Slot >= 0 && Slot < Menu.NumSlots();
	// Only a plain click may land outside the window (it throws the cursor stack).
	if (!bOnSlot && !(Kind == EMcClickType::Pickup && Slot == OutsideSlot))
	{
		return EResult::BadSlot;
	}
	switch (Kind)
	{
	case EMcClickType::Pickup:
	case EMcClickType::QuickMove:
	case EMcClickType::Throw:
		return Button == 0 || Button == 1 ? EResult::Ok : EResult::BadButton;
	case EMcClickType::Swap:
		return Button >= 0 && Button < 9 ? EResult::Ok : EResult::BadButton;
	case EMcClickType::Clone:
		if (!bCreative)
		{
			return EResult::NotAllowed;
		}
		return Button == 0 ? EResult::Ok : EResult::BadButton;
	case EMcClickType::PickupAll:
		return Button == 0 ? EResult::Ok : EResult::BadButton;
	default:
		return EResult::BadType;
	}
}

McMenuRpc::EResult McMenuRpc::ValidateDrag(const FMcMenu& Menu, int32 ServerWindow, int32 ClaimedWindow, int32 Button, const TArray<int32>& Slots, bool bCreative)
{
	if (ServerWindow < 0 || ClaimedWindow != ServerWindow)
	{
		return EResult::WrongWindow;
	}
	if (Button < 0 || Button > 2)
	{
		return EResult::BadButton;
	}
	if (Button == 2 && !bCreative)
	{
		return EResult::NotAllowed;
	}
	if (Slots.IsEmpty() || Slots.Num() > MaxDragSlots)
	{
		return EResult::BadSlot;
	}
	for (const int32 Slot : Slots)
	{
		if (Slot < 0 || Slot >= Menu.NumSlots())
		{
			return EResult::BadSlot;
		}
	}
	return EResult::Ok;
}
