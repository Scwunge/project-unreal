#include "McWorkstationMenus.h"
#include "McItemRegistry.h"
#include "McMapStore.h"
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
			for (const auto& Entry : Value->AsObject()->Values)
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

// ---- Loom ----

namespace
{
	bool IsBanner(const FMcItemStack& Stack)
	{
		const FString Name = Stack.Item.ToString();
		return Name.EndsWith(TEXT("_banner")) && !Name.EndsWith(TEXT("_wall_banner"));
	}

	bool IsDye(const FMcItemStack& Stack)
	{
		return Stack.Item.ToString().EndsWith(TEXT("_dye"));
	}

	bool IsPatternItem(const FMcItemStack& Stack)
	{
		return Stack.Item.ToString().EndsWith(TEXT("_banner_pattern"));
	}

	TArray<TSharedPtr<FJsonValue>> LayersOf(const FMcItemStack& Banner)
	{
		const TSharedPtr<FJsonValue> Layers = Banner.Get(TEXT("minecraft:banner_patterns"));
		return Layers.IsValid() && Layers->Type == EJson::Array ? Layers->AsArray() : TArray<TSharedPtr<FJsonValue>>();
	}
}

const TArray<FName>& FMcLoomMenu::GetFreePatterns()
{
	static const TArray<FName> Free = {
		TEXT("minecraft:square_bottom_left"), TEXT("minecraft:square_bottom_right"), TEXT("minecraft:square_top_left"), TEXT("minecraft:square_top_right"),
		TEXT("minecraft:stripe_bottom"), TEXT("minecraft:stripe_top"), TEXT("minecraft:stripe_left"), TEXT("minecraft:stripe_right"),
		TEXT("minecraft:stripe_center"), TEXT("minecraft:stripe_middle"), TEXT("minecraft:stripe_downright"), TEXT("minecraft:stripe_downleft"),
		TEXT("minecraft:small_stripes"), TEXT("minecraft:cross"), TEXT("minecraft:straight_cross"), TEXT("minecraft:triangle_bottom"),
		TEXT("minecraft:triangle_top"), TEXT("minecraft:triangles_bottom"), TEXT("minecraft:triangles_top"), TEXT("minecraft:diagonal_left"),
		TEXT("minecraft:diagonal_up_right"), TEXT("minecraft:diagonal_up_left"), TEXT("minecraft:diagonal_right"), TEXT("minecraft:circle"),
		TEXT("minecraft:rhombus"), TEXT("minecraft:half_vertical"), TEXT("minecraft:half_horizontal"), TEXT("minecraft:half_vertical_right"),
		TEXT("minecraft:half_horizontal_bottom"), TEXT("minecraft:border"), TEXT("minecraft:gradient"), TEXT("minecraft:gradient_up"),
	};
	return Free;
}

TArray<FName> FMcLoomMenu::GetPatternsOfItem(FName Item)
{
	// Each pattern item stands for one pattern (the data's `pattern_item` tags).
	static const TMap<FName, FName> Of = {
		{ TEXT("minecraft:flower_banner_pattern"), TEXT("minecraft:flower") },
		{ TEXT("minecraft:creeper_banner_pattern"), TEXT("minecraft:creeper") },
		{ TEXT("minecraft:skull_banner_pattern"), TEXT("minecraft:skull") },
		{ TEXT("minecraft:mojang_banner_pattern"), TEXT("minecraft:mojang") },
		{ TEXT("minecraft:globe_banner_pattern"), TEXT("minecraft:globe") },
		{ TEXT("minecraft:piglin_banner_pattern"), TEXT("minecraft:piglin") },
		{ TEXT("minecraft:flow_banner_pattern"), TEXT("minecraft:flow") },
		{ TEXT("minecraft:guster_banner_pattern"), TEXT("minecraft:guster") },
		{ TEXT("minecraft:field_masoned_banner_pattern"), TEXT("minecraft:bricks") },
		{ TEXT("minecraft:bordure_indented_banner_pattern"), TEXT("minecraft:curly_border") },
	};
	const FName* Found = Of.Find(Item);
	return Found ? TArray<FName>{ *Found } : TArray<FName>();
}

FMcLoomMenu::FMcLoomMenu(const TSharedPtr<FMcContainer>& PlayerInventory)
	: Table(MakeShared<FMcContainer>(4))
	, Player(PlayerInventory)
{
	AddSlot(Table, BannerSlot);
	AddSlot(Table, DyeSlot);
	AddSlot(Table, PatternSlot);
	AddSlot(Table, ResultSlot);
	AddPlayerSlots(PlayerInventory);
}

