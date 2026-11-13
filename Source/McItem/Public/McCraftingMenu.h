// The crafting menus: the 2x2 grid in the player's inventory and the 3x3 of a crafting table. Slot 0 is the result,
// slots 1.. the grid row by row, then the player's slots. The result follows the grid through the recipe book, and
// taking it uses up one of each ingredient (leaving buckets, bowls and the like where they were).

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McRecipe.h"

class MCITEM_API FMcCraftingMenu : public FMcMenu
{
public:
	// GridSize is 2 or 3.
	FMcCraftingMenu(int32 InGridSize, const TSharedPtr<FMcContainer>& InPlayer);

	int32 GetGridSize() const { return GridSize; }
	// The grid and the result, to put things in the grid directly.
	const TSharedPtr<FMcContainer>& GetGrid() const { return Grid; }
	// The recipe the grid currently matches, null if none.
	const FMcRecipe* GetRecipe() const { return Match.Recipe; }
	// Looks again at the grid (after changing it behind the menu's back).
	void Refresh();
	// Called when a result is taken: the recipe's id and what came out (the server counts it and tells the advancements).
	TFunction<void(FName Recipe, const FMcItemStack& Result)> OnCrafted;
	// If set and it says no to a recipe (limited crafting: the player has not unlocked it), the grid shows no result for it.
	TFunction<bool(FName Recipe)> MayCraft;

	// The recipe book's click on a recipe: Id is GetPlaceId. The grid is emptied into the player's slots and the recipe's
	// ingredients are moved into it from there, one set (or as many sets as the slots hold, for shift-click).
	virtual bool ClickButton(int32 Id) override;
	static int32 GetPlaceId(int32 RecipeIndex, bool bAll) { return RecipeIndex * 2 + (bAll ? 1 : 0); }
	bool PlaceRecipe(const FMcRecipe& Recipe, bool bAll);
	// Whether the stacks (the player's slots and the grid) hold the ingredients of one craft of the recipe.
	static bool CanCraftWith(const FMcRecipe& Recipe, int32 GridSize, const TArray<FMcItemStack>& Available);

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override { return Slot != 0; }
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != 0; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != 0; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void SlotChanged(int32 Slot) override;
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;

private:
	// Uses up the ingredients of the crafted recipe.
	void ConsumeIngredients(TArray<FMcItemStack>& Dropped);

	int32 GridSize;
	TSharedPtr<FMcContainer> Grid;
	TSharedPtr<FMcContainer> Result;
	TSharedPtr<FMcContainer> Player;
	FMcCraftingMatch Match;
	bool bRefreshing = false;
};
