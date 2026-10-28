#include "McAnvilMenu.h"
#include "McItemRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const FName EnchantedBook(TEXT("minecraft:enchanted_book"));
	const FName StoredEnchantments(TEXT("minecraft:stored_enchantments"));
	const FName RepairCost(TEXT("minecraft:repair_cost"));
	const FName Repairable(TEXT("minecraft:repairable"));

	int32 GetRepairCost(const FMcItemStack& Stack)
	{
		const TSharedPtr<FJsonValue> Value = Stack.Get(RepairCost);
		return Value.IsValid() && Value->Type == EJson::Number ? static_cast<int32>(Value->AsNumber()) : 0;
	}

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

	void SetEnchantmentsOf(FMcItemStack& Stack, const TMap<FName, int32>& Enchantments)
	{
		if (Stack.Item != EnchantedBook)
		{
			Stack.SetEnchantments(Enchantments);
			return;
		}
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		for (const TPair<FName, int32>& Entry : Enchantments)
		{
			Object->SetNumberField(Entry.Key.ToString(), Entry.Value);
		}
		Stack.Set(StoredEnchantments, MakeShared<FJsonValueObject>(Object));
	}

	// Whether Material mends Item (its repairable component names it).
	bool IsRepairMaterial(const FMcItemStack& Item, const FMcItemStack& Material)
	{
		const TSharedPtr<FJsonValue> Value = Item.Get(Repairable);
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			return false;
		}
		const TSharedPtr<FJsonValue> Items = Value->AsObject()->TryGetField(TEXT("items"));
		const FMcItemRegistry& Registry = FMcItemRegistry::Get();
		if (!Items.IsValid())
		{
			return false;
		}
		if (Items->Type == EJson::String)
		{
			return Registry.Matches(Material.Item, Items->AsString());
		}
		if (Items->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Items->AsArray())
			{
				if (Entry.IsValid() && Entry->Type == EJson::String && Registry.Matches(Material.Item, Entry->AsString()))
				{
					return true;
				}
			}
		}
		return false;
	}
}

FMcAnvilMenu::FMcAnvilMenu(const TSharedPtr<FMcContainer>& PlayerInventory)
	: Table(MakeShared<FMcContainer>(3))
	, Player(PlayerInventory)
{
	AddSlot(Table, LeftSlot);
	AddSlot(Table, RightSlot);
	AddSlot(Table, ResultSlot);
	AddPlayerSlots(PlayerInventory);
}

void FMcAnvilMenu::Refresh()
{
	Cost = 0;
	bTooExpensive = false;
	MaterialUsed = 0;
	Table->Set(ResultSlot, FMcItemStack());
	const FMcItemStack& Left = Table->Get(LeftSlot);
	const FMcItemStack& Right = Table->Get(RightSlot);
	if (Left.IsEmpty())
	{
		return;
	}
	const FMcItemRegistry& Registry = FMcItemRegistry::Get();
	FMcItemStack Result = Left;
	int32 Work = 0;
	const int32 Prior = GetRepairCost(Left) + (Right.IsEmpty() ? 0 : GetRepairCost(Right));
	int32 RenameCost = 0;

	if (!Right.IsEmpty())
	{
		const bool bBook = Right.Item == EnchantedBook && !EnchantmentsOf(Right).IsEmpty();
		if (Result.IsDamageable() && IsRepairMaterial(Left, Right))
		{
			// Each material mends a quarter, while there is damage left.
			const int32 Quarter = FMath::Max(1, Result.GetMaxDamage() / 4);
			int32 Damage = Result.GetDamage();
			if (Damage <= 0)
			{
				return;
			}
			int32 Used = 0;
			while (Damage > 0 && Used < Right.Count)
			{
				Damage = FMath::Max(0, Damage - Quarter);
				++Used;
				++Work;
			}
			Result.SetDamage(Damage);
			MaterialUsed = Used;
		}
		else
		{
			if (!bBook && (Right.Item != Left.Item || !Result.IsDamageable()))
			{
				return;
			}
			if (Result.IsDamageable() && !bBook)
			{
				const int32 Max = Result.GetMaxDamage();
				const int32 Remaining = (Max - Left.GetDamage()) + (Max - Right.GetDamage()) + Max * 12 / 100;
				const int32 NewDamage = FMath::Max(0, Max - Remaining);
				if (NewDamage < Result.GetDamage())
				{
					Result.SetDamage(NewDamage);
					Work += 2;
				}
			}
			TMap<FName, int32> Enchants = EnchantmentsOf(Result);
			bool bAnyApplied = false;
			bool bAnyRefused = false;
			for (const TPair<FName, int32>& Entry : EnchantmentsOf(Right))
			{
				const FMcEnchantmentDef* Def = Registry.FindEnchantment(Entry.Key);
				if (!Def)
				{
					continue;
				}
				const int32 Had = Enchants.FindRef(Entry.Key);
				const int32 Level = FMath::Min(Had == Entry.Value ? Entry.Value + 1 : FMath::Max(Had, Entry.Value), Def->MaxLevel);
				bool bApplies = Left.Item == EnchantedBook || bPlayerCreative || Registry.CanEnchant(Left.Item, Entry.Key);
				for (const TPair<FName, int32>& Other : Enchants)
				{
					if (Other.Key != Entry.Key && !Registry.AreCompatible(Other.Key, Entry.Key))
					{
						bApplies = false;
						++Work;
					}
				}
				if (!bApplies)
				{
					bAnyRefused = true;
					continue;
				}
				bAnyApplied = true;
				Enchants.Add(Entry.Key, Level);
				const int32 PerLevel = bBook ? FMath::Max(1, Def->AnvilCost / 2) : Def->AnvilCost;
				Work += PerLevel * Level;
				if (Left.Count > 1)
				{
					Work = TooExpensive;
				}
			}
			if (bAnyRefused && !bAnyApplied)
			{
				return;
			}
			SetEnchantmentsOf(Result, Enchants);
		}
	}

	// The name: a new one, or taking a custom name off.
	const FString Current = Left.GetCustomName();
	if (ItemName.TrimStartAndEnd().IsEmpty())
	{
		if (!Current.IsEmpty())
		{
			RenameCost = 1;
			Result.SetCustomName(FString());
		}
	}
	else if (ItemName != Current)
	{
		RenameCost = 1;
		Result.SetCustomName(ItemName);
	}
	Work += RenameCost;
	if (Work <= 0)
	{
		return;
	}
	Cost = Prior + Work;
	if (RenameCost == Work && Cost >= TooExpensive)
	{
		// A rename alone never costs too much.
		Cost = TooExpensive - 1;
	}
	if (Cost >= TooExpensive && !bPlayerCreative)
	{
		bTooExpensive = true;
		return;
	}
	if (RenameCost != Work)
	{
		const int32 Penalty = FMath::Max(GetRepairCost(Left), Right.IsEmpty() ? 0 : GetRepairCost(Right));
		Result.Set(RepairCost, MakeShared<FJsonValueNumber>(Penalty * 2 + 1));
	}
	Result.Normalize();
	Table->Set(ResultSlot, Result);
}

