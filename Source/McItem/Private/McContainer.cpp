#include "McContainer.h"

namespace
{
	// Ids of the slots outside the window.
	constexpr int32 OutsideWindow = -999;
	constexpr int32 PlayerMainSlots = 27;
	constexpr int32 PlayerHotbarSlots = 9;
	constexpr int32 PlayerInventorySlots = PlayerMainSlots + PlayerHotbarSlots;
}

// ---- FMcContainer ----

void FMcContainer::Set(int32 Slot, const FMcItemStack& Stack)
{
	Items[Slot] = Stack.IsEmpty() ? FMcItemStack() : Stack;
}

bool FMcContainer::IsEmpty() const
{
	for (const FMcItemStack& Stack : Items)
	{
		if (!Stack.IsEmpty())
		{
			return false;
		}
	}
	return true;
}

void FMcContainer::Clear()
{
	for (FMcItemStack& Stack : Items)
	{
		Stack.Clear();
	}
}

FMcItemStack FMcContainer::AddItem(const FMcItemStack& Stack)
{
	FMcItemStack Rest = Stack;
	// Onto stacks that are already there, then into empty slots.
	for (const bool bEmptyPass : { false, true })
	{
		for (FMcItemStack& Slot : Items)
		{
			if (Rest.IsEmpty())
			{
				return FMcItemStack();
			}
			if (Slot.IsEmpty() != bEmptyPass)
			{
				continue;
			}
			const int32 Space = Slot.GetSpaceFor(Rest);
			if (Space <= 0)
			{
				continue;
			}
			if (bEmptyPass)
			{
				Slot = Rest;
				Slot.Count = Space;
			}
			else
			{
				Slot.Count += Space;
			}
			Rest.Count -= Space;
		}
	}
	if (Rest.Count <= 0)
	{
		Rest.Clear();
	}
	return Rest;
}

bool FMcContainer::CanAddItem(const FMcItemStack& Stack) const
{
	FMcContainer Copy = *this;
	return Copy.AddItem(Stack).IsEmpty();
}

int32 FMcContainer::RemoveItem(FName Item, int32 Amount)
{
	int32 Removed = 0;
	for (FMcItemStack& Slot : Items)
	{
		if (Removed >= Amount)
		{
			break;
		}
		if (!Slot.IsEmpty() && Slot.Item == Item)
		{
			const int32 Take = FMath::Min(Amount - Removed, Slot.Count);
			Slot.Count -= Take;
			Removed += Take;
			if (Slot.Count <= 0)
			{
				Slot.Clear();
			}
		}
	}
	return Removed;
}

int32 FMcContainer::CountItem(FName Item) const
{
	int32 Count = 0;
	for (const FMcItemStack& Slot : Items)
	{
		if (!Slot.IsEmpty() && Slot.Item == Item)
		{
			Count += Slot.Count;
		}
	}
	return Count;
}

// ---- FMcMenu ----

int32 FMcMenu::AddSlot(const TSharedPtr<FMcContainer>& Container, int32 Index)
{
	FSlot Slot;
	Slot.Container = Container;
	Slot.Index = Index;
	return Slots.Add(MoveTemp(Slot));
}

void FMcMenu::AddPlayerSlots(const TSharedPtr<FMcContainer>& PlayerInventory)
{
	PlayerStart = Slots.Num();
	check(PlayerInventory->Num() >= PlayerInventorySlots);
	for (int32 Index = PlayerHotbarSlots; Index < PlayerInventorySlots; ++Index)
	{
		AddSlot(PlayerInventory, Index);
	}
	for (int32 Index = 0; Index < PlayerHotbarSlots; ++Index)
	{
		AddSlot(PlayerInventory, Index);
	}
}

void FMcMenu::SetStack(int32 Slot, const FMcItemStack& Stack)
{
	Slots[Slot].Container->Set(Slots[Slot].Index, Stack);
	SlotChanged(Slot);
}

