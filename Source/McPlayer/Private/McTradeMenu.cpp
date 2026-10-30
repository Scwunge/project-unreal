#include "McTradeMenu.h"

FMcTradeMenu::FMcTradeMenu(const TSharedPtr<FMcContainer>& PlayerInventory)
	: Table(MakeShared<FMcContainer>(3))
	, Player(PlayerInventory)
{
	AddSlot(Table, WantsSlot);
	AddSlot(Table, ExtraSlot);
	AddSlot(Table, ResultSlot);
	AddPlayerSlots(PlayerInventory);
}

void FMcTradeMenu::SetTrader(const TArray<McTrading::FOffer>& InOffers, int32 InReputation, int32 InLevel, int32 InXp, bool bInLevels)
{
	Offers = InOffers;
	Reputation = InReputation;
	Level = InLevel;
	Xp = InXp;
	bLevels = bInLevels;
	Selected = -1;
	Refresh();
}

void FMcTradeMenu::UpdateTrader(const TArray<McTrading::FOffer>& InOffers, int32 InLevel, int32 InXp)
{
	Offers = InOffers;
	Level = InLevel;
	Xp = InXp;
	Refresh();
}

int32 FMcTradeMenu::GetCost(int32 Index) const
{
	return Offers.IsValidIndex(Index) ? McTrading::GetCost(Offers[Index], Reputation) : 0;
}

bool FMcTradeMenu::Pays(const FMcItemStack& Wanted, int32 Count, const FMcItemStack& Held) const
{
	return Wanted.IsEmpty() || (!Held.IsEmpty() && Held.IsSameItemSameComponents(Wanted) && Held.Count >= Count);
}

void FMcTradeMenu::Refresh()
{
	FMcItemStack Result;
	if (Offers.IsValidIndex(Selected) && !Offers[Selected].IsOutOfStock())
	{
		const McTrading::FOffer& Offer = Offers[Selected];
		if (Pays(Offer.Wants, GetCost(Selected), Table->Get(WantsSlot)) && Pays(Offer.Extra, Offer.Extra.Count, Table->Get(ExtraSlot)))
		{
			Result = Offer.Gives;
		}
	}
	Table->Set(ResultSlot, Result);
}