bool FMcLoomMenu::MayPlace(int32 Slot, const FMcItemStack& Stack) const
{
	switch (Slot)
	{
	case BannerSlot: return IsBanner(Stack);
	case DyeSlot: return IsDye(Stack);
	case PatternSlot: return IsPatternItem(Stack);
	case ResultSlot: return false;
	default: return true;
	}
}

void FMcLoomMenu::Refresh()
{
	const FMcItemStack& Banner = Table->Get(BannerSlot);
	const FMcItemStack& Dye = Table->Get(DyeSlot);
	const FMcItemStack& Item = Table->Get(PatternSlot);
	const FName Chosen = Patterns.IsValidIndex(Selected) ? Patterns[Selected] : NAME_None;
	Patterns.Reset();
	if (!Banner.IsEmpty() && !Dye.IsEmpty() && LayersOf(Banner).Num() < MaxPatterns)
	{
		Patterns = Item.IsEmpty() ? GetFreePatterns() : GetPatternsOfItem(Item.Item);
	}
	// Still the same pattern, though it may have moved in the list; else none is chosen.
	Selected = Chosen.IsNone() ? -1 : Patterns.IndexOfByKey(Chosen);
	FMcItemStack Result;
	if (Patterns.IsValidIndex(Selected))
	{
		Result = Banner;
		Result.Count = 1;
		TArray<TSharedPtr<FJsonValue>> Layers = LayersOf(Banner);
		const TSharedRef<FJsonObject> Layer = MakeShared<FJsonObject>();
		Layer->SetStringField(TEXT("pattern"), Patterns[Selected].ToString());
		FString Colour = Dye.Item.ToString();
		Colour.RemoveFromStart(TEXT("minecraft:"));
		Colour.RemoveFromEnd(TEXT("_dye"));
		Layer->SetStringField(TEXT("color"), Colour);
		Layers.Add(MakeShared<FJsonValueObject>(Layer));
		Result.Set(TEXT("minecraft:banner_patterns"), MakeShared<FJsonValueArray>(Layers));
	}
	Table->Set(ResultSlot, Result);
}

bool FMcLoomMenu::ClickButton(int32 Id)
{
	Refresh();
	if (!Patterns.IsValidIndex(Id))
	{
		return false;
	}
	Selected = Id;
	Refresh();
	return true;
}

void FMcLoomMenu::SerializeState(FArchive& Ar)
{
	int32 Chosen = Selected;
	Ar << Chosen;
	if (Ar.IsLoading())
	{
		// The offers follow from the slots, which are synced first; the choice is read back after them.
		Selected = -1;
		Refresh();
		Selected = Patterns.IsValidIndex(Chosen) ? Chosen : -1;
		Refresh();
	}
}

void FMcLoomMenu::SlotChanged(int32 Slot)
{
	if (Slot != ResultSlot)
	{
		Refresh();
	}
}

void FMcLoomMenu::OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped)
{
	if (Slot != ResultSlot)
	{
		return;
	}
	UseOne(*Table, BannerSlot);
	UseOne(*Table, DyeSlot);
	Refresh();
}

FMcItemStack FMcLoomMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	FMcItemStack Rest = Original;
	bool bMoved;
	if (Slot == ResultSlot)
	{
		// One banner, if it fits in the inventory; shift-clicking again repeats it.
		if (!Player->CanAddItem(Original))
		{
			return FMcItemStack();
		}
		MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
		TArray<FMcItemStack> Dropped;
		FMcItemStack Taken = Original;
		Table->Set(ResultSlot, FMcItemStack());
		OnTake(ResultSlot, Taken, Dropped);
		return Original;
	}
	if (Slot < PlayerStart)
	{
		bMoved = MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
	}
	else
	{
		const int32 Target = IsBanner(Original) ? BannerSlot : IsDye(Original) ? DyeSlot : IsPatternItem(Original) ? PatternSlot : -1;
		const int32 HotbarStart = PlayerStart + 27;
		bMoved = Target >= 0 ? MoveItemStackTo(Rest, Target, Target + 1, false)
			: Slot >= HotbarStart ? MoveItemStackTo(Rest, PlayerStart, HotbarStart, false) : MoveItemStackTo(Rest, HotbarStart, NumSlots(), false);
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
	return Original;
}

void FMcLoomMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	Table->Set(ResultSlot, FMcItemStack());
	GiveBack(*Table, 3, *Player, Dropped);
	Refresh();
}

// ---- Cartography table ----