bool FMcMenu::MoveItemStackTo(FMcItemStack& Stack, int32 Start, int32 End, bool bReverse)
{
	bool bMoved = false;
	const auto At = [&](int32 Step) { return bReverse ? End - 1 - Step : Start + Step; };
	const int32 Count = End - Start;
	if (Stack.GetMaxStackSize() > 1)
	{
		for (int32 Step = 0; Step < Count && !Stack.IsEmpty(); ++Step)
		{
			const int32 Slot = At(Step);
			FMcItemStack Current = GetStack(Slot);
			if (Current.IsEmpty() || !Current.IsSameItemSameComponents(Stack))
			{
				continue;
			}
			const int32 Space = FMath::Min(Current.GetSpaceFor(Stack, GetSlotLimit(Slot)), Stack.Count);
			if (Space > 0 && MayPlace(Slot, Stack))
			{
				Current.Count += Space;
				Stack.Count -= Space;
				SetStack(Slot, Current);
				bMoved = true;
			}
		}
	}
	for (int32 Step = 0; Step < Count && !Stack.IsEmpty(); ++Step)
	{
		const int32 Slot = At(Step);
		if (GetStack(Slot).IsEmpty() && MayPlace(Slot, Stack))
		{
			const int32 Put = FMath::Min(Stack.Count, EffectiveLimit(Slot, Stack));
			if (Put <= 0)
			{
				continue;
			}
			FMcItemStack Placed = Stack;
			Placed.Count = Put;
			Stack.Count -= Put;
			SetStack(Slot, Placed);
			bMoved = true;
		}
	}
	if (Stack.Count <= 0)
	{
		Stack.Clear();
	}
	return bMoved;
}

FMcItemStack FMcMenu::QuickMoveStack(int32 Slot)
{
	FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	FMcItemStack Rest = Original;
	bool bMoved;
	if (Slot < PlayerStart)
	{
		bMoved = MoveItemStackTo(Rest, PlayerStart, Slots.Num(), true);
	}
	else if (PlayerStart > 0)
	{
		bMoved = MoveItemStackTo(Rest, 0, PlayerStart, false);
	}
	else
	{
		// No menu of its own: the inventory's main part and the hotbar swap.
		const bool bInHotbar = Slot >= PlayerStart + PlayerMainSlots;
		bMoved = bInHotbar ? MoveItemStackTo(Rest, PlayerStart, PlayerStart + PlayerMainSlots, false)
			: MoveItemStackTo(Rest, PlayerStart + PlayerMainSlots, Slots.Num(), false);
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest);
	return Original;
}

void FMcMenu::Click(int32 Slot, int32 Button, EMcClickType Type, TArray<FMcItemStack>& Dropped, bool bCreative)
{
	if (Type != EMcClickType::QuickCraft && bDragging)
	{
		// Any other click ends a drag that wasn't finished.
		bDragging = false;
		DragSlots.Reset();
	}
	const bool bValid = Slots.IsValidIndex(Slot);
	switch (Type)
	{
	case EMcClickType::Pickup:
		Pickup(Slot, Button, Dropped);
		break;
	case EMcClickType::QuickMove:
		if (bValid && MayPickup(Slot))
		{
			FMcItemStack Moved = QuickMoveStack(Slot);
			// Moving a stack out of a slot that refills (the crafting result) goes on until nothing more moves.
			while (!Moved.IsEmpty() && GetStack(Slot).IsSameItemSameComponents(Moved))
			{
				Moved = QuickMoveStack(Slot);
			}
		}
		break;
	case EMcClickType::Swap:
		if (bValid)
		{
			SwapWithHotbar(Slot, Button, Dropped);
		}
		break;
	case EMcClickType::Clone:
		if (bCreative && bValid && Carried.IsEmpty() && !GetStack(Slot).IsEmpty())
		{
			Carried = GetStack(Slot);
			Carried.Count = Carried.GetMaxStackSize();
		}
		break;
	case EMcClickType::Throw:
		if (bValid && Carried.IsEmpty() && !GetStack(Slot).IsEmpty() && MayPickup(Slot))
		{
			FMcItemStack Stack = GetStack(Slot);
			FMcItemStack Thrown = Stack.Split(Button == 1 ? Stack.Count : 1);
			SetStack(Slot, Stack);
			OnTake(Slot, Thrown, Dropped);
			Dropped.Add(Thrown);
		}
		break;
	case EMcClickType::PickupAll:
		if (bValid)
		{
			PickupAll(Slot, Button);
		}
		break;
	case EMcClickType::QuickCraft:
		break;
	}
}

