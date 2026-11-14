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
		if (Match.Recipe && MayCraft && !MayCraft(Match.Recipe->Id))
		{
			Match = FMcCraftingMatch();
			return;
		}
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
	if (OnCrafted)
	{
		OnCrafted(Match.Recipe->Id, Taken);
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

namespace
{
	// The grid slot and the ingredient of each part of a crafting recipe (a shaped one keeps its shape from the top left, a
	// shapeless one fills the slots in order). False if the recipe isn't one the grid can hold.
	bool GetPlacement(const FMcRecipe& Recipe, int32 GridSize, TArray<int32>& OutSlots, TArray<const FMcIngredient*>& OutNeeds)
	{
		if (Recipe.Type != EMcRecipeType::CraftingShaped && Recipe.Type != EMcRecipeType::CraftingShapeless)
		{
			return false;
		}
		if (Recipe.Type == EMcRecipeType::CraftingShaped)
		{
			if (Recipe.Width > GridSize || Recipe.Height > GridSize || Recipe.Ingredients.Num() < Recipe.Width * Recipe.Height)
			{
				return false;
			}
			for (int32 Y = 0; Y < Recipe.Height; ++Y)
			{
				for (int32 X = 0; X < Recipe.Width; ++X)
				{
					const FMcIngredient& Ingredient = Recipe.Ingredients[Y * Recipe.Width + X];
					if (!Ingredient.IsEmpty())
					{
						OutSlots.Add(Y * GridSize + X);
						OutNeeds.Add(&Ingredient);
					}
				}
			}
			return OutNeeds.Num() > 0;
		}
		if (Recipe.Ingredients.Num() > GridSize * GridSize)
		{
			return false;
		}
		for (int32 Index = 0; Index < Recipe.Ingredients.Num(); ++Index)
		{
			OutSlots.Add(Index);
			OutNeeds.Add(&Recipe.Ingredients[Index]);
		}
		return OutNeeds.Num() > 0;
	}

	// Picks, for each ingredient, a stack of Have that satisfies it (taking one from a copy of the counts); the index of the
	// stack for each, or empty if some ingredient can't be met.
	bool Pick(const TArray<const FMcIngredient*>& Needs, TArray<int32>& Counts, const TArray<const FMcItemStack*>& Have, TArray<int32>& OutPicked)
	{
		OutPicked.Reset();
		for (const FMcIngredient* Need : Needs)
		{
			int32 Found = INDEX_NONE;
			for (int32 Index = 0; Index < Have.Num() && Found == INDEX_NONE; ++Index)
			{
				if (Counts[Index] > 0 && Need->Test(*Have[Index]))
				{
					Found = Index;
				}
			}
			if (Found == INDEX_NONE)
			{
				return false;
			}
			--Counts[Found];
			OutPicked.Add(Found);
		}
		return true;
	}
}

bool FMcCraftingMenu::CanCraftWith(const FMcRecipe& Recipe, int32 GridSize, const TArray<FMcItemStack>& Available)
{
	TArray<int32> Slots;
	TArray<const FMcIngredient*> Needs;
	if (!GetPlacement(Recipe, GridSize, Slots, Needs))
	{
		return false;
	}
	TArray<const FMcItemStack*> Have;
	TArray<int32> Counts;
	for (const FMcItemStack& Stack : Available)
	{
		if (!Stack.IsEmpty())
		{
			Have.Add(&Stack);
			Counts.Add(Stack.Count);
		}
	}
	TArray<int32> Picked;
	return Pick(Needs, Counts, Have, Picked);
}

bool FMcCraftingMenu::PlaceRecipe(const FMcRecipe& Recipe, bool bAll)
{
	TArray<int32> GridSlots;
	TArray<const FMcIngredient*> Needs;
	if (!GetPlacement(Recipe, GridSize, GridSlots, Needs) || (MayCraft && !MayCraft(Recipe.Id)))
	{
		return false;
	}
	// The grid goes back to the player's slots first (whatever doesn't fit stops it before anything has moved).
	TArray<FMcItemStack> Saved = Grid->GetAll();
	TArray<FMcItemStack> Held;
	for (int32 Slot = PlayerStart; Slot < NumSlots(); ++Slot)
	{
		Held.Add(GetStack(Slot));
	}
	const auto Restore = [&]()
	{
		for (int32 Slot = PlayerStart; Slot < NumSlots(); ++Slot)
		{
			SetStack(Slot, Held[Slot - PlayerStart]);
		}
		for (int32 Index = 0; Index < Saved.Num(); ++Index)
		{
			Grid->Set(Index, Saved[Index]);
		}
		Refresh();
	};
	for (int32 Index = 0; Index < Grid->Num(); ++Index)
	{
		FMcItemStack Stack = Grid->Get(Index);
		if (Stack.IsEmpty())
		{
			continue;
		}
		MoveItemStackTo(Stack, PlayerStart, NumSlots(), true);
		if (!Stack.IsEmpty())
		{
			Restore();
			return false;
		}
		Grid->Set(Index, FMcItemStack());
	}
	// As many sets as asked, each from what is left in the player's slots.
	int32 Sets = 0;
	while (Sets < (bAll ? 64 : 1))
	{
		TArray<const FMcItemStack*> Have;
		TArray<int32> Index;
		TArray<int32> Counts;
		for (int32 Slot = PlayerStart; Slot < NumSlots(); ++Slot)
		{
			if (!GetStack(Slot).IsEmpty())
			{
				Have.Add(&GetStack(Slot));
				Index.Add(Slot);
				Counts.Add(GetStack(Slot).Count);
			}
		}
		TArray<int32> Picked;
		if (!Pick(Needs, Counts, Have, Picked))
		{
			break;
		}
		// The grid slots take one of each; a set that doesn't fit the stack sizes ends it.
		bool bFits = true;
		for (int32 Part = 0; Part < Needs.Num() && bFits; ++Part)
		{
			const FMcItemStack& From = GetStack(Index[Picked[Part]]);
			const FMcItemStack& To = Grid->Get(GridSlots[Part]);
			bFits = To.IsEmpty() || (To.IsSameItemSameComponents(From) && To.Count < To.GetMaxStackSize());
		}
		if (!bFits)
		{
			break;
		}
		for (int32 Part = 0; Part < Needs.Num(); ++Part)
		{
			FMcItemStack Taken = GetStack(Index[Picked[Part]]);
			Taken.Count = 1;
			FMcItemStack Left = GetStack(Index[Picked[Part]]);
			Left.Count -= 1;
			SetStack(Index[Picked[Part]], Left.Count > 0 ? Left : FMcItemStack());
			FMcItemStack Placed = Grid->Get(GridSlots[Part]);
			if (Placed.IsEmpty())
			{
				Placed = Taken;
			}
			else
			{
				Placed.Count += 1;
			}
			Grid->Set(GridSlots[Part], Placed);
		}
		++Sets;
	}
	if (Sets == 0)
	{
		Restore();
		return false;
	}
	Refresh();
	return true;
}

bool FMcCraftingMenu::ClickButton(int32 Id)
{
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (Id < 0 || !Book.EnsureLoaded() || !Book.GetAll().IsValidIndex(Id / 2))
	{
		return false;
	}
	return PlaceRecipe(Book.GetAll()[Id / 2], (Id & 1) != 0);
}
