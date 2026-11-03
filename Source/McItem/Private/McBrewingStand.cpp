#include "McBrewingStand.h"
#include "McItemRegistry.h"
#include "McRecipe.h"

FMcBrewingStand::FMcBrewingStand()
	: Items(MakeShared<FMcContainer>(5))
{
}

bool FMcBrewingStand::IsBottle(const FMcItemStack& Stack)
{
	return Stack.Item == TEXT("minecraft:potion") || Stack.Item == TEXT("minecraft:splash_potion")
		|| Stack.Item == TEXT("minecraft:lingering_potion") || Stack.Item == TEXT("minecraft:glass_bottle");
}

bool FMcBrewingStand::IsFuel(const FMcItemStack& Stack)
{
	return Stack.Item == TEXT("minecraft:blaze_powder");
}

bool FMcBrewingStand::CanBrew() const
{
	const FMcItemStack& Ingredient = Items->Get(IngredientSlot);
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (Ingredient.IsEmpty() || !Book.EnsureLoaded())
	{
		return false;
	}
	for (int32 Slot = FirstBottle; Slot < IngredientSlot; ++Slot)
	{
		FMcItemStack Out;
		if (!Items->Get(Slot).IsEmpty() && Book.FindBrewing(Items->Get(Slot), Ingredient, Out))
		{
			return true;
		}
	}
	return false;
}

void FMcBrewingStand::Brew()
{
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	FMcItemStack& Ingredient = Items->GetMutable(IngredientSlot);
	for (int32 Slot = FirstBottle; Slot < IngredientSlot; ++Slot)
	{
		FMcItemStack Out;
		if (!Items->Get(Slot).IsEmpty() && Book.FindBrewing(Items->Get(Slot), Ingredient, Out))
		{
			Items->Set(Slot, Out);
		}
	}
	// What a used-up ingredient leaves (none of the brewing ingredients leaves anything today, but the rule is the item's).
	const FName Remainder = FMcItemRegistry::Get().GetUseRemainder(Ingredient.Item);
	if (--Ingredient.Count <= 0)
	{
		Ingredient = Remainder.IsNone() ? FMcItemStack() : FMcItemStack(Remainder);
	}
}

bool FMcBrewingStand::Tick()
{
	bool bChanged = false;
	FMcItemStack& FuelStack = Items->GetMutable(FuelSlot);
	if (Fuel <= 0 && IsFuel(FuelStack))
	{
		Fuel = FuelPerPowder;
		if (--FuelStack.Count <= 0)
		{
			FuelStack.Clear();
		}
		bChanged = true;
	}
	const bool bCanBrew = CanBrew();
	const FName Ingredient = Items->Get(IngredientSlot).Item;
	if (BrewTimeLeft > 0)
	{
		if (!bCanBrew || Ingredient != Brewing)
		{
			BrewTimeLeft = 0;
			bChanged = true;
		}
		else if (--BrewTimeLeft == 0)
		{
			Brew();
			bFinished = true;
			bChanged = true;
		}
	}
	else if (bCanBrew && Fuel > 0)
	{
		--Fuel;
		BrewTimeLeft = BrewTime;
		Brewing = Ingredient;
		bChanged = true;
	}
	return bChanged;
}

bool FMcBrewingStand::Serialize(FArchive& Ar)
{
	for (int32 Slot = 0; Slot < Items->Num(); ++Slot)
	{
		if (!Items->GetMutable(Slot).Serialize(Ar))
		{
			return false;
		}
	}
	FString Name = Brewing.ToString();
	Ar << BrewTimeLeft << Fuel << Name;
	if (Ar.IsLoading())
	{
		if (BrewTimeLeft < 0 || BrewTimeLeft > BrewTime || Fuel < 0 || Fuel > FuelPerPowder)
		{
			return false;
		}
		Brewing = Name.IsEmpty() || Name == TEXT("None") ? NAME_None : FName(*Name);
	}
	return !Ar.IsError();
}

FMcBrewingMenu::FMcBrewingMenu(const TSharedPtr<FMcBrewingStand>& InStand, const TSharedPtr<FMcContainer>& PlayerInventory)
	: Stand(InStand)
{
	for (int32 Slot = 0; Slot < InStand->GetItems()->Num(); ++Slot)
	{
		AddSlot(InStand->GetItems(), Slot);
	}
	AddPlayerSlots(PlayerInventory);
}

bool FMcBrewingMenu::MayPlace(int32 Slot, const FMcItemStack& Stack) const
{
	if (Slot < FMcBrewingStand::IngredientSlot)
	{
		return FMcBrewingStand::IsBottle(Stack);
	}
	if (Slot == FMcBrewingStand::IngredientSlot)
	{
		FMcRecipeBook& Book = FMcRecipeBook::Get();
		return Book.EnsureLoaded() && Book.IsBrewingReagent(Stack);
	}
	if (Slot == FMcBrewingStand::FuelSlot)
	{
		return FMcBrewingStand::IsFuel(Stack);
	}
	return true;
}

FMcItemStack FMcBrewingMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	FMcItemStack Rest = Original;
	bool bMoved = false;
	if (Slot < PlayerStart)
	{
		bMoved = MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
	}
	else
	{
		// Fuel first (blaze powder is also an ingredient), then the ingredient, then the bottles.
		if (MayPlace(FMcBrewingStand::FuelSlot, Rest))
		{
			bMoved = MoveItemStackTo(Rest, FMcBrewingStand::FuelSlot, FMcBrewingStand::FuelSlot + 1, false);
		}
		if (!bMoved && MayPlace(FMcBrewingStand::IngredientSlot, Rest))
		{
			bMoved = MoveItemStackTo(Rest, FMcBrewingStand::IngredientSlot, FMcBrewingStand::IngredientSlot + 1, false);
		}
		if (!bMoved && FMcBrewingStand::IsBottle(Rest))
		{
			bMoved = MoveItemStackTo(Rest, FMcBrewingStand::FirstBottle, FMcBrewingStand::IngredientSlot, false);
		}
		if (!bMoved)
		{
			const int32 HotbarStart = PlayerStart + 27;
			bMoved = Slot >= HotbarStart ? MoveItemStackTo(Rest, PlayerStart, HotbarStart, false) : MoveItemStackTo(Rest, HotbarStart, NumSlots(), false);
		}
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
	return Original;
}
