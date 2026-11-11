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