void FMcMenu::Pickup(int32 Slot, int32 Button, TArray<FMcItemStack>& Dropped)
{
	if (Slot == OutsideWindow)
	{
		if (!Carried.IsEmpty())
		{
			Dropped.Add(Carried.Split(Button == 0 ? Carried.Count : 1));
			if (Carried.Count <= 0)
			{
				Carried.Clear();
			}
		}
		return;
	}
	if (!Slots.IsValidIndex(Slot))
	{
		return;
	}
	FMcItemStack Stack = GetStack(Slot);
	if (Stack.IsEmpty())
	{
		if (!Carried.IsEmpty() && MayPlace(Slot, Carried))
		{
			const int32 Amount = FMath::Min(Button == 0 ? Carried.Count : 1, EffectiveLimit(Slot, Carried));
			SetStack(Slot, Carried.Split(Amount));
			if (Carried.Count <= 0)
			{
				Carried.Clear();
			}
		}
		return;
	}
	if (!MayPickup(Slot))
	{
		return;
	}
	if (Carried.IsEmpty())
	{
		FMcItemStack Taken = Stack.Split(Button == 0 ? Stack.Count : (Stack.Count + 1) / 2);
		SetStack(Slot, Stack);
		OnTake(Slot, Taken, Dropped);
		Carried = Taken;
		return;
	}
	if (MayPlace(Slot, Carried))
	{
		if (Stack.IsSameItemSameComponents(Carried))
		{
			const int32 Amount = FMath::Min(Button == 0 ? Carried.Count : 1, Stack.GetSpaceFor(Carried, GetSlotLimit(Slot)));
			if (Amount > 0)
			{
				Stack.Count += Amount;
				Carried.Count -= Amount;
				if (Carried.Count <= 0)
				{
					Carried.Clear();
				}
				SetStack(Slot, Stack);
			}
		}
		else if (Carried.Count <= EffectiveLimit(Slot, Carried))
		{
			SetStack(Slot, Carried);
			Carried = Stack;
		}
	}
	else if (Stack.IsSameItemSameComponents(Carried) && Carried.Count + Stack.Count <= Carried.GetMaxStackSize())
	{
		// Something that can't be put back (the crafting result), added to the cursor.
		FMcItemStack Taken = Stack;
		SetStack(Slot, FMcItemStack());
		OnTake(Slot, Taken, Dropped);
		Carried.Count += Taken.Count;
	}
}

void FMcMenu::SwapWithHotbar(int32 Slot, int32 Button, TArray<FMcItemStack>& Dropped)
{
	const int32 Hotbar = PlayerStart + PlayerMainSlots + Button;
	if (Button < 0 || Button >= PlayerHotbarSlots || Hotbar >= Slots.Num() || Hotbar == Slot || !Carried.IsEmpty())
	{
		return;
	}
	FMcItemStack Here = GetStack(Slot);
	FMcItemStack There = GetStack(Hotbar);
	if (Here.IsEmpty() && There.IsEmpty())
	{
		return;
	}
	if (Here.IsEmpty())
	{
		if (MayPlace(Slot, There))
		{
			const int32 Put = FMath::Min(There.Count, EffectiveLimit(Slot, There));
			SetStack(Slot, There.Split(Put));
			SetStack(Hotbar, There);
		}
	}
	else if (There.IsEmpty())
	{
		if (MayPickup(Slot))
		{
			FMcItemStack Taken = Here;
			SetStack(Slot, FMcItemStack());
			OnTake(Slot, Taken, Dropped);
			SetStack(Hotbar, Taken);
		}
	}
	else if (MayPickup(Slot) && MayPlace(Slot, There) && There.Count <= EffectiveLimit(Slot, There))
	{
		FMcItemStack Taken = Here;
		SetStack(Slot, There);
		OnTake(Slot, Taken, Dropped);
		SetStack(Hotbar, Taken);
	}
}

