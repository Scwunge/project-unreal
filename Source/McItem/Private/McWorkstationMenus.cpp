#include "McWorkstationMenus.h"
#include "McItemRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	// The table's inputs go back to the player when the menu closes.
	void GiveBack(FMcContainer& Table, int32 Num, FMcContainer& Player, TArray<FMcItemStack>& Dropped)
	{
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (!Table.Get(Index).IsEmpty())
			{
				const FMcItemStack Rest = Player.AddItem(Table.Get(Index));
				if (!Rest.IsEmpty())
				{
					Dropped.Add(Rest);
				}
				Table.Set(Index, FMcItemStack());
			}
		}
	}

	void UseOne(FMcContainer& Table, int32 Index)
	{
		FMcItemStack& Stack = Table.GetMutable(Index);
		if (--Stack.Count <= 0)
		{
			Stack.Clear();
		}
	}
}

// ---- Stonecutter ----

FMcStonecutterMenu::FMcStonecutterMenu(const TSharedPtr<FMcContainer>& PlayerInventory)
	: Table(MakeShared<FMcContainer>(2))
	, Player(PlayerInventory)
{
	AddSlot(Table, InputSlot);
	AddSlot(Table, ResultSlot);
	AddPlayerSlots(PlayerInventory);
}

void FMcStonecutterMenu::Refresh()
{
	const FMcItemStack& Input = Table->Get(InputSlot);
	const FName Now = Input.IsEmpty() ? NAME_None : Input.Item;
	if (Now == RecipesFor)
	{
		SetupResult();
		return;
	}
	RecipesFor = Now;
	Recipes.Reset();
	Selected = -1;
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (!Now.IsNone() && Book.EnsureLoaded())
	{
		Book.FindStonecutting(Input, Recipes);
		// In a fixed order: by what they make, then by id.
		Recipes.Sort([](const FMcRecipe& A, const FMcRecipe& B)
		{
			return A.Result.Item != B.Result.Item ? A.Result.Item.LexicalLess(B.Result.Item) : A.Id.LexicalLess(B.Id);
		});
	}
	SetupResult();
}

void FMcStonecutterMenu::SetupResult()
{
	const bool bValid = Recipes.IsValidIndex(Selected) && !Table->Get(InputSlot).IsEmpty();
	Table->Set(ResultSlot, bValid ? Recipes[Selected]->Result : FMcItemStack());
}

bool FMcStonecutterMenu::ClickButton(int32 Id)
{
	Refresh();
	if (!Recipes.IsValidIndex(Id))
	{
		return false;
	}
	Selected = Id;
	SetupResult();
	return true;
}

void FMcStonecutterMenu::SerializeState(FArchive& Ar)
{
	int32 Chosen = Selected;
	Ar << Chosen;
	if (Ar.IsLoading())
	{
		RecipesFor = NAME_None;
		Refresh();
		Selected = Recipes.IsValidIndex(Chosen) ? Chosen : -1;
		SetupResult();
	}
}

void FMcStonecutterMenu::SlotChanged(int32 Slot)
{
	if (Slot == InputSlot)
	{
		Refresh();
	}
}

void FMcStonecutterMenu::OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped)
{
	if (Slot != ResultSlot)
	{
		return;
	}
	UseOne(*Table, InputSlot);
	Refresh();
}

FMcItemStack FMcStonecutterMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	if (Slot == ResultSlot)
	{
		// One cut, if it fits in the inventory; shift-clicking again repeats it.
		if (!Player->CanAddItem(Original))
		{
			return FMcItemStack();
		}
		FMcItemStack Rest = Original;
		MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
		TArray<FMcItemStack> Dropped;
		FMcItemStack Taken = Original;
		Table->Set(ResultSlot, FMcItemStack());
		OnTake(ResultSlot, Taken, Dropped);
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
		TArray<const FMcRecipe*> Cuts;
		FMcRecipeBook& Book = FMcRecipeBook::Get();
		if (Book.EnsureLoaded())
		{
			Book.FindStonecutting(Original, Cuts);
		}
		const int32 HotbarStart = PlayerStart + 27;
		bMoved = !Cuts.IsEmpty() ? MoveItemStackTo(Rest, InputSlot, InputSlot + 1, false)
			: Slot >= HotbarStart ? MoveItemStackTo(Rest, PlayerStart, HotbarStart, false) : MoveItemStackTo(Rest, HotbarStart, NumSlots(), false);
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
	return Original;
}

void FMcStonecutterMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	Table->Set(ResultSlot, FMcItemStack());
	GiveBack(*Table, 1, *Player, Dropped);
	Refresh();
}

// ---- Grindstone ----

namespace
{
	const FName StoredEnchantments(TEXT("minecraft:stored_enchantments"));
	const FName EnchantedBook(TEXT("minecraft:enchanted_book"));

	bool IsCurse(FName Enchantment)
	{
		const TSet<FName>* Curses = FMcItemRegistry::Get().GetEnchantmentTag(TEXT("minecraft:curse"));
		return Curses && Curses->Contains(Enchantment);
	}

