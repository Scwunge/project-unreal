#include "McMenuInput.h"

void FMcMenuInput::MouseDown(int32 Slot, int32 Button, bool bShift, double Time)
{
	const bool bOnSlot = Slot >= 0 && Slot < Menu->NumSlots();
	if (Button == 2)
	{
		if (bOnSlot)
		{
			DoClick(Slot, 0, EMcClickType::Clone);
		}
		return;
	}
	if (Button != 0 && Button != 1)
	{
		return;
	}
	if (bOnSlot && bShift)
	{
		DoClick(Slot, Button, EMcClickType::QuickMove);
		LastClickSlot = INDEX_NONE;
		return;
	}
	// A second left click on a slot soon after the first gathers what matches the cursor stack.
	const bool bDouble = bOnSlot && Button == 0 && Slot == LastClickSlot && Time - LastClickTime <= DoubleClickSeconds && !Menu->GetCarried().IsEmpty();
	LastClickSlot = bOnSlot ? Slot : INDEX_NONE;
	LastClickTime = Time;
	if (bDouble)
	{
		DoClick(Slot, 0, EMcClickType::PickupAll);
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
	DoClick(bOnSlot ? Slot : Outside, Button, EMcClickType::Pickup);
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
		DragSlots.Reset();
		DragSlots.Add(PressSlot);
		Menu->BeginQuickCraft(PressButton == 0 ? 0 : 1);
		Menu->AddQuickCraftSlot(PressSlot);
	}
	DragSlots.AddUnique(Slot);
	Menu->AddQuickCraftSlot(Slot);
}

void FMcMenuInput::FinishDrag()
{
	PressSlot = INDEX_NONE;
	bDragActive = false;
	DragSlots.Reset();
}

void FMcMenuInput::DoClick(int32 Slot, int32 Button, EMcClickType Type)
{
	Menu->Click(Slot, Button, Type, Dropped, bCreative);
	if (OnClick)
	{
		OnClick(Slot, Button, Type);
	}
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
		if (OnDrag)
		{
			OnDrag(PressButton == 0 ? 0 : 1, DragSlots);
		}
	}
	else
	{
		DoClick(PressSlot, PressButton, EMcClickType::Pickup);
	}
	FinishDrag();
}

void FMcMenuInput::NumberKey(int32 Slot, int32 Number)
{
	if (Slot >= 0 && Slot < Menu->NumSlots() && Number >= 0 && Number < 9)
	{
		DoClick(Slot, Number, EMcClickType::Swap);
	}
}

void FMcMenuInput::DropKey(int32 Slot, bool bCtrl)
{
	if (Slot >= 0 && Slot < Menu->NumSlots())
	{
		DoClick(Slot, bCtrl ? 1 : 0, EMcClickType::Throw);
	}
}

void FMcMenuInput::Close()
{
	FinishDrag();
	Menu->Close(Dropped);
}
