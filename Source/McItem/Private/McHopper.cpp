#include "McHopper.h"
#include "McBrewingStand.h"
#include "McFurnace.h"
#include "McRecipe.h"

namespace
{
	TArray<int32> Range(int32 Start, int32 End)
	{
		TArray<int32> Slots;
		for (int32 Slot = Start; Slot < End; ++Slot)
		{
			Slots.Add(Slot);
		}
		return Slots;
	}

	// How many of Stack the slot can still take, within the access's rules.
	int32 SpaceIn(const FMcSlotAccess& To, int32 Slot, const FMcItemStack& Stack)
	{
		if (To.CanInsert && !To.CanInsert(Slot, Stack))
		{
			return 0;
		}
		const FMcItemStack& Current = To.GetStack(Slot);
		const int32 Limit = FMath::Min(To.SlotLimit ? To.SlotLimit(Slot) : 64, Stack.GetMaxStackSize());
		if (Current.IsEmpty())
		{
			return Limit;
		}
		return Current.IsSameItemSameComponents(Stack) ? FMath::Max(0, Limit - Current.Count) : 0;
	}
}

FMcSlotAccess McHopper::ForContainer(const TSharedPtr<FMcContainer>& Items, const TSharedPtr<FMcContainer>& More)
{
	FMcSlotAccess Access;
	Access.Items = Items;
	Access.More = More;
	Access.Insert = Range(0, Items->Num() + (More.IsValid() ? More->Num() : 0));
	Access.Extract = Access.Insert;
	return Access;
}

FMcSlotAccess McHopper::ForFurnace(const TSharedPtr<FMcFurnace>& Furnace, EFace Face)
{
	FMcSlotAccess Access;
	Access.Items = Furnace->GetItems();
	switch (Face)
	{
	case EFace::Top:
		Access.Insert = { FMcFurnace::InputSlot };
		break;
	case EFace::Side:
		Access.Insert = { FMcFurnace::FuelSlot };
		Access.CanInsert = [Furnace](int32, const FMcItemStack& Stack) { return Furnace->IsFuel(Stack); };
		break;
	case EFace::Bottom:
		Access.Extract = { FMcFurnace::OutputSlot };
		if (Furnace->GetItems()->Get(FMcFurnace::FuelSlot).Item == TEXT("minecraft:bucket"))
		{
			Access.Extract.Add(FMcFurnace::FuelSlot);
		}
		break;
	}
	return Access;
}

FMcSlotAccess McHopper::ForBrewingStand(const TSharedPtr<FMcBrewingStand>& Stand, EFace Face)
{
	FMcSlotAccess Access;
	Access.Items = Stand->GetItems();
	switch (Face)
	{
	case EFace::Top:
		Access.Insert = { FMcBrewingStand::IngredientSlot };
		Access.CanInsert = [](int32, const FMcItemStack& Stack)
		{
			FMcRecipeBook& Book = FMcRecipeBook::Get();
			return Book.EnsureLoaded() && Book.IsBrewingReagent(Stack);
		};
		break;
	case EFace::Side:
		Access.Insert = { FMcBrewingStand::FuelSlot, 0, 1, 2 };
		Access.CanInsert = [](int32 Slot, const FMcItemStack& Stack)
		{
			return Slot == FMcBrewingStand::FuelSlot ? FMcBrewingStand::IsFuel(Stack) : FMcBrewingStand::IsBottle(Stack);
		};
		// One bottle to a slot.
		Access.SlotLimit = [](int32 Slot) { return Slot == FMcBrewingStand::FuelSlot ? 64 : 1; };
		break;
	case EFace::Bottom:
		Access.Extract = { 0, 1, 2 };
		break;
	}
	return Access;
}

bool McHopper::Insert(const FMcSlotAccess& To, FMcItemStack& Stack)
{
	bool bMoved = false;
	// The slots in order, each taking what it can, whether empty or holding the same.
	for (const int32 Slot : To.Insert)
	{
		if (Stack.IsEmpty())
		{
			break;
		}
		const int32 Put = FMath::Min(SpaceIn(To, Slot, Stack), Stack.Count);
		if (Put <= 0)
		{
			continue;
		}
		FMcItemStack Current = To.GetStack(Slot);
		if (Current.IsEmpty())
		{
			Current = Stack;
			Current.Count = 0;
		}
		Current.Count += Put;
		To.SetStack(Slot, Current);
		Stack.Count -= Put;
		bMoved = true;
	}
	if (Stack.Count <= 0)
	{
		Stack = FMcItemStack();
	}
	return bMoved;
}

bool McHopper::MoveOne(const FMcSlotAccess& From, const FMcSlotAccess& To)
{
	if (!From.IsValid() || !To.IsValid())
	{
		return false;
	}
	for (const int32 Slot : From.Extract)
	{
		FMcItemStack Source = From.GetStack(Slot);
		if (Source.IsEmpty())
		{
			continue;
		}
		FMcItemStack One = Source;
		One.Count = 1;
		if (Insert(To, One))
		{
			--Source.Count;
			From.SetStack(Slot, Source.Count > 0 ? Source : FMcItemStack());
			return true;
		}
	}
	return false;
}