	// An enchanted book's stored enchantments, or any other item's.
	TMap<FName, int32> EnchantmentsOf(const FMcItemStack& Stack)
	{
		if (Stack.Item != EnchantedBook)
		{
			return Stack.GetEnchantments();
		}
		TMap<FName, int32> Result;
		const TSharedPtr<FJsonValue> Value = Stack.Get(StoredEnchantments);
		if (Value.IsValid() && Value->Type == EJson::Object)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Value->AsObject()->Values)
			{
				const FString Name = Entry.Key.Contains(TEXT(":")) ? Entry.Key : TEXT("minecraft:") + Entry.Key;
				Result.Add(FName(*Name), static_cast<int32>(Entry.Value->AsNumber()));
			}
		}
		return Result;
	}

	bool HasNonCurse(const FMcItemStack& Stack)
	{
		for (const TPair<FName, int32>& Entry : EnchantmentsOf(Stack))
		{
			if (!IsCurse(Entry.Key))
			{
				return true;
			}
		}
		return false;
	}

	// The stack with only its curses left; an enchanted book left with none becomes a book.
	FMcItemStack KeepCurses(const FMcItemStack& Stack, const TMap<FName, int32>& Curses)
	{
		FMcItemStack Out = Stack;
		Out.Count = 1;
		if (Stack.Item == EnchantedBook)
		{
			if (Curses.IsEmpty())
			{
				Out = FMcItemStack(TEXT("minecraft:book"));
				return Out;
			}
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			for (const TPair<FName, int32>& Entry : Curses)
			{
				Object->SetNumberField(Entry.Key.ToString(), Entry.Value);
			}
			Out.Set(StoredEnchantments, MakeShared<FJsonValueObject>(Object));
			return Out;
		}
		Out.SetEnchantments(Curses);
		// What anvils added to the cost goes too.
		Out.Reset(TEXT("minecraft:repair_cost"));
		Out.Normalize();
		return Out;
	}

	TMap<FName, int32> CursesOf(const FMcItemStack& Stack)
	{
		TMap<FName, int32> Curses;
		for (const TPair<FName, int32>& Entry : EnchantmentsOf(Stack))
		{
			if (IsCurse(Entry.Key))
			{
				Curses.Add(Entry.Key, Entry.Value);
			}
		}
		return Curses;
	}
}

FMcGrindstoneMenu::FMcGrindstoneMenu(const TSharedPtr<FMcContainer>& PlayerInventory)
	: Table(MakeShared<FMcContainer>(3))
	, Player(PlayerInventory)
{
	AddSlot(Table, TopSlot);
	AddSlot(Table, BottomSlot);
	AddSlot(Table, ResultSlot);
	AddPlayerSlots(PlayerInventory);
}

FMcItemStack FMcGrindstoneMenu::GetResult(const FMcItemStack& Top, const FMcItemStack& Bottom)
{
	if (Top.IsEmpty() && Bottom.IsEmpty())
	{
		return FMcItemStack();
	}
	if (Top.IsEmpty() || Bottom.IsEmpty())
	{
		// One item: it must have something to take off.
		const FMcItemStack& Only = Top.IsEmpty() ? Bottom : Top;
		return HasNonCurse(Only) ? KeepCurses(Only, CursesOf(Only)) : FMcItemStack();
	}
	// Two: the same damageable item (an enchanted book can't be joined).
	if (Top.Item != Bottom.Item || !Top.IsDamageable() || Top.Count != 1 || Bottom.Count != 1)
	{
		return FMcItemStack();
	}
	TMap<FName, int32> Curses = CursesOf(Top);
	for (const TPair<FName, int32>& Entry : CursesOf(Bottom))
	{
		int32& Level = Curses.FindOrAdd(Entry.Key);
		Level = FMath::Max(Level, Entry.Value);
	}
	const int32 Max = Top.GetMaxDamage();
	const int32 Left = (Max - Top.GetDamage()) + (Max - Bottom.GetDamage()) + Max * 5 / 100;
	FMcItemStack Out = KeepCurses(Top, Curses);
	Out.SetDamage(FMath::Max(0, Max - Left));
	Out.Normalize();
	return Out;
}

int32 FMcGrindstoneMenu::GetExperienceValue(const FMcItemStack& Top, const FMcItemStack& Bottom)
{
	int32 Value = 0;
	for (const FMcItemStack* Stack : { &Top, &Bottom })
	{
		for (const TPair<FName, int32>& Entry : EnchantmentsOf(*Stack))
		{
			const FMcEnchantmentDef* Def = FMcItemRegistry::Get().FindEnchantment(Entry.Key);
			if (Def && !IsCurse(Entry.Key))
			{
				Value += Def->MinCost(Entry.Value);
			}
		}
	}
	return Value;
}

void FMcGrindstoneMenu::Refresh()
{
	Table->Set(ResultSlot, GetResult(Table->Get(TopSlot), Table->Get(BottomSlot)));
}

bool FMcGrindstoneMenu::MayPlace(int32 Slot, const FMcItemStack& Stack) const
{
	// Only what can be damaged or is enchanted goes in.
	return Slot != ResultSlot && (Stack.IsDamageable() || !EnchantmentsOf(Stack).IsEmpty());
}

