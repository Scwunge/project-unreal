#include "McEnchantEffects.h"

#include "McCache.h"
#include "McItemRegistry.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection GLock;

	TSharedPtr<FJsonObject> GetDefinition(FName Enchantment)
	{
		static TMap<FName, TSharedPtr<FJsonObject>> Cache;
		FScopeLock Lock(&GLock);
		if (const TSharedPtr<FJsonObject>* Found = Cache.Find(Enchantment))
		{
			return *Found;
		}
		TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(TEXT("enchantment/") + McCache::PathOf(Enchantment.ToString()) + TEXT(".json")));
		Cache.Add(Enchantment, Json);
		return Json;
	}

	// The members of an entity type tag, its nested tags resolved (read from the files each time it is first asked).
	TSet<FName> ReadEntityTag(FName Tag, int32 Depth)
	{
		TSet<FName> Members;
		if (Depth > 8)
		{
			return Members;
		}
		const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(TEXT("tags/entity_type/") + McCache::PathOf(Tag.ToString()) + TEXT(".json")));
		const TArray<TSharedPtr<FJsonValue>>* Values;
		if (!Json || !Json->TryGetArrayField(TEXT("values"), Values))
		{
			return Members;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Entry;
			if (!Value->TryGetString(Entry) && Value->Type == EJson::Object)
			{
				Value->AsObject()->TryGetStringField(TEXT("id"), Entry);
			}
			if (Entry.StartsWith(TEXT("#")))
			{
				Members.Append(ReadEntityTag(FName(*McCache::Namespaced(Entry.Mid(1))), Depth + 1));
			}
			else if (!Entry.IsEmpty())
			{
				Members.Add(FName(*McCache::Namespaced(Entry)));
			}
		}
		return Members;
	}

	bool InEntityTag(FName Tag, FName Type)
	{
		static TMap<FName, TSet<FName>> Cache;
		FScopeLock Lock(&GLock);
		TSet<FName>* Found = Cache.Find(Tag);
		if (!Found)
		{
			Found = &Cache.Add(Tag, ReadEntityTag(Tag, 0));
		}
		return Found->Contains(Type);
	}

	FString TypeOf(const TSharedPtr<FJsonObject>& Object)
	{
		FString Type;
		Object->TryGetStringField(TEXT("type"), Type);
		return McCache::PathOf(Type);
	}

	bool TagListMatches(const TSharedPtr<FJsonObject>& Predicate, const TSet<FName>& Tags)
	{
		const TArray<TSharedPtr<FJsonValue>>* List;
		if (!Predicate->TryGetArrayField(TEXT("tags"), List))
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *List)
		{
			const TSharedPtr<FJsonObject> Tag = Entry->AsObject();
			if (!Tag)
			{
				return false;
			}
			FString Id;
			bool bExpected = true;
			Tag->TryGetStringField(TEXT("id"), Id);
			Tag->TryGetBoolField(TEXT("expected"), bExpected);
			if (Id.StartsWith(TEXT("#")))
			{
				Id.RemoveAt(0);
			}
			if (Tags.Contains(FName(*McCache::Namespaced(Id))) != bExpected)
			{
				return false;
			}
		}
		return true;
	}

	// Calls Each(effect body, level) for the effects of that component that pass their requirements.
	void ForEach(const FMcItemStack& Stack, const FString& Component, const McEnchantEffects::FContext& Context, const TFunctionRef<void(const TSharedPtr<FJsonObject>&, int32)>& Each)
	{
		FRandomStream Random(FMath::Rand());
		for (const TPair<FName, int32>& Entry : Stack.GetEnchantments())
		{
			const TSharedPtr<FJsonObject> Definition = GetDefinition(Entry.Key);
			const TSharedPtr<FJsonObject>* Effects = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!Definition || !Definition->TryGetObjectField(TEXT("effects"), Effects) || !(*Effects)->TryGetArrayField(Component, List))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Item : *List)
			{
				const TSharedPtr<FJsonObject> Effect = Item->AsObject();
				const TSharedPtr<FJsonObject>* Body = nullptr;
				if (Effect && Effect->TryGetObjectField(TEXT("effect"), Body) && McEnchantEffects::Check(Effect->TryGetField(TEXT("requirements")), Context, Stack, Entry.Value, Random))
				{
					Each(*Body, Entry.Value);
				}
			}
		}
	}
}