void FMcTradeMenu::ReturnInputs()
{
	for (const int32 Slot : { WantsSlot, ExtraSlot })
	{
		const FMcItemStack Held = Table->Get(Slot);
		if (!Held.IsEmpty())
		{
			// Back into the player's slots; what does not fit stays where it is.
			FMcItemStack Rest = Held;
			MoveItemStackTo(Rest, PlayerStart, NumSlots(), false);
			Table->Set(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
		}
	}
}

void FMcTradeMenu::Fill()
{
	ReturnInputs();
	if (!Offers.IsValidIndex(Selected) || Offers[Selected].IsOutOfStock())
	{
		Refresh();
		return;
	}
	const McTrading::FOffer& Offer = Offers[Selected];
	const auto Take = [&](int32 TableSlot, const FMcItemStack& Wanted, int32 Count)
	{
		if (Wanted.IsEmpty() || !Table->Get(TableSlot).IsEmpty())
		{
			return;
		}
		int32 Left = Count;
		FMcItemStack In = Wanted;
		In.Count = 0;
		for (int32 Slot = PlayerStart; Slot < NumSlots() && Left > 0; ++Slot)
		{
			const FMcItemStack& Have = GetStack(Slot);
			if (!Have.IsEmpty() && Have.IsSameItemSameComponents(Wanted))
			{
				const int32 Moved = FMath::Min(Have.Count, Left);
				FMcItemStack Rest = Have;
				Rest.Count -= Moved;
				SetStack(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
				In.Count += Moved;
				Left -= Moved;
			}
		}
		Table->Set(TableSlot, In.Count > 0 ? In : FMcItemStack());
	};
	Take(WantsSlot, Offer.Wants, GetCost(Selected));
	Take(ExtraSlot, Offer.Extra, Offer.Extra.Count);
	Refresh();
}

bool FMcTradeMenu::ClickButton(int32 Id)
{
	if (!Offers.IsValidIndex(Id))
	{
		return false;
	}
	Selected = Id;
	Fill();
	return true;
}

void FMcTradeMenu::SlotChanged(int32 Slot)
{
	if (Slot == WantsSlot || Slot == ExtraSlot)
	{
		Refresh();
	}
}

void FMcTradeMenu::OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped)
{
	if (Slot != ResultSlot || !Offers.IsValidIndex(Selected))
	{
		return;
	}
	McTrading::FOffer& Offer = Offers[Selected];
	const auto Use = [&](int32 TableSlot, int32 Count)
	{
		FMcItemStack Held = Table->Get(TableSlot);
		Held.Count -= Count;
		Table->Set(TableSlot, Held.Count > 0 ? Held : FMcItemStack());
	};
	if (!Offer.Wants.IsEmpty())
	{
		Use(WantsSlot, GetCost(Selected));
	}
	if (!Offer.Extra.IsEmpty())
	{
		Use(ExtraSlot, Offer.Extra.Count);
	}
	++Offer.Uses;
	// The player gets 3 to 6 points of experience from a trade; the trader earns what the trade says.
	PendingPlayerXp += 3 + FMath::RandRange(0, 3);
	if (OnTrade)
	{
		OnTrade(Selected, Offer.Xp);
	}
	else
	{
		Xp += Offer.Xp;
	}
	// The same trade again from the player's slots, if they can pay for it.
	Fill();
}

int32 FMcTradeMenu::TakeExperience()
{
	const int32 Earned = PendingPlayerXp;
	PendingPlayerXp = 0;
	return Earned;
}

FMcItemStack FMcTradeMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	if (Slot == ResultSlot)
	{
		// One trade, if it fits; shift-clicking again repeats it.
		if (!Player->CanAddItem(Original))
		{
			return FMcItemStack();
		}
		FMcItemStack Rest = Original;
		MoveItemStackTo(Rest, PlayerStart, NumSlots(), true);
		FMcItemStack Taken = Original;
		Table->Set(ResultSlot, FMcItemStack());
		TArray<FMcItemStack> Dropped;
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
		const McTrading::FOffer* Offer = Offers.IsValidIndex(Selected) ? &Offers[Selected] : nullptr;
		if (Offer && !Offer->Wants.IsEmpty() && Original.IsSameItemSameComponents(Offer->Wants))
		{
			bMoved = MoveItemStackTo(Rest, WantsSlot, WantsSlot + 1, false);
		}
		else if (Offer && !Offer->Extra.IsEmpty() && Original.IsSameItemSameComponents(Offer->Extra))
		{
			bMoved = MoveItemStackTo(Rest, ExtraSlot, ExtraSlot + 1, false);
		}
		else
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

void FMcTradeMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	Table->Set(ResultSlot, FMcItemStack());
	for (const int32 Slot : { WantsSlot, ExtraSlot })
	{
		if (!Table->Get(Slot).IsEmpty())
		{
			const FMcItemStack Rest = Player->AddItem(Table->Get(Slot));
			if (!Rest.IsEmpty())
			{
				Dropped.Add(Rest);
			}
			Table->Set(Slot, FMcItemStack());
		}
	}
}

void FMcTradeMenu::SerializeState(FArchive& Ar)
{
	int32 Count = Offers.Num();
	Ar << Count << Selected << Reputation << Level << Xp << bLevels;
	if (Ar.IsLoading())
	{
		if (Count < 0 || Count > 64)
		{
			Ar.SetError();
			return;
		}
		Offers.SetNum(Count);
	}
	for (McTrading::FOffer& Offer : Offers)
	{
		if (!Offer.Serialize(Ar))
		{
			Ar.SetError();
			return;
		}
	}
	if (Ar.IsLoading())
	{
		Refresh();
	}
}