void FMcGrindstoneMenu::SlotChanged(int32 Slot)
{
	if (Slot == TopSlot || Slot == BottomSlot)
	{
		Refresh();
	}
}

void FMcGrindstoneMenu::OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped)
{
	if (Slot != ResultSlot)
	{
		return;
	}
	// Between half the value (rounded up) and all of it.
	const int32 Value = GetExperienceValue(Table->Get(TopSlot), Table->Get(BottomSlot));
	const int32 Half = (Value + 1) / 2;
	const float Roll = Random ? Random() : FMath::FRand();
	Earned += Half + FMath::Min(Half, FMath::FloorToInt32(Roll * Half));
	Table->Set(TopSlot, FMcItemStack());
	Table->Set(BottomSlot, FMcItemStack());
	Refresh();
}

int32 FMcGrindstoneMenu::TakeExperience()
{
	const int32 Out = Earned;
	Earned = 0;
	return Out;
}

FMcItemStack FMcGrindstoneMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	if (Slot == ResultSlot)
	{
		if (!Player->CanAddItem(Original))
		{
			return FMcItemStack();
		}
		FMcItemStack Rest = Original;
		MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
		TArray<FMcItemStack> Dropped;
		FMcItemStack Taken = Original;
		OnTake(ResultSlot, Taken, Dropped);
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
		const int32 HotbarStart = PlayerStart + 27;
		bMoved = MoveItemStackTo(Rest, TopSlot, ResultSlot, false)
			|| (Slot >= HotbarStart ? MoveItemStackTo(Rest, PlayerStart, HotbarStart, false) : MoveItemStackTo(Rest, HotbarStart, NumSlots(), false));
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
	return Original;
}

void FMcGrindstoneMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	Table->Set(ResultSlot, FMcItemStack());
	GiveBack(*Table, 2, *Player, Dropped);
	Refresh();
}

// ---- Smithing table ----

FMcSmithingMenu::FMcSmithingMenu(const TSharedPtr<FMcContainer>& PlayerInventory)
	: Table(MakeShared<FMcContainer>(4))
	, Player(PlayerInventory)
{
	AddSlot(Table, TemplateSlot);
	AddSlot(Table, BaseSlot);
	AddSlot(Table, AdditionSlot);
	AddSlot(Table, ResultSlot);
	AddPlayerSlots(PlayerInventory);
}

void FMcSmithingMenu::Refresh()
{
	Match = FMcCraftingMatch();
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (!Book.EnsureLoaded() || !Book.FindSmithing(Table->Get(TemplateSlot), Table->Get(BaseSlot), Table->Get(AdditionSlot), Match))
	{
		Match = FMcCraftingMatch();
	}
	Table->Set(ResultSlot, Match.Result);
}

int32 FMcSmithingMenu::FindInputSlot(const FMcItemStack& Stack)
{
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (Stack.IsEmpty() || !Book.EnsureLoaded())
	{
		return -1;
	}
	for (int32 Slot = TemplateSlot; Slot <= AdditionSlot; ++Slot)
	{
		for (const FMcRecipe& Recipe : Book.GetAll())
		{
			if (Recipe.Type != EMcRecipeType::SmithingTransform && Recipe.Type != EMcRecipeType::SmithingTrim)
			{
				continue;
			}
			const FMcIngredient& Part = Slot == TemplateSlot ? Recipe.Template : Slot == BaseSlot ? Recipe.Base : Recipe.Addition;
			if (Part.Test(Stack))
			{
				return Slot;
			}
		}
	}
	return -1;
}

void FMcSmithingMenu::SlotChanged(int32 Slot)
{
	if (Slot >= TemplateSlot && Slot <= AdditionSlot)
	{
		Refresh();
	}
}

void FMcSmithingMenu::OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped)
{
	if (Slot != ResultSlot || !Match.Recipe)
	{
		return;
	}
	UseOne(*Table, TemplateSlot);
	UseOne(*Table, BaseSlot);
	UseOne(*Table, AdditionSlot);
	Refresh();
}

FMcItemStack FMcSmithingMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	if (Slot == ResultSlot)
	{
		if (!Player->CanAddItem(Original))
		{
			return FMcItemStack();
		}
		FMcItemStack Rest = Original;
		MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
		TArray<FMcItemStack> Dropped;
		FMcItemStack Taken = Original;
		Table->Set(ResultSlot, FMcItemStack());
		OnTake(ResultSlot, Taken, Dropped);
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
		const int32 Input = FindInputSlot(Original);
		const int32 HotbarStart = PlayerStart + 27;
		bMoved = Input >= 0 ? MoveItemStackTo(Rest, Input, Input + 1, false)
			: Slot >= HotbarStart ? MoveItemStackTo(Rest, PlayerStart, HotbarStart, false) : MoveItemStackTo(Rest, HotbarStart, NumSlots(), false);
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
	return Original;
}

void FMcSmithingMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	Table->Set(ResultSlot, FMcItemStack());
	GiveBack(*Table, 3, *Player, Dropped);
	Refresh();
}
