#include "McEnchantMenu.h"
#include "McItemRegistry.h"

namespace
{
	const FName Lapis(TEXT("minecraft:lapis_lazuli"));
}

FMcEnchantMenu::FMcEnchantMenu(const TSharedPtr<FMcContainer>& PlayerInventory, int32 InBookshelves, int32 InSeed)
	: Table(MakeShared<FMcContainer>(2))
	, Player(PlayerInventory)
	, Bookshelves(FMath::Clamp(InBookshelves, 0, 15))
	, Seed(InSeed)
{
	AddSlot(Table, ItemSlot);
	AddSlot(Table, LapisSlot);
	AddPlayerSlots(PlayerInventory);
}

void FMcEnchantMenu::SetSeed(int32 InSeed)
{
	Seed = InSeed;
	OffersFor = NAME_None;
	Refresh();
}

void FMcEnchantMenu::Refresh()
{
	const FMcItemStack& Item = GetStack(ItemSlot);
	// An item already enchanted can't be enchanted again here.
	const bool bEnchantable = Item.Item == TEXT("minecraft:book") || (!Item.IsEmpty() && !McEnchanting::IsBook(Item) && !Item.IsEnchanted());
	const FName Now = bEnchantable ? Item.Item : NAME_None;
	if (Now == OffersFor && !Now.IsNone())
	{
		return;
	}
	OffersFor = Now;
	Offers = Now.IsNone() ? McEnchanting::FOffers() : McEnchanting::GetOffers(Now, Bookshelves, Seed);
}

void FMcEnchantMenu::SlotChanged(int32 Slot)
{
	if (Slot == ItemSlot)
	{
		Refresh();
	}
}

bool FMcEnchantMenu::CanEnchant(int32 Slot, int32 Levels, bool bCreative) const
{
	if (Slot < 0 || Slot > 2 || Offers.Costs[Slot] <= 0 || GetStack(ItemSlot).IsEmpty())
	{
		return false;
	}
	if (bCreative)
	{
		return true;
	}
	const FMcItemStack& Fuel = GetStack(LapisSlot);
	return Fuel.Item == Lapis && Fuel.Count >= Slot + 1 && Levels >= Offers.Costs[Slot];
}

bool FMcEnchantMenu::Enchant(int32 Slot, int32 Levels, bool bCreative, int32& OutLevelsSpent)
{
	OutLevelsSpent = 0;
	Refresh();
	if (!CanEnchant(Slot, Levels, bCreative))
	{
		return false;
	}
	FMcItemStack Item = GetStack(ItemSlot);
	const TArray<McEnchanting::FEnchant> Enchants = McEnchanting::GetTableEnchantments(Item.Item, Seed, Slot, Offers.Costs[Slot]);
	if (Enchants.IsEmpty())
	{
		return false;
	}
	for (const McEnchanting::FEnchant& Enchant : Enchants)
	{
		McEnchanting::AddEnchantment(Item, Enchant.Name, Enchant.Level);
	}
	SetStack(ItemSlot, Item);
	if (!bCreative)
	{
		FMcItemStack Fuel = GetStack(LapisSlot);
		Fuel.Count -= Slot + 1;
		SetStack(LapisSlot, Fuel.Count > 0 ? Fuel : FMcItemStack());
		OutLevelsSpent = Slot + 1;
	}
	OffersFor = NAME_None;
	Offers = McEnchanting::FOffers();
	return true;
}

bool FMcEnchantMenu::MayPlace(int32 Slot, const FMcItemStack& Stack) const
{
	if (Slot == LapisSlot)
	{
		return Stack.Item == Lapis;
	}
	return true;
}

FMcItemStack FMcEnchantMenu::QuickMoveStack(int32 Slot)
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
	else if (Original.Item == Lapis)
	{
		bMoved = MoveItemStackTo(Rest, LapisSlot, LapisSlot + 1, false);
	}
	else if (GetStack(ItemSlot).IsEmpty())
	{
		// One item goes on the table; the rest of the stack stays.
		FMcItemStack One = Original;
		One.Count = 1;
		SetStack(ItemSlot, One);
		Rest.Count -= 1;
		bMoved = true;
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
	SetStack(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
	return Original;
}

void FMcEnchantMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	for (int32 Index = 0; Index < Table->Num(); ++Index)
	{
		if (!Table->Get(Index).IsEmpty())
		{
			const FMcItemStack Rest = Player->AddItem(Table->Get(Index));
			if (!Rest.IsEmpty())
			{
				Dropped.Add(Rest);
			}
		}
	}
	Table->Clear();
	Refresh();
}