bool FMcAnvilMenu::SetText(const FString& Text)
{
	if (Text.Len() > MaxNameLength)
	{
		return false;
	}
	ItemName = Text;
	Refresh();
	return true;
}

void FMcAnvilMenu::SetPlayerLevels(int32 Levels, bool bCreative)
{
	const bool bChanged = bCreative != bPlayerCreative;
	PlayerLevels = Levels;
	bPlayerCreative = bCreative;
	if (bChanged)
	{
		Refresh();
	}
}

bool FMcAnvilMenu::MayPickup(int32 Slot) const
{
	if (Slot != ResultSlot)
	{
		return true;
	}
	return !GetStack(ResultSlot).IsEmpty() && Cost > 0 && (bPlayerCreative || PlayerLevels >= Cost);
}

void FMcAnvilMenu::SlotChanged(int32 Slot)
{
	if (Slot == LeftSlot || Slot == RightSlot)
	{
		Refresh();
	}
}

void FMcAnvilMenu::OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped)
{
	if (Slot != ResultSlot)
	{
		return;
	}
	if (!bPlayerCreative)
	{
		Spent += Cost;
		PlayerLevels -= Cost;
	}
	Table->Set(LeftSlot, FMcItemStack());
	if (MaterialUsed > 0)
	{
		FMcItemStack Rest = Table->Get(RightSlot);
		Rest.Count -= MaterialUsed;
		Table->Set(RightSlot, Rest.Count > 0 ? Rest : FMcItemStack());
	}
	else
	{
		Table->Set(RightSlot, FMcItemStack());
	}
	ItemName.Reset();
	Refresh();
}

int32 FMcAnvilMenu::TakeLevelsSpent()
{
	const int32 Out = Spent;
	Spent = 0;
	return Out;
}

void FMcAnvilMenu::SerializeState(FArchive& Ar)
{
	FString Name = ItemName;
	Ar << Name;
	if (Ar.IsLoading())
	{
		ItemName = Name.Left(MaxNameLength);
		Refresh();
	}
}

FMcItemStack FMcAnvilMenu::QuickMoveStack(int32 Slot)
{
	const FMcItemStack Original = GetStack(Slot);
	if (Original.IsEmpty())
	{
		return FMcItemStack();
	}
	if (Slot == ResultSlot)
	{
		if (!MayPickup(ResultSlot) || !Player->CanAddItem(Original))
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
		bMoved = MoveItemStackTo(Rest, LeftSlot, ResultSlot, false)
			|| (Slot >= HotbarStart ? MoveItemStackTo(Rest, PlayerStart, HotbarStart, false) : MoveItemStackTo(Rest, HotbarStart, NumSlots(), false));
	}
	if (!bMoved)
	{
		return FMcItemStack();
	}
	SetStack(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
	return Original;
}

void FMcAnvilMenu::OnClosed(TArray<FMcItemStack>& Dropped)
{
	Table->Set(ResultSlot, FMcItemStack());
	for (int32 Index = LeftSlot; Index <= RightSlot; ++Index)
	{
		if (!Table->Get(Index).IsEmpty())
		{
			const FMcItemStack Rest = Player->AddItem(Table->Get(Index));
			if (!Rest.IsEmpty())
			{
				Dropped.Add(Rest);
			}
			Table->Set(Index, FMcItemStack());
		}
	}
	ItemName.Reset();
	Refresh();
}