namespace McEnchantEffects
{
	float EvalLevelBased(const TSharedPtr<FJsonValue>& Value, int32 Level)
	{
		if (!Value.IsValid())
		{
			return 0.0f;
		}
		double Number;
		if (Value->Type == EJson::Number && Value->TryGetNumber(Number))
		{
			return static_cast<float>(Number);
		}
		const TSharedPtr<FJsonObject> Object = Value->Type == EJson::Object ? Value->AsObject() : nullptr;
		if (!Object)
		{
			return 0.0f;
		}
		const FString Type = TypeOf(Object);
		const float Above = static_cast<float>(FMath::Max(0, Level - 1));
		if (Type == TEXT("linear"))
		{
			return static_cast<float>(Object->GetNumberField(TEXT("base")) + Object->GetNumberField(TEXT("per_level_above_first")) * Above);
		}
		if (Type == TEXT("constant"))
		{
			return static_cast<float>(Object->GetNumberField(TEXT("value")));
		}
		if (Type == TEXT("levels_squared"))
		{
			return static_cast<float>(Level * Level + Object->GetNumberField(TEXT("added")));
		}
		if (Type == TEXT("fraction"))
		{
			const float Denominator = EvalLevelBased(Object->TryGetField(TEXT("denominator")), Level);
			return Denominator == 0.0f ? 0.0f : EvalLevelBased(Object->TryGetField(TEXT("numerator")), Level) / Denominator;
		}
		if (Type == TEXT("exponent"))
		{
			return FMath::Pow(EvalLevelBased(Object->TryGetField(TEXT("base")), Level), EvalLevelBased(Object->TryGetField(TEXT("power")), Level));
		}
		if (Type == TEXT("clamped"))
		{
			return FMath::Clamp(EvalLevelBased(Object->TryGetField(TEXT("value")), Level), static_cast<float>(Object->GetNumberField(TEXT("min"))), static_cast<float>(Object->GetNumberField(TEXT("max"))));
		}
		if (Type == TEXT("lookup"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Values;
			if (Object->TryGetArrayField(TEXT("values"), Values) && Level >= 1 && Level <= Values->Num())
			{
				return EvalLevelBased((*Values)[Level - 1], Level);
			}
			return EvalLevelBased(Object->TryGetField(TEXT("fallback")), Level);
		}
		return 0.0f;
	}

	bool EntityMatches(FName Type, const TSharedPtr<FJsonValue>& Reference)
	{
		if (!Reference.IsValid() || Type.IsNone())
		{
			return false;
		}
		FString Text;
		if (Reference->TryGetString(Text))
		{
			if (Text.StartsWith(TEXT("#")))
			{
				return InEntityTag(FName(*McCache::Namespaced(Text.Mid(1))), Type);
			}
			return Type == FName(*McCache::Namespaced(Text));
		}
		if (Reference->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Each : Reference->AsArray())
			{
				if (EntityMatches(Type, Each))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool Check(const TSharedPtr<FJsonValue>& Requirement, const FContext& Context, const FMcItemStack& Stack, int32 Level, FRandomStream& Random)
	{
		if (!Requirement.IsValid())
		{
			return true;
		}
		const TSharedPtr<FJsonObject> Object = Requirement->Type == EJson::Object ? Requirement->AsObject() : nullptr;
		if (!Object)
		{
			return false;
		}
		const FString Type = TypeOf(Object);
		if (Type == TEXT("all_of") || Type == TEXT("any_of"))
		{
			const bool bAll = Type == TEXT("all_of");
			const TArray<TSharedPtr<FJsonValue>>* Terms;
			if (!Object->TryGetArrayField(TEXT("terms"), Terms))
			{
				return bAll;
			}
			for (const TSharedPtr<FJsonValue>& Term : *Terms)
			{
				const bool bPass = Check(Term, Context, Stack, Level, Random);
				if (bAll && !bPass)
				{
					return false;
				}
				if (!bAll && bPass)
				{
					return true;
				}
			}
			return bAll;
		}
		if (Type == TEXT("inverted"))
		{
			return !Check(Object->TryGetField(TEXT("term")), Context, Stack, Level, Random);
		}
		if (Type == TEXT("random_chance"))
		{
			return Random.GetFraction() < EvalLevelBased(Object->TryGetField(TEXT("chance")), Level);
		}
		const TSharedPtr<FJsonObject>* Predicate;
		if (Type == TEXT("match_tool"))
		{
			FString Items;
			if (Object->TryGetObjectField(TEXT("predicate"), Predicate) && (*Predicate)->TryGetStringField(TEXT("items"), Items))
			{
				return FMcItemRegistry::Get().Matches(Stack.Item, Items);
			}
			return false;
		}
		if (Type == TEXT("damage_source_properties") && Object->TryGetObjectField(TEXT("predicate"), Predicate))
		{
			bool bDirect;
			if ((*Predicate)->TryGetBoolField(TEXT("is_direct"), bDirect) && bDirect != Context.bDirect)
			{
				return false;
			}
			return TagListMatches(*Predicate, Context.DamageTags);
		}
		if (Type == TEXT("entity_properties") && Object->TryGetObjectField(TEXT("predicate"), Predicate))
		{
			FString Who;
			Object->TryGetStringField(TEXT("entity"), Who);
			const TSharedPtr<FJsonValue> Wanted = (*Predicate)->TryGetField(TEXT("minecraft:entity_type"));
			if (!Wanted.IsValid())
			{
				return false;
			}
			if (Who == TEXT("direct_attacker") && !Context.DirectAttackerType.IsNone())
			{
				return EntityMatches(Context.DirectAttackerType, Wanted);
			}
			if (Who == TEXT("attacker") || Who == TEXT("direct_attacker"))
			{
				return Context.bAttackerIsPlayer && EntityMatches(TEXT("minecraft:player"), Wanted);
			}
			return EntityMatches(Context.VictimType, Wanted);
		}
		return false;
	}

	TSet<FName> GetDamageTags(FName DamageType)
	{
		static TMap<FName, TSet<FName>> ByType;
		static bool bRead = false;
		FScopeLock Lock(&GLock);
		if (!bRead)
		{
			bRead = true;
			TArray<FString> Files;
			const FString Root = McCache::Data(TEXT("tags/damage_type"));
			IFileManager::Get().FindFiles(Files, *(Root / TEXT("*.json")), true, false);
			for (const FString& File : Files)
			{
				const FName Tag(*McCache::Namespaced(FPaths::GetBaseFilename(File)));
				const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Root / File);
				const TArray<TSharedPtr<FJsonValue>>* Values;
				if (Json && Json->TryGetArrayField(TEXT("values"), Values))
				{
					for (const TSharedPtr<FJsonValue>& Value : *Values)
					{
						FString Entry;
						if (Value->TryGetString(Entry))
						{
							ByType.FindOrAdd(FName(*McCache::Namespaced(Entry))).Add(Tag);
						}
					}
				}
			}
		}
		const TSet<FName>* Found = ByType.Find(DamageType);
		return Found ? *Found : TSet<FName>();
	}

	bool HasEffect(FName Enchantment, const FString& Component)
	{
		const TSharedPtr<FJsonObject> Definition = GetDefinition(Enchantment);
		const TSharedPtr<FJsonObject>* Effects;
		return Definition && Definition->TryGetObjectField(TEXT("effects"), Effects) && (*Effects)->HasField(Component);
	}

	float SumAdd(const FMcItemStack& Stack, const FString& Component, const FContext& Context)
	{
		float Sum = 0.0f;
		ForEach(Stack, Component, Context, [&](const TSharedPtr<FJsonObject>& Body, int32 Level)
		{
			if (TypeOf(Body) == TEXT("add"))
			{
				Sum += EvalLevelBased(Body->TryGetField(TEXT("value")), Level);
			}
		});
		return Sum;
	}

	float GetFactor(const FMcItemStack& Stack, const FString& Component, const FContext& Context)
	{
		float Product = 1.0f;
		ForEach(Stack, Component, Context, [&](const TSharedPtr<FJsonObject>& Body, int32 Level)
		{
			if (TypeOf(Body) == TEXT("multiply"))
			{
				Product *= EvalLevelBased(Body->TryGetField(TEXT("factor")), Level);
			}
		});
		return Product;
	}

	int32 GetItemDamage(const FMcItemStack& Stack, int32 Amount, FRandomStream& Random)
	{
		if (Amount <= 0 || Stack.GetEnchantments().IsEmpty())
		{
			return Amount;
		}
		// "minecraft:item_damage" effects: remove_binomial drops each point of wear with its chance.
		float Chance = 0.0f;
		int32 Found = 0;
		ForEach(Stack, TEXT("minecraft:item_damage"), FContext(), [&](const TSharedPtr<FJsonObject>& Body, int32 Level)
		{
			if (TypeOf(Body) == TEXT("remove_binomial"))
			{
				Chance = EvalLevelBased(Body->TryGetField(TEXT("chance")), Level);
				++Found;
			}
		});
		if (Found == 0)
		{
			return Amount;
		}
		int32 Kept = 0;
		for (int32 Point = 0; Point < Amount; ++Point)
		{
			Kept += Random.GetFraction() < Chance ? 0 : 1;
		}
		return Kept;
	}
}
