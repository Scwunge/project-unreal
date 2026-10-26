#include "McFurnace.h"
#include "Serialization/Archive.h"

namespace
{
	constexpr int32 MaxRecipesUsed = 1024;
}

FMcFurnace::FMcFurnace(EMcFurnaceKind InKind)
	: Kind(InKind)
	, Items(MakeShared<FMcContainer>(3))
{
}

EMcRecipeType FMcFurnace::GetRecipeType() const
{
	switch (Kind)
	{
	case EMcFurnaceKind::BlastFurnace:
		return EMcRecipeType::Blasting;
	case EMcFurnaceKind::Smoker:
		return EMcRecipeType::Smoking;
	default:
		return EMcRecipeType::Smelting;
	}
}

bool FMcFurnace::IsFuel(const FMcItemStack& Stack) const
{
	return !Stack.IsEmpty() && FMcItemRegistry::Get().GetBurnTime(Stack.Item, Kind != EMcFurnaceKind::Furnace) > 0;
}

bool FMcFurnace::CanSmelt(const FMcItemStack& Stack) const
{
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	return !Stack.IsEmpty() && Book.EnsureLoaded() && Book.FindCooking(GetRecipeType(), Stack) != nullptr;
}

bool FMcFurnace::CanBurn(const FMcRecipe* Recipe) const
{
	if (!Recipe || Items->Get(InputSlot).IsEmpty() || Recipe->Result.IsEmpty())
	{
		return false;
	}
	const FMcItemStack& Output = Items->Get(OutputSlot);
	return Output.IsEmpty() || Output.GetSpaceFor(Recipe->Result) >= Recipe->Result.Count;
}

void FMcFurnace::Burn(const FMcRecipe* Recipe)
{
	FMcItemStack& Input = Items->GetMutable(InputSlot);
	FMcItemStack& Fuel = Items->GetMutable(FuelSlot);
	FMcItemStack& Output = Items->GetMutable(OutputSlot);
	// A wet sponge dries out and fills the empty bucket that was in the fuel slot.
	if (Input.Item == TEXT("minecraft:wet_sponge") && Fuel.Item == TEXT("minecraft:bucket") && Fuel.Count > 0)
	{
		Fuel = FMcItemStack(TEXT("minecraft:water_bucket"), 1);
	}
	if (Output.IsEmpty())
	{
		Output = Recipe->Result;
	}
	else
	{
		Output.Count += Recipe->Result.Count;
	}
	RecipesUsed.FindOrAdd(Recipe->Id) += 1;
	Input.Count -= 1;
	if (Input.Count <= 0)
	{
		Input.Clear();
	}
}

bool FMcFurnace::Tick()
{
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (!Book.EnsureLoaded())
	{
		return false;
	}
	const FMcItemRegistry& Registry = FMcItemRegistry::Get();
	const bool bFast = Kind != EMcFurnaceKind::Furnace;
	bool bChanged = false;
	if (IsLit())
	{
		--LitTimeRemaining;
	}
	FMcItemStack& Input = Items->GetMutable(InputSlot);
	FMcItemStack& Fuel = Items->GetMutable(FuelSlot);
	const bool bHasInput = !Input.IsEmpty();
	const bool bHasFuel = !Fuel.IsEmpty();
	if (IsLit() || (bHasFuel && bHasInput))
	{
		const FMcRecipe* Recipe = bHasInput ? Book.FindCooking(GetRecipeType(), Input) : nullptr;
		if (!IsLit() && CanBurn(Recipe))
		{
			const int32 BurnTime = bHasFuel ? Registry.GetBurnTime(Fuel.Item, bFast) : 0;
			if (BurnTime > 0)
			{
				LitTimeRemaining = BurnTime;
				LitTotalTime = BurnTime;
				LitSpeed = Registry.GetFuelSpeed(Fuel.Item, bFast);
				const FName Remainder = Registry.GetUseRemainder(Fuel.Item);
				Fuel.Count -= 1;
				if (Fuel.Count <= 0)
				{
					Fuel = Remainder.IsNone() ? FMcItemStack() : FMcItemStack(Remainder, 1);
				}
				bChanged = true;
			}
		}
		if (IsLit() && CanBurn(Recipe))
		{
			CookingTotalTime = Recipe->CookingTime;
			CookingProgress += LitSpeed;
			if (CookingProgress >= CookingTotalTime)
			{
				CookingProgress = 0.0f;
				Burn(Recipe);
				bChanged = true;
			}
		}
		else
		{
			CookingProgress = 0.0f;
		}
	}
	else if (CookingProgress > 0.0f)
	{
		// It cools off, twice as fast as it heated.
		CookingProgress = FMath::Clamp(CookingProgress - 2.0f, 0.0f, static_cast<float>(CookingTotalTime));
	}
	return bChanged;
}

