#include "McEnchanting.h"
#include "McCache.h"
#include "McItemRegistry.h"
#include "McRandom.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const FName Book(TEXT("minecraft:book"));
	const FName EnchantedBook(TEXT("minecraft:enchanted_book"));
	const FName Stored(TEXT("minecraft:stored_enchantments"));

	// Whether the table offers the enchantment for the item: its primary items if it lists any, else what it supports.
	bool TableOffers(const FMcItemRegistry& Items, FName Item, const FMcEnchantmentDef& Def)
	{
		if (Def.Primary.IsEmpty())
		{
			return Items.CanEnchant(Item, Def.Name);
		}
		return Def.Primary.ContainsByPredicate([&](const FString& Reference) { return Items.Matches(Item, Reference); });
	}
}

bool McEnchanting::IsBook(const FMcItemStack& Stack)
{
	return Stack.Item == Book || Stack.Item == EnchantedBook;
}

TMap<FName, int32> McEnchanting::GetEnchantments(const FMcItemStack& Stack)
{
	if (!IsBook(Stack))
	{
		return Stack.GetEnchantments();
	}
	TMap<FName, int32> Result;
	const TSharedPtr<FJsonValue> Value = Stack.Get(Stored);
	if (Value.IsValid() && Value->Type == EJson::Object)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>> Entry : Value->AsObject()->Values)
		{
			Result.Add(FName(*McCache::Namespaced(Entry.Key)), static_cast<int32>(Entry.Value->AsNumber()));
		}
	}
	return Result;
}

void McEnchanting::AddEnchantment(FMcItemStack& Stack, FName Enchantment, int32 Level)
{
	if (IsBook(Stack))
	{
		TMap<FName, int32> Enchantments = GetEnchantments(Stack);
		Enchantments.Add(Enchantment, Level);
		Stack.Item = EnchantedBook;
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		for (const TPair<FName, int32>& Entry : Enchantments)
		{
			Object->SetNumberField(Entry.Key.ToString(), Entry.Value);
		}
		Stack.Set(Stored, MakeShared<FJsonValueObject>(Object));
		return;
	}
	TMap<FName, int32> Enchantments = Stack.GetEnchantments();
	Enchantments.Add(Enchantment, Level);
	Stack.SetEnchantments(Enchantments);
}

TArray<McEnchanting::FEnchant> McEnchanting::Select(FName Item, int32 Levels, const TArray<FName>& Options, FMcRandom& Random, bool bTable)
{
	TArray<FEnchant> Result;
	const FMcItemRegistry& Items = FMcItemRegistry::Get();
	const bool bBook = Item == Book || Item == EnchantedBook;
	const int32 Enchantability = Items.GetEnchantability(Item);
	if (Enchantability <= 0)
	{
		return Result;
	}
	// The power: the levels, pushed up by the enchantability and spread by up to 15% either way.
	const int32 Bonus = Enchantability / 4 + 1;
	const double Modified = Levels + 1 + Random.NextInt(Bonus) + Random.NextInt(Bonus);
	const double Spread = (Random.NextDouble() + Random.NextDouble() - 1.0) * 0.15;
	int32 Power = FMath::Max(1, FMath::RoundToInt32(Modified + Modified * Spread));

	struct FOffer
	{
		FEnchant Enchant;
		int32 Weight;
	};
	TArray<FOffer> Available;
	for (const FName Name : Options)
	{
		const FMcEnchantmentDef* Def = Items.FindEnchantment(Name);
		if (!Def || !(bBook || (bTable ? TableOffers(Items, Item, *Def) : Items.CanEnchant(Item, Name))))
		{
			continue;
		}
		// The highest level whose cost range takes the power.
		for (int32 Level = Def->MaxLevel; Level >= 1; --Level)
		{
			if (Power >= Def->MinCost(Level) && Power <= Def->MaxCost(Level))
			{
				Available.Add({ { Name, Level }, Def->Weight });
				break;
			}
		}
	}
	const auto PickWeighted = [&Random](TArray<FOffer>& From) -> FEnchant
	{
		int32 Total = 0;
		for (const FOffer& Offer : From)
		{
			Total += Offer.Weight;
		}
		int32 Roll = Random.NextInt(FMath::Max(1, Total));
		for (int32 Index = 0; Index < From.Num(); ++Index)
		{
			Roll -= From[Index].Weight;
			if (Roll < 0)
			{
				const FEnchant Picked = From[Index].Enchant;
				From.RemoveAt(Index);
				return Picked;
			}
		}
		return From.Pop().Enchant;
	};
	if (Available.IsEmpty())
	{
		return Result;
	}
	Result.Add(PickWeighted(Available));
	// Each further enchantment is likelier the more power there is, and halves it.
	while (Random.NextInt(50) <= Power)
	{
		const FName Last = Result.Last().Name;
		Available.RemoveAll([&](const FOffer& Offer) { return !Items.AreCompatible(Last, Offer.Enchant.Name); });
		if (Available.IsEmpty())
		{
			break;
		}
		Result.Add(PickWeighted(Available));
		Power /= 2;
	}
	return Result;
}