FMcCartographyMenu::FMcCartographyMenu(const TSharedPtr<FMcContainer>& PlayerInventory)
	: Table(MakeShared<FMcContainer>(3))
	, Player(PlayerInventory)
{
	AddSlot(Table, MapSlot);
	AddSlot(Table, MaterialSlot);
	AddSlot(Table, ResultSlot);
	AddPlayerSlots(PlayerInventory);
}

FMcCartographyMenu::EOperation FMcCartographyMenu::GetOperation(const FMcItemStack& Map, const FMcItemStack& Material)
{
	if (Map.Item != FName(TEXT("minecraft:filled_map")) || Map.IsEmpty() || Material.IsEmpty())
	{
		return EOperation::None;
	}
	const FMcMapData* Data = McMapStore::Find(McMapStore::GetId(Map));
	if (!Data)
	{
		return EOperation::None;
	}
	if (Material.Item == FName(TEXT("minecraft:paper")))
	{
		return McMapStore::CanZoom(Map) ? EOperation::Zoom : EOperation::None;
	}
	if (Material.Item == FName(TEXT("minecraft:map")))
	{
		return EOperation::Copy;
	}
	if (Material.Item == FName(TEXT("minecraft:glass_pane")))
	{
		return Data->bLocked ? EOperation::None : EOperation::Lock;
	}
	return EOperation::None;
}

FMcItemStack FMcCartographyMenu::GetPreview(const FMcItemStack& Map, const FMcItemStack& Material)
{
	FMcItemStack Out = Map;
	Out.Count = 1;
	switch (GetOperation(Map, Material))
	{
	case EOperation::Zoom:
		McMapStore::SetId(Out, McMapStore::Zoom(McMapStore::GetId(Map)));
		return Out;
	case EOperation::Copy:
		Out.Count = 2;
		return Out;
	case EOperation::Lock:
		return Out;
	default:
		return FMcItemStack();
	}
}

FMcItemStack FMcCartographyMenu::MakeResult() const
{
	const FMcItemStack& Map = Table->Get(MapSlot);
	FMcItemStack Out = GetPreview(Map, Table->Get(MaterialSlot));
	if (!Out.IsEmpty() && GetCurrentOperation() == EOperation::Lock)
	{
		McMapStore::SetId(Out, McMapStore::LockedCopy(McMapStore::GetId(Map)));
	}
	return Out;
}

void FMcCartographyMenu::Consume()
{
	for (const int32 Slot : { MapSlot, MaterialSlot })
	{
		FMcItemStack Used = Table->Get(Slot);
		Used.Count -= 1;
		Table->Set(Slot, Used.Count > 0 ? Used : FMcItemStack());
	}
	Refresh();
}

void FMcCartographyMenu::Refresh()
{
	Table->Set(ResultSlot, GetPreview(Table->Get(MapSlot), Table->Get(MaterialSlot)));
}

bool FMcCartographyMenu::MayPlace(int32 Slot, const FMcItemStack& Stack) const
{
	if (Slot == MapSlot)
	{
		return Stack.Item == FName(TEXT("minecraft:filled_map"));
	}
	if (Slot >= PlayerStart)
	{
		return true;
	}
	return Slot == MaterialSlot && (Stack.Item == FName(TEXT("minecraft:paper")) || Stack.Item == FName(TEXT("minecraft:map")) || Stack.Item == FName(TEXT("minecraft:glass_pane")));
}

void FMcCartographyMenu::SlotChanged(int32 Slot)
{
	if (Slot == MapSlot || Slot == MaterialSlot)
	{
		Refresh();
	}
}

void FMcCartographyMenu::OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped)
{
	if (Slot != ResultSlot)
	{
		return;
	}
	// Whatever part of the stack the click split off, the whole result comes (two maps for a copy).
	Taken = MakeResult();
	Consume();
}

FMcItemStack FMcCartographyMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	if (Slot == ResultSlot)
	{
		FMcItemStack Made = MakeResult();
		if (Made.IsEmpty() || !Player->CanAddItem(Made))
		{
			return FMcItemStack();
		}
		FMcItemStack Rest = Made;
		MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
		Consume();
		return Made;
	}
	FMcItemStack Rest = Original;
	bool bMoved;
	if (Slot < PlayerStart)
	{
		bMoved = MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
	}
	else
	{
		const int32 Into = MayPlace(MapSlot, Original) ? MapSlot : MaterialSlot;
		const int32 HotbarStart = PlayerStart + 27;
		bMoved = MayPlace(Into, Original) && MoveItemStackTo(Rest, Into, Into + 1, false);
		if (!bMoved)
		{
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

void FMcCartographyMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	Table->Set(ResultSlot, FMcItemStack());
	GiveBack(*Table, 2, *Player, Dropped);
	Refresh();
}