int32 FMcFurnace::TakeExperience(float Roll)
{
	const FMcRecipeBook& Book = FMcRecipeBook::Get();
	int32 Total = 0;
	for (const TPair<FName, int32>& Entry : RecipesUsed)
	{
		const FMcRecipe* Recipe = Book.Find(Entry.Key);
		if (!Recipe)
		{
			continue;
		}
		const float Amount = Recipe->Experience * Entry.Value;
		const int32 Whole = FMath::FloorToInt32(Amount);
		Total += Whole + (Roll < Amount - Whole ? 1 : 0);
	}
	RecipesUsed.Reset();
	return Total;
}

bool FMcFurnace::Serialize(FArchive& Ar)
{
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		if (!Items->GetMutable(Slot).Serialize(Ar))
		{
			return false;
		}
	}
	Ar << LitTimeRemaining << LitTotalTime << LitSpeed << CookingProgress << CookingTotalTime;
	int32 Num = RecipesUsed.Num();
	Ar << Num;
	if (Ar.IsSaving())
	{
		for (TPair<FName, int32>& Entry : RecipesUsed)
		{
			FString Name = Entry.Key.ToString();
			Ar << Name << Entry.Value;
		}
		return !Ar.IsError();
	}
	if (Num < 0 || Num > MaxRecipesUsed)
	{
		return false;
	}
	RecipesUsed.Reset();
	for (int32 Index = 0; Index < Num; ++Index)
	{
		FString Name;
		int32 Count = 0;
		Ar << Name << Count;
		RecipesUsed.Add(FName(*Name), Count);
	}
	return !Ar.IsError();
}

// ---- FMcFurnaceMenu ----

FMcFurnaceMenu::FMcFurnaceMenu(const TSharedPtr<FMcFurnace>& InFurnace, const TSharedPtr<FMcContainer>& PlayerInventory)
	: Furnace(InFurnace)
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AddSlot(Furnace->GetItems(), Index);
	}
	AddPlayerSlots(PlayerInventory);
}

bool FMcFurnaceMenu::MayPlace(int32 Slot, const FMcItemStack& Stack) const
{
	if (Slot == FMcFurnace::OutputSlot)
	{
		return false;
	}
	if (Slot == FMcFurnace::FuelSlot)
	{
		// A bucket can go in as fuel: it is what a wet sponge fills.
		return Furnace->IsFuel(Stack) || Stack.Item == TEXT("minecraft:bucket");
	}
	return true;
}

FMcItemStack FMcFurnaceMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	FMcItemStack Rest = Original;
	bool bMoved;
	if (Slot < PlayerStart)
	{
		bMoved = MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
	}
	else if (Furnace->CanSmelt(Original))
	{
		bMoved = MoveItemStackTo(Rest, FMcFurnace::InputSlot, FMcFurnace::InputSlot + 1, false);
	}
	else if (Furnace->IsFuel(Original))
	{
		bMoved = MoveItemStackTo(Rest, FMcFurnace::FuelSlot, FMcFurnace::FuelSlot + 1, false);
	}
	else
	{
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
