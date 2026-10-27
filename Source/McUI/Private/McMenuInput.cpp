#include "McMenuInput.h"

void FMcMenuInput::MouseDown(int32 Slot, int32 Button, bool bShift, double Time)
{
	const bool bOnSlot = Slot >= 0 && Slot < Menu->NumSlots();
	if (Button == 2)
	{
		if (bOnSlot)
		{
			Menu->Click(Slot, 0, EMcClickType::Clone, Dropped, bCreative);
		}
		return;
	}
	if (Button != 0 && Button != 1)
	{
		return;
	}
	if (bOnSlot && bShift)
	{
		Menu->Click(Slot, Button, EMcClickType::QuickMove, Dropped, bCreative);
		LastClickSlot = INDEX_NONE;
		return;
	}
	// A second left click on a slot soon after the first gathers what matches the cursor stack.
	const bool bDouble = bOnSlot && Button == 0 && Slot == LastClickSlot && Time - LastClickTime <= DoubleClickSeconds && !Menu->GetCarried().IsEmpty();
	LastClickSlot = bOnSlot ? Slot : INDEX_NONE;
	LastClickTime = Time;
	if (bDouble)
	{
		Menu->Click(Slot, 0, EMcClickType::PickupAll, Dropped, bCreative);
		LastClickSlot = INDEX_NONE;
		return;
	}
	if (bOnSlot && !Menu->GetCarried().IsEmpty())
	{
		// Wait: let go over this slot and it is a click, move to another first and it is a drag.
		PressSlot = Slot;
		PressButton = Button;
		bDragActive = false;
		return;
	}
	Menu->Click(bOnSlot ? Slot : Outside, Button, EMcClickType::Pickup, Dropped, bCreative);
}

void FMcMenuInput::MouseMove(int32 Slot)
{
	if (PressSlot == INDEX_NONE || Slot < 0 || Slot >= Menu->NumSlots())
	{
		return;
	}
	if (!bDragActive)
	{
		if (Slot == PressSlot)
		{
			return;
		}
		bDragActive = true;
		Menu->BeginQuickCraft(PressButton == 0 ? 0 : 1);
		Menu->AddQuickCraftSlot(PressSlot);
	}
	Menu->AddQuickCraftSlot(Slot);
}

void FMcMenuInput::FinishDrag()
{
	PressSlot = INDEX_NONE;
	bDragActive = false;
}

void FMcMenuInput::MouseUp(int32 Slot, int32 Button)
{
	if (PressSlot == INDEX_NONE || Button != PressButton)
	{
		return;
	}
	if (bDragActive)
	{
		Menu->EndQuickCraft(bCreative);
	}
	else
	{
		Menu->Click(PressSlot, PressButton, EMcClickType::Pickup, Dropped, bCreative);
	}
	FinishDrag();
}

void FMcMenuInput::NumberKey(int32 Slot, int32 Number)
{
	if (Slot >= 0 && Slot < Menu->NumSlots() && Number >= 0 && Number < 9)
	{
		Menu->Click(Slot, Number, EMcClickType::Swap, Dropped, bCreative);
	}
}

void FMcMenuInput::DropKey(int32 Slot, bool bCtrl)
{
	if (Slot >= 0 && Slot < Menu->NumSlots())
	{
		Menu->Click(Slot, bCtrl ? 1 : 0, EMcClickType::Throw, Dropped, bCreative);
	}
}

void FMcMenuInput::Close()
{
	FinishDrag();
	Menu->Close(Dropped);
}