TArray<FName> McEnchanting::GetTableOptions()
{
	TArray<FName> Options;
	if (const TSet<FName>* Tag = FMcItemRegistry::Get().GetEnchantmentTag(TEXT("minecraft:in_enchanting_table")))
	{
		Options = Tag->Array();
	}
	// A set has no order; a seeded pick must not depend on it.
	Options.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Options;
}

int32 McEnchanting::CountBookshelves(TFunctionRef<bool(const FIntVector&)> IsBookshelf, TFunctionRef<bool(const FIntVector&)> IsClear)
{
	int32 Count = 0;
	for (int32 X = -2; X <= 2; ++X)
	{
		for (int32 Z = -2; Z <= 2; ++Z)
		{
			if (FMath::Max(FMath::Abs(X), FMath::Abs(Z)) != 2)
			{
				continue;
			}
			for (int32 Y = 0; Y <= 1; ++Y)
			{
				// The block halfway to the table (rounded towards it) has to be clear.
				const FIntVector Between(X / 2, Y, Z / 2);
				if (IsBookshelf(FIntVector(X, Y, Z)) && IsClear(Between))
				{
					++Count;
				}
			}
		}
	}
	return FMath::Min(Count, 15);
}

TArray<McEnchanting::FEnchant> McEnchanting::GetTableEnchantments(FName Item, int32 Seed, int32 Slot, int32 Cost)
{
	FMcRandom Random(static_cast<uint64>(static_cast<int64>(Seed) + Slot));
	TArray<FEnchant> Enchants = Select(Item, Cost, GetTableOptions(), Random, true);
	// A book gets one fewer than the same cost would put on a tool.
	if (Item == Book && Enchants.Num() > 1)
	{
		Enchants.RemoveAt(Random.NextInt(Enchants.Num()));
	}
	return Enchants;
}

McEnchanting::FOffers McEnchanting::GetOffers(FName Item, int32 Bookshelves, int32 Seed)
{
	FOffers Offers;
	if (FMcItemRegistry::Get().GetEnchantability(Item) <= 0)
	{
		return Offers;
	}
	const int32 Shelves = FMath::Clamp(Bookshelves, 0, 15);
	FMcRandom Random(static_cast<uint64>(static_cast<int64>(Seed)));
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		const int32 Base = Random.NextInt(8) + 1 + Shelves / 2 + Random.NextInt(Shelves + 1);
		int32 Cost = Slot == 0 ? FMath::Max(Base / 3, 1) : Slot == 1 ? Base * 2 / 3 + 1 : FMath::Max(Base, Shelves * 2);
		// The bottom option needs at least 3 levels, the middle 2.
		Offers.Costs[Slot] = Cost < Slot + 1 ? 0 : Cost;
	}
	for (int32 Slot = 0; Slot < 3; ++Slot)
	{
		if (Offers.Costs[Slot] <= 0)
		{
			continue;
		}
		const TArray<FEnchant> Enchants = GetTableEnchantments(Item, Seed, Slot, Offers.Costs[Slot]);
		if (Enchants.IsEmpty())
		{
			Offers.Costs[Slot] = 0;
			continue;
		}
		Offers.Clues[Slot] = Enchants[Random.NextInt(Enchants.Num())];
	}
	return Offers;
}
