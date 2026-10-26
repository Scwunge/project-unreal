#include "McCraftingMenu.h"

FMcCraftingMenu::FMcCraftingMenu(int32 InGridSize, const TSharedPtr<FMcContainer>& InPlayer)
	: GridSize(InGridSize)
	, Grid(MakeShared<FMcContainer>(InGridSize * InGridSize))
	, Result(MakeShared<FMcContainer>(1))
	, Player(InPlayer)
{
	AddSlot(Result, 0);
	for (int32 Index = 0; Index < Grid->Num(); ++Index)
	{
		AddSlot(Grid, Index);
	}
	AddPlayerSlots(InPlayer);
}

void FMcCraftingMenu::Refresh()
{
	Match = FMcCraftingMatch();
	Result->Clear();
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (Book.EnsureLoaded() && Book.FindCraftingMatch(Grid->GetAll(), GridSize, GridSize, Match))
	{
		Result->Set(0, Match.Result);
	}
}

void FMcCraftingMenu::SlotChanged(int32 Slot)
{
	if (Slot > 0 && Slot < PlayerStart)
	{
		Refresh();
	}
}

void FMcCraftingMenu::ConsumeIngredients(TArray<FMcItemStack>& Dropped)
{
	TArray<FMcItemStack> Remainders;
	FMcRecipeBook::GetRemainders(Match.Recipe, Grid->GetAll(), Remainders);
	for (int32 Index = 0; Index < Grid->Num(); ++Index)
	{
		FMcItemStack& Stack = Grid->GetMutable(Index);
		if (Stack.IsEmpty())
		{
			continue;
		}
		Stack.Count -= 1;
		if (Stack.Count <= 0)
		{
			Stack.Clear();
		}
		FMcItemStack Left = Remainders[Index];
		if (Left.IsEmpty())
		{
			continue;
		}
		if (Stack.IsEmpty())
		{
			Stack = Left;
		}
		else if (Stack.IsSameItemSameComponents(Left) && Stack.GetSpaceFor(Left) > 0)
		{
			Stack.Count += Left.Count;
		}
		else
		{
			// It doesn't fit back in the grid: into the player's inventory, or onto the floor.
			const FMcItemStack Rest = Player->AddItem(Left);
			if (!Rest.IsEmpty())
			{
				Dropped.Add(Rest);
			}
		}
	}
}

void FMcCraftingMenu::OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped)
{
	if (Slot != 0 || !Match.Recipe)
	{
		return;
	}
	ConsumeIngredients(Dropped);
	Refresh();
}

FMcItemStack FMcCraftingMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	if (Slot == 0)
	{
		// One craft, if all of it fits in the inventory.
		if (!Player->CanAddItem(Original))
		{
			return FMcItemStack();
		}
		FMcItemStack Rest = Original;
		MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
		TArray<FMcItemStack> Dropped;
		SetStack(0, FMcItemStack());
		FMcItemStack Taken = Original;
		OnTake(0, Taken, Dropped);
		// What a remainder pushed out of the grid can't be kept here; the caller of Click gets nothing, so put it back.
		for (const FMcItemStack& Extra : Dropped)
		{
			Player->AddItem(Extra);
		}
		return Original;
	}
	FMcItemStack Rest = Original;
	bool bMoved;
	if (Slot < PlayerStart)
	{
		bMoved = MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
	}
	else
	{
		// The player's own slots: the main part and the hotbar trade.
		const int32 HotbarStart = PlayerStart + 27;
		bMoved = Slot >= HotbarStart ? MoveItemStackTo(Rest, PlayerStart, HotbarStart, false) : MoveItemStackTo(Rest, HotbarStart, NumSlots(), false);
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest);
	return Original;
}

void FMcCraftingMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	for (int32 Index = 0; Index < Grid->Num(); ++Index)
	{
		if (!Grid->Get(Index).IsEmpty())
		{
			const FMcItemStack Rest = Player->AddItem(Grid->Get(Index));
			if (!Rest.IsEmpty())
			{
				Dropped.Add(Rest);
			}
		}
	}
	Grid->Clear();
	Refresh();
}
