#include "McTrading.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "McCache.h"
#include "McLoot.h"
#include "McRandom.h"

namespace
{
	constexpr int32 MaxTagDepth = 6;

	// The names a tag lists, its own tags opened, as the ids of trade files ("minecraft:farmer/1/wheat_emerald").
	void AddTradeTag(const FString& Tag, TArray<FString>& Out, int32 Depth = 0)
	{
		if (Depth > MaxTagDepth)
		{
			return;
		}
		const TSharedPtr<FJsonObject> Object = McCache::LoadJsonObject(McCache::Data(TEXT("tags/villager_trade/") + McCache::PathOf(Tag) + TEXT(".json")));
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("values"), Values))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Values)
		{
			FString Text;
			if (Entry->TryGetString(Text))
			{
				if (Text.StartsWith(TEXT("#")))
				{
					AddTradeTag(Text.Mid(1), Out, Depth + 1);
				}
				else
				{
					Out.AddUnique(McCache::Namespaced(Text));
				}
			}
		}
	}

	// A stack the trade names: an id, a count and the components it carries.
	bool ReadStack(const TSharedPtr<FJsonObject>& Object, FMcItemStack& Out)
	{
		FString Id;
		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("id"), Id))
		{
			return false;
		}
		int32 Count = 1;
		Object->TryGetNumberField(TEXT("count"), Count);
		Out = FMcItemStack(FName(*McCache::Namespaced(Id)), FMath::Max(1, Count));
		const TSharedPtr<FJsonObject>* Components = nullptr;
		if (Object->TryGetObjectField(TEXT("components"), Components))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Components)->Values)
			{
				Out.Set(FName(*McCache::Namespaced(Entry.Key)), Entry.Value);
			}
		}
		return true;
	}

	// Whether the trade's condition (only "the villager's type is one of these" is asked) holds for a villager of the type.
	bool Allows(const TSharedPtr<FJsonObject>& Trade, FName VillagerVariant)
	{
		const TSharedPtr<FJsonObject>* Predicate = nullptr;
		if (!Trade->TryGetObjectField(TEXT("merchant_predicate"), Predicate))
		{
			return true;
		}
		const TSharedPtr<FJsonObject>* Inner = nullptr;
		const TSharedPtr<FJsonObject>* Predicates = nullptr;
		if (!(*Predicate)->TryGetObjectField(TEXT("predicate"), Inner) || !(*Inner)->TryGetObjectField(TEXT("minecraft:predicates"), Predicates))
		{
			return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* Variants = nullptr;
		if (!(*Predicates)->TryGetArrayField(TEXT("minecraft:villager/variant"), Variants))
		{
			return true;
		}
		const FString Mine = McCache::PathOf(VillagerVariant.IsNone() ? FString(TEXT("plains")) : VillagerVariant.ToString());
		for (const TSharedPtr<FJsonValue>& Entry : *Variants)
		{
			FString Text;
			if (Entry->TryGetString(Text) && McCache::PathOf(Text) == Mine)
			{
				return true;
			}
		}
		return false;
	}

	bool MakeOffer(const FString& Id, FName VillagerVariant, FMcRandom& Random, McTrading::FOffer& Out)
	{
		const TSharedPtr<FJsonObject> Trade = McCache::LoadJsonObject(McCache::Data(TEXT("villager_trade/") + McCache::PathOf(Id) + TEXT(".json")));
		if (!Trade.IsValid() || !Allows(Trade, VillagerVariant))
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Wants = nullptr;
		const TSharedPtr<FJsonObject>* Gives = nullptr;
		if (!Trade->TryGetObjectField(TEXT("wants"), Wants) || !Trade->TryGetObjectField(TEXT("gives"), Gives)
			|| !ReadStack(*Wants, Out.Wants) || !ReadStack(*Gives, Out.Gives))
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Extra = nullptr;
		if (Trade->TryGetObjectField(TEXT("additional_wants"), Extra))
		{
			ReadStack(*Extra, Out.Extra);
		}
		Out.MaxUses = 12;
		Trade->TryGetNumberField(TEXT("max_uses"), Out.MaxUses);
		Out.Xp = 1;
		Trade->TryGetNumberField(TEXT("xp"), Out.Xp);
		double Discount = 0.0;
		Trade->TryGetNumberField(TEXT("reputation_discount"), Discount);
		Out.Discount = static_cast<float>(Discount);
		// What it gives may be enchanted, or have a potion, once it is made.
		if (const TSharedPtr<FJsonValue> Modifier = Trade->TryGetField(TEXT("given_item_modifier")))
		{
			McLoot::ApplyModifier(Modifier, Random, Out.Gives);
		}
		return !Out.Gives.IsEmpty();
	}
}

bool McTrading::FOffer::Serialize(FArchive& Ar)
{
	if (!Wants.Serialize(Ar) || !Extra.Serialize(Ar) || !Gives.Serialize(Ar))
	{
		return false;
	}
	Ar << MaxUses << Uses << Xp << Discount;
	return !Ar.IsError() && MaxUses > 0 && Uses >= 0 && Xp >= 0 && !Wants.IsEmpty() && !Gives.IsEmpty();
}

bool McTrading::RollSet(const FString& Set, FName VillagerVariant, FMcRandom& Random, TArray<FOffer>& Out)
{
	const TSharedPtr<FJsonObject> Object = McCache::LoadJsonObject(McCache::Data(TEXT("trade_set/") + Set + TEXT(".json")));
	FString Tag;
	if (!Object.IsValid() || !Object->TryGetStringField(TEXT("trades"), Tag))
	{
		return false;
	}
	int32 Amount = 1;
	Object->TryGetNumberField(TEXT("amount"), Amount);
	TArray<FString> Ids;
	AddTradeTag(Tag.StartsWith(TEXT("#")) ? Tag.Mid(1) : Tag, Ids);
	// Without repeats: each pick leaves the list; one that can't be made (the villager's type rules it out) is dropped.
	while (Amount > 0 && !Ids.IsEmpty())
	{
		const int32 Pick = Random.NextInt(Ids.Num());
		FOffer Offer;
		if (MakeOffer(Ids[Pick], VillagerVariant, Random, Offer))
		{
			Out.Add(MoveTemp(Offer));
			--Amount;
		}
		Ids.RemoveAtSwap(Pick);
	}
	return true;
}

bool McTrading::RollLevel(FName Profession, int32 Level, FName VillagerVariant, FMcRandom& Random, TArray<FOffer>& Out)
{
	return RollSet(McCache::PathOf(Profession.ToString()) + FString::Printf(TEXT("/level_%d"), Level), VillagerVariant, Random, Out);
}

int32 McTrading::GetCost(const FOffer& Offer, int32 Reputation)
{
	const int32 Base = Offer.Wants.Count;
	return FMath::Max(1, Base - FMath::FloorToInt32(Base * Offer.Discount * Reputation));
}