void FMcMenu::PickupAll(int32 Slot, int32 Button)
{
	if (Carried.IsEmpty())
	{
		return;
	}
	const int32 Max = Carried.GetMaxStackSize();
	// Stacks that aren't full go first, then the full ones.
	for (const bool bFullPass : { false, true })
	{
		for (int32 Step = 0; Step < Slots.Num() && Carried.Count < Max; ++Step)
		{
			const int32 Index = Button == 0 ? Step : Slots.Num() - 1 - Step;
			FMcItemStack Stack = GetStack(Index);
			if (Stack.IsEmpty() || !CanTakeForPickupAll(Index) || !MayPickup(Index) || !Stack.IsSameItemSameComponents(Carried))
			{
				continue;
			}
			const bool bFull = Stack.Count >= Stack.GetMaxStackSize();
			if (bFull != bFullPass)
			{
				continue;
			}
			const int32 Take = FMath::Min(Stack.Count, Max - Carried.Count);
			Carried.Count += Take;
			Stack.Count -= Take;
			SetStack(Index, Stack.Count > 0 ? Stack : FMcItemStack());
		}
	}
}

void FMcMenu::BeginQuickCraft(int32 Button)
{
	bDragging = true;
	DragButton = Button;
	DragSlots.Reset();
}

void FMcMenu::AddQuickCraftSlot(int32 Slot)
{
	if (!bDragging || !Slots.IsValidIndex(Slot) || Carried.IsEmpty() || DragSlots.Contains(Slot))
	{
		return;
	}
	const FMcItemStack& Stack = GetStack(Slot);
	if (!CanDragTo(Slot) || !MayPlace(Slot, Carried) || (!Stack.IsEmpty() && !Stack.IsSameItemSameComponents(Carried)))
	{
		return;
	}
	// A split can't use more slots than there are items.
	if (DragButton != 1 && DragSlots.Num() >= Carried.Count)
	{
		return;
	}
	DragSlots.Add(Slot);
}

void FMcMenu::EndQuickCraft(bool bCreative)
{
	if (!bDragging)
	{
		return;
	}
	bDragging = false;
	TArray<int32> Targets = MoveTemp(DragSlots);
	DragSlots.Reset();
	if (Targets.IsEmpty() || Carried.IsEmpty())
	{
		return;
	}
	if (Targets.Num() == 1)
	{
		// One slot is a plain click.
		TArray<FMcItemStack> Dropped;
		Pickup(Targets[0], DragButton == 1 ? 1 : 0, Dropped);
		return;
	}
	if (DragButton == 2 && !bCreative)
	{
		return;
	}
	int32 Remaining = Carried.Count;
	const int32 Each = DragButton == 0 ? Carried.Count / Targets.Num() : DragButton == 1 ? 1 : Carried.GetMaxStackSize();
	for (const int32 Slot : Targets)
	{
		FMcItemStack Stack = GetStack(Slot);
		const int32 Existing = Stack.IsEmpty() ? 0 : Stack.Count;
		const int32 Limit = EffectiveLimit(Slot, Carried);
		const int32 NewCount = FMath::Min(Existing + Each, Limit);
		const int32 Add = NewCount - Existing;
		if (Add <= 0 || (DragButton != 2 && Add > Remaining))
		{
			continue;
		}
		if (Stack.IsEmpty())
		{
			Stack = Carried;
		}
		Stack.Count = NewCount;
		SetStack(Slot, Stack);
		if (DragButton != 2)
		{
			Remaining -= Add;
		}
	}
	Carried.Count = Remaining;
	if (Carried.Count <= 0)
	{
		Carried.Clear();
	}
}

void FMcMenu::Close(TArray<FMcItemStack>& Dropped)
{
	bDragging = false;
	DragSlots.Reset();
	OnClosed(Dropped);
	if (!Carried.IsEmpty())
	{
		FMcItemStack Rest = Carried;
		MoveItemStackTo(Rest, PlayerStart, Slots.Num(), false);
		if (!Rest.IsEmpty())
		{
			Dropped.Add(Rest);
		}
		Carried.Clear();
	}
}

// ---- FMcContainerMenu ----

FMcContainerMenu::FMcContainerMenu(const TSharedPtr<FMcContainer>& InContainer, const TSharedPtr<FMcContainer>& PlayerInventory)
	: Container(InContainer)
{
	for (int32 Index = 0; Index < Container->Num(); ++Index)
	{
		AddSlot(Container, Index);
	}
	AddPlayerSlots(PlayerInventory);
}
