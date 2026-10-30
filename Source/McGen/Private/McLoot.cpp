#include "McLoot.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McGen.h"
#include "McItemRegistry.h"
#include "McNbt.h"
#include "McRandom.h"
#include "Misc/ScopeLock.h"

namespace
{
	constexpr int32 MaxDepth = 8;

	FCriticalSection TablesLock;
	TMap<FString, TSharedPtr<FJsonObject>> Tables;

	// The block being rolled for, if a block's table is (set for the length of RollBlock on its thread).
	thread_local const McLoot::FBlockContext* GBlock = nullptr;
	// Whether the mob whose table is being rolled leads a raid party: 1 or 0, -1 when the roll is for no mob.
	thread_local int32 GCaptain = -1;

	TSharedPtr<FJsonObject> FindTable(const FString& Id)
	{
		const FString Key = McCache::Namespaced(Id);
		FScopeLock Lock(&TablesLock);
		if (const TSharedPtr<FJsonObject>* Found = Tables.Find(Key))
		{
			return *Found;
		}
		TSharedPtr<FJsonObject> Table;
		const FString Path = McCache::Data(TEXT("loot_table/") + McCache::PathOf(Key) + TEXT(".json"));
		if (IFileManager::Get().FileExists(*Path))
		{
			Table = McCache::LoadJsonObject(Path);
		}
		if (!Table.IsValid())
		{
			UE_LOG(LogMcGen, Warning, TEXT("Loot table %s can't be read"), *Key);
		}
		Tables.Add(Key, Table);
		return Table;
	}

	// A whole number from a number, or a provider: constant, uniform (inclusive) or binomial.
	int32 Count(const TSharedPtr<FJsonValue>& Value, FMcRandom& Random)
	{
		if (!Value.IsValid())
		{
			return 0;
		}
		if (Value->Type == EJson::Number)
		{
			return FMath::FloorToInt32(Value->AsNumber());
		}
		if (Value->Type != EJson::Object)
		{
			return 0;
		}
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		const FString Type = McCache::PathOf(Object->GetStringField(TEXT("type")));
		if (Type == TEXT("constant"))
		{
			return Count(Object->TryGetField(TEXT("value")), Random);
		}
		if (Type == TEXT("uniform"))
		{
			const int32 Min = Count(Object->TryGetField(TEXT("min")), Random);
			const int32 Max = Count(Object->TryGetField(TEXT("max")), Random);
			return Max < Min ? Min : Random.NextIntBetween(Min, Max);
		}
		if (Type == TEXT("binomial"))
		{
			const int32 Trials = Count(Object->TryGetField(TEXT("n")), Random);
			const double Chance = Object->HasTypedField<EJson::Number>(TEXT("p")) ? Object->GetNumberField(TEXT("p")) : 0.5;
			int32 Hits = 0;
			for (int32 Trial = 0; Trial < Trials; ++Trial)
			{
				Hits += Random.NextDouble() < Chance ? 1 : 0;
			}
			return Hits;
		}
		return 0;
	}

	double Real(const TSharedPtr<FJsonValue>& Value, FMcRandom& Random)
	{
		if (Value.IsValid() && Value->Type == EJson::Number)
		{
			return Value->AsNumber();
		}
		if (Value.IsValid() && Value->Type == EJson::Object && Value->AsObject()->GetStringField(TEXT("type")).EndsWith(TEXT("uniform")))
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const double Min = Real(Object->TryGetField(TEXT("min")), Random);
			const double Max = Real(Object->TryGetField(TEXT("max")), Random);
			return Min + Random.NextDouble() * (Max - Min);
		}
		return Count(Value, Random);
	}

	// 1 passes, 0 fails, -1 can't be told at generation (which passes, but not when inverted).
	int32 Check(const TSharedPtr<FJsonValue>& Condition, FMcRandom& Random)
	{
		if (!Condition.IsValid())
		{
			return 1;
		}
		if (Condition->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Each : Condition->AsArray())
			{
				if (Check(Each, Random) == 0)
				{
					return 0;
				}
			}
			return 1;
		}
		if (Condition->Type == EJson::String)
		{
			// A named predicate. Breaking a block without a tool fails every one of the tool ones (silk touch, shears).
			return GBlock && McCache::PathOf(Condition->AsString()).StartsWith(TEXT("tool/")) ? 0 : -1;
		}
		if (Condition->Type != EJson::Object)
		{
			return -1;
		}
		const TSharedPtr<FJsonObject> Object = Condition->AsObject();
		const FString Type = McCache::PathOf(Object->GetStringField(TEXT("type")).IsEmpty() ? Object->GetStringField(TEXT("condition")) : Object->GetStringField(TEXT("type")));
		if (Type == TEXT("random_chance"))
		{
			return Random.NextDouble() < Real(Object->TryGetField(TEXT("chance")), Random) ? 1 : 0;
		}
		if (Type == TEXT("survives_explosion"))
		{
			// An item of a block that was blown up survives with a chance of 1 over the blast's power.
			return GBlock && GBlock->ExplosionRadius > 0.0f && Random.NextFloat() > 1.0f / GBlock->ExplosionRadius ? 0 : 1;
		}
		if (GBlock && Type == TEXT("match_tool"))
		{
			return 0;
		}
		if (GBlock && Type == TEXT("table_bonus"))
		{
			// Fortune at level 0: the first chance.
			const TArray<TSharedPtr<FJsonValue>>* Chances;
			return Object->TryGetArrayField(TEXT("chances"), Chances) && Chances->Num() > 0 && Random.NextDouble() < (*Chances)[0]->AsNumber() ? 1 : 0;
		}
		if (GBlock && Type == TEXT("match_block"))
		{
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			if (!Registry.IsValidState(GBlock->State))
			{
				return -1;
			}
			// One block, or a list of them (a "#tag" is not understood).
			TArray<FString> Names;
			FString Single;
			const TArray<TSharedPtr<FJsonValue>>* List;
			if (Object->TryGetStringField(TEXT("blocks"), Single))
			{
				Names.Add(Single);
			}
			else if (Object->TryGetArrayField(TEXT("blocks"), List))
			{
				for (const TSharedPtr<FJsonValue>& Each : *List)
				{
					Names.Add(Each->AsString());
				}
			}
			if (!Names.IsEmpty())
			{
				bool bAnyTag = false;
				bool bFound = false;
				for (const FString& Name : Names)
				{
					bAnyTag |= Name.StartsWith(TEXT("#"));
					bFound |= FName(*McCache::Namespaced(Name)) == Registry.GetBlockOf(GBlock->State).Name;
				}
				if (!bFound)
				{
					return bAnyTag ? -1 : 0;
				}
			}
			const TSharedPtr<FJsonObject>* Properties;
			if (Object->TryGetObjectField(TEXT("state"), Properties))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Properties)->Values)
				{
					if (Entry.Value->Type == EJson::Object)
					{
						return -1;
					}
					if (Registry.GetPropertyValue(GBlock->State, FName(*Entry.Key)) != FName(*Entry.Value->AsString()))
					{
						return 0;
					}
				}
			}
			return 1;
		}
		if (GCaptain >= 0 && Type == TEXT("entity_properties"))
		{
			// Only the raider's captain flag is known; any other predicate can't be told here.
			const TSharedPtr<FJsonObject>* Predicate;
			const TSharedPtr<FJsonObject>* Raider;
			bool bWanted = false;
			if (Object->TryGetObjectField(TEXT("predicate"), Predicate) && (*Predicate)->TryGetObjectField(TEXT("minecraft:type_specific/raider"), Raider) && (*Raider)->TryGetBoolField(TEXT("is_captain"), bWanted))
			{
				return (GCaptain != 0) == bWanted ? 1 : 0;
			}
			return -1;
		}
		if (Type == TEXT("inverted"))
		{
			const int32 Inner = Check(Object->TryGetField(TEXT("term")), Random);
			return Inner < 0 ? -1 : 1 - Inner;
		}
		const TArray<TSharedPtr<FJsonValue>>* Terms;
		if ((Type == TEXT("all_of") || Type == TEXT("any_of")) && Object->TryGetArrayField(TEXT("terms"), Terms))
		{
			const bool bAll = Type == TEXT("all_of");
			int32 Result = bAll ? 1 : 0;
			for (const TSharedPtr<FJsonValue>& Term : *Terms)
			{
				const int32 Each = Check(Term, Random);
				if (bAll ? Each == 0 : Each == 1)
				{
					return Each;
				}
				if (Each < 0)
				{
					Result = -1;
				}
			}
			return Result;
		}
		return -1;
	}

	// Whether an entry, pool or modifier's own conditions let it through.
	bool Passes(const FJsonObject& Object, FMcRandom& Random)
	{
		return Check(Object.TryGetField(TEXT("condition")), Random) != 0 && Check(Object.TryGetField(TEXT("conditions")), Random) != 0;
	}

	// ---- Item functions (minecraft.wiki, "Item modifier") ----

	TSharedPtr<FJsonValue> Number(double Value)
	{
		return MakeShared<FJsonValueNumber>(Value);
	}

	// The enchantments an "options" field names: an enchantment, a "#tag" or a list of them; all if it is missing.
	void EnchantmentOptions(const TSharedPtr<FJsonValue>& Options, TArray<FName>& Out)
	{
		const FMcItemRegistry& Items = FMcItemRegistry::Get();
		TArray<FString> References;
		FString Text;
		if (Options.IsValid() && Options->TryGetString(Text))
		{
			References.Add(Text);
		}
		else if (Options.IsValid() && Options->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Each : Options->AsArray())
			{
				if (Each->TryGetString(Text))
				{
					References.Add(Text);
				}
			}
		}
		else
		{
			for (const FMcEnchantmentDef& Def : Items.GetEnchantments())
			{
				Out.Add(Def.Name);
			}
			return;
		}
		for (const FString& Reference : References)
		{
			FName Name;
			if (McSplitReference(Reference, Name))
			{
				if (const TSet<FName>* Members = Items.GetEnchantmentTag(Name))
				{
					Out.Append(Members->Array());
				}
			}
			else
			{
				Out.Add(Name);
			}
		}
		// A set has no order; a seeded pick must not depend on it.
		Out.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	}

	// Books take any enchantment and become enchanted books holding them.
	bool IsBook(const FMcItemStack& Stack)
	{
		return Stack.Item == TEXT("minecraft:book") || Stack.Item == TEXT("minecraft:enchanted_book");
	}

	void AddEnchantment(FMcItemStack& Stack, FName Enchantment, int32 Level)
	{
		if (IsBook(Stack))
		{
			Stack.Item = TEXT("minecraft:enchanted_book");
			TMap<FName, int32> Stored;
			const TSharedPtr<FJsonValue> Existing = Stack.Get(TEXT("minecraft:stored_enchantments"));
			if (Existing.IsValid() && Existing->Type == EJson::Object)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Existing->AsObject()->Values)
				{
					Stored.Add(FName(*McCache::Namespaced(Entry.Key)), static_cast<int32>(Entry.Value->AsNumber()));
				}
			}
			Stored.Add(Enchantment, Level);
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			for (const TPair<FName, int32>& Entry : Stored)
			{
				Object->SetNumberField(Entry.Key.ToString(), Entry.Value);
			}
			Stack.Set(TEXT("minecraft:stored_enchantments"), MakeShared<FJsonValueObject>(Object));
			return;
		}
		TMap<FName, int32> Enchantments = Stack.GetEnchantments();
		Enchantments.Add(Enchantment, Level);
		Stack.SetEnchantments(Enchantments);
	}

	TMap<FName, int32> CurrentEnchantments(const FMcItemStack& Stack)
	{
		if (!IsBook(Stack))
		{
			return Stack.GetEnchantments();
		}
		TMap<FName, int32> Result;
		const TSharedPtr<FJsonValue> Stored = Stack.Get(TEXT("minecraft:stored_enchantments"));
		if (Stored.IsValid() && Stored->Type == EJson::Object)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Stored->AsObject()->Values)
			{
				Result.Add(FName(*McCache::Namespaced(Entry.Key)), static_cast<int32>(Entry.Value->AsNumber()));
			}
		}
		return Result;
	}

	bool FitsWithExisting(const FMcItemStack& Stack, FName Enchantment)
	{
		const FMcItemRegistry& Items = FMcItemRegistry::Get();
		for (const TPair<FName, int32>& Entry : CurrentEnchantments(Stack))
		{
			if (!Items.AreCompatible(Entry.Key, Enchantment) && Entry.Key != Enchantment)
			{
				return false;
			}
		}
		return true;
	}

	// enchant_randomly: one enchantment the item can take, at a level from 1 up to its highest.
	void EnchantRandomly(const FJsonObject& Object, FMcRandom& Random, FMcItemStack& Stack)
	{
		const FMcItemRegistry& Items = FMcItemRegistry::Get();
		TArray<FName> Options;
		EnchantmentOptions(Object.TryGetField(TEXT("options")), Options);
		bool bOnlyCompatible = true;
		Object.TryGetBoolField(TEXT("only_compatible"), bOnlyCompatible);
		TArray<FName> Candidates;
		for (const FName Name : Options)
		{
			if ((IsBook(Stack) || Items.CanEnchant(Stack.Item, Name)) && (!bOnlyCompatible || FitsWithExisting(Stack, Name)))
			{
				Candidates.Add(Name);
			}
		}
		if (Candidates.IsEmpty())
		{
			return;
		}
		const FName Pick = Candidates[Random.NextInt(Candidates.Num())];
		const FMcEnchantmentDef* Def = Items.FindEnchantment(Pick);
		AddEnchantment(Stack, Pick, 1 + Random.NextInt(Def ? Def->MaxLevel : 1));
	}

	// enchant_with_levels: what an enchanting table gives at that many levels.
	void EnchantWithLevels(const FJsonObject& Object, FMcRandom& Random, FMcItemStack& Stack)
	{
		const FMcItemRegistry& Items = FMcItemRegistry::Get();
		const int32 Enchantability = Items.GetEnchantability(Stack.Item);
		if (Enchantability <= 0 && !IsBook(Stack))
		{
			return;
		}
		const int32 Levels = Count(Object.TryGetField(TEXT("levels")), Random);
		const int32 Bonus = Enchantability / 4 + 1;
		double Modified = Levels + 1 + Random.NextInt(Bonus) + Random.NextInt(Bonus);
		const double Spread = (Random.NextDouble() + Random.NextDouble() - 1.0) * 0.15;
		const int32 Power = FMath::Max(1, FMath::RoundToInt32(Modified + Modified * Spread));

		TArray<FName> Options;
		EnchantmentOptions(Object.TryGetField(TEXT("options")), Options);
		struct FOffer
		{
			FName Name;
			int32 Level;
			int32 Weight;
		};
		TArray<FOffer> Available;
		for (const FName Name : Options)
		{
			const FMcEnchantmentDef* Def = Items.FindEnchantment(Name);
			if (!Def || !(IsBook(Stack) || Items.CanEnchant(Stack.Item, Name)))
			{
				continue;
			}
			for (int32 Level = Def->MaxLevel; Level >= 1; --Level)
			{
				if (Power >= Def->MinCost(Level) && Power <= Def->MaxCost(Level))
				{
					Available.Add({ Name, Level, Def->Weight });
					break;
				}
			}
		}
		const auto PickWeighted = [&Random](TArray<FOffer>& From) -> FOffer
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
					const FOffer Result = From[Index];
					From.RemoveAt(Index);
					return Result;
				}
			}
			const FOffer Last = From.Pop();
			return Last;
		};
		if (Available.IsEmpty())
		{
			return;
		}
		FOffer Chosen = PickWeighted(Available);
		AddEnchantment(Stack, Chosen.Name, Chosen.Level);
		// Each further enchantment is likelier the more power there is, and halves it.
		int32 Remaining = Power;
		while (Random.NextInt(50) <= Remaining)
		{
			Available.RemoveAll([&](const FOffer& Offer) { return !Items.AreCompatible(Chosen.Name, Offer.Name); });
			if (Available.IsEmpty())
			{
				break;
			}
			Chosen = PickWeighted(Available);
			AddEnchantment(Stack, Chosen.Name, Chosen.Level);
			Remaining /= 2;
		}
	}

	// A text component as the item stores it: kept as JSON.
	TSharedPtr<FJsonValue> CopyValue(const TSharedPtr<FJsonValue>& Value)
	{
		return Value;
	}

	// The effects of a modifier (or list of them) on a stack.
	void Modify(const TSharedPtr<FJsonValue>& Modifier, FMcRandom& Random, FMcItemStack& Stack)
	{
		if (!Modifier.IsValid() || Stack.Count <= 0)
		{
			return;
		}
		if (Modifier->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Each : Modifier->AsArray())
			{
				Modify(Each, Random, Stack);
			}
			return;
		}
		if (Modifier->Type != EJson::Object)
		{
			return;
		}
		const TSharedPtr<FJsonObject> Object = Modifier->AsObject();
		const FString Type = McCache::PathOf(Object->GetStringField(TEXT("type")).IsEmpty() ? Object->GetStringField(TEXT("function")) : Object->GetStringField(TEXT("type")));
		if (!Passes(*Object, Random))
		{
			return;
		}
		if (Type == TEXT("set_count"))
		{
			const int32 Amount = Count(Object->TryGetField(TEXT("count")), Random);
			bool bAdd = false;
			Object->TryGetBoolField(TEXT("add"), bAdd);
			Stack.Count = bAdd ? Stack.Count + Amount : Amount;
		}
		else if (Type == TEXT("explosion_decay") && GBlock && GBlock->ExplosionRadius > 0.0f)
		{
			// Each item of the stack survives a blast with a chance of 1 over its power.
			int32 Kept = 0;
			for (int32 Item = 0; Item < Stack.Count; ++Item)
			{
				Kept += Random.NextFloat() <= 1.0f / GBlock->ExplosionRadius ? 1 : 0;
			}
			Stack.Count = Kept;
		}
		else if (Type == TEXT("apply_bonus") && GBlock)
		{
			// Fortune at level 0: ore drops and uniform bonuses add nothing; a binomial one still has its extra rounds.
			const FString Formula = McCache::PathOf(Object->GetStringField(TEXT("formula")));
			const TSharedPtr<FJsonObject>* Parameters;
			if (Formula == TEXT("binomial_with_bonus_count") && Object->TryGetObjectField(TEXT("parameters"), Parameters))
			{
				const int32 Extra = static_cast<int32>((*Parameters)->GetNumberField(TEXT("extra")));
				const double Probability = (*Parameters)->GetNumberField(TEXT("probability"));
				for (int32 Trial = 0; Trial < Extra; ++Trial)
				{
					Stack.Count += Random.NextFloat() < Probability ? 1 : 0;
				}
			}
		}
		else if (Type == TEXT("limit_count"))
		{
			// A number is the most; { min, max } bounds it both ways.
			const TSharedPtr<FJsonValue> Limit = Object->TryGetField(TEXT("limit"));
			if (Limit.IsValid() && Limit->Type == EJson::Number)
			{
				Stack.Count = FMath::Min(Stack.Count, static_cast<int32>(Limit->AsNumber()));
			}
			else if (Limit.IsValid() && Limit->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> Bounds = Limit->AsObject();
				if (Bounds->HasField(TEXT("max")))
				{
					Stack.Count = FMath::Min(Stack.Count, Count(Bounds->TryGetField(TEXT("max")), Random));
				}
				if (Bounds->HasField(TEXT("min")))
				{
					Stack.Count = FMath::Max(Stack.Count, Count(Bounds->TryGetField(TEXT("min")), Random));
				}
			}
		}
		else if (Type == TEXT("set_damage"))
		{
			// The damage is the fraction of the durability left: 1 is new.
			const int32 Max = Stack.GetMaxDamage();
			if (Max > 0)
			{
				const double Left = FMath::Clamp(Real(Object->TryGetField(TEXT("damage")), Random), 0.0, 1.0);
				bool bAdd = false;
				Object->TryGetBoolField(TEXT("add"), bAdd);
				const double Fraction = bAdd ? 1.0 - (static_cast<double>(Stack.GetDamage()) / Max) + Left : Left;
				Stack.SetDamage(FMath::FloorToInt32((1.0 - FMath::Clamp(Fraction, 0.0, 1.0)) * Max));
			}
		}
		else if (Type == TEXT("enchant_randomly"))
		{
			EnchantRandomly(*Object, Random, Stack);
		}
		else if (Type == TEXT("enchant_with_levels"))
		{
			EnchantWithLevels(*Object, Random, Stack);
		}
		else if (Type == TEXT("set_enchantments"))
		{
			const TSharedPtr<FJsonObject>* Enchantments;
			if (Object->TryGetObjectField(TEXT("enchantments"), Enchantments))
			{
				bool bAdd = false;
				Object->TryGetBoolField(TEXT("add"), bAdd);
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Enchantments)->Values)
				{
					const FName Name(*McCache::Namespaced(Entry.Key));
					const int32 Level = Count(Entry.Value, Random) + (bAdd ? CurrentEnchantments(Stack).FindRef(Name) : 0);
					if (Level > 0)
					{
						AddEnchantment(Stack, Name, Level);
					}
				}
			}
		}
		else if (Type == TEXT("set_potion"))
		{
			FString Id;
			if (Object->TryGetStringField(TEXT("id"), Id))
			{
				const TSharedRef<FJsonObject> Contents = MakeShared<FJsonObject>();
				Contents->SetStringField(TEXT("potion"), McCache::Namespaced(Id));
				Stack.Set(TEXT("minecraft:potion_contents"), MakeShared<FJsonValueObject>(Contents));
			}
		}
		else if (Type == TEXT("set_name"))
		{
			// Written as text; the item_name target is the item's own name, custom_name the one a player gave it.
			FString Target = TEXT("custom_name");
			Object->TryGetStringField(TEXT("target"), Target);
			if (const TSharedPtr<FJsonValue> Name = Object->TryGetField(TEXT("name")))
			{
				Stack.Set(FName(*(TEXT("minecraft:") + McCache::PathOf(Target))), CopyValue(Name));
			}
		}
		else if (Type == TEXT("set_components"))
		{
			const TSharedPtr<FJsonObject>* Components;
			if (Object->TryGetObjectField(TEXT("components"), Components))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Components)->Values)
				{
					// A name with a '!' in front removes the component.
					if (Entry.Key.StartsWith(TEXT("!")))
					{
						Stack.Remove(FName(*McCache::Namespaced(Entry.Key.Mid(1))));
					}
					else
					{
						Stack.Set(FName(*McCache::Namespaced(Entry.Key)), Entry.Value);
					}
				}
				Stack.Normalize();
			}
		}
		else if (Type == TEXT("set_stew_effect"))
		{
			// One of the effects listed, for a duration given in seconds (the item keeps ticks).
			const TArray<TSharedPtr<FJsonValue>>* Effects;
			if (Object->TryGetArrayField(TEXT("effects"), Effects) && Effects->Num() > 0)
			{
				const TSharedPtr<FJsonObject> Effect = (*Effects)[Random.NextInt(Effects->Num())]->AsObject();
				if (Effect.IsValid())
				{
					const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("id"), McCache::Namespaced(Effect->GetStringField(TEXT("type"))));
					Entry->SetNumberField(TEXT("duration"), Count(Effect->TryGetField(TEXT("duration")), Random) * 20);
					Stack.Set(TEXT("minecraft:suspicious_stew_effects"), MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueObject>(Entry) }));
				}
			}
		}
		else if (Type == TEXT("set_ominous_bottle_amplifier"))
		{
			Stack.Set(TEXT("minecraft:ominous_bottle_amplifier"), Number(Count(Object->TryGetField(TEXT("amplifier")), Random)));
		}
		else if (Type == TEXT("set_instrument"))
		{
			FString Options;
			if (Object->TryGetStringField(TEXT("options"), Options) && Options.StartsWith(TEXT("#")))
			{
				TArray<FString> Instruments;
				if (const TSharedPtr<FJsonObject> Tag = McCache::LoadJsonObject(McCache::Data(TEXT("tags/instrument/") + McCache::PathOf(Options.Mid(1)) + TEXT(".json"))))
				{
					const TArray<TSharedPtr<FJsonValue>>* Values;
					if (Tag->TryGetArrayField(TEXT("values"), Values))
					{
						for (const TSharedPtr<FJsonValue>& Value : *Values)
						{
							FString Id;
							if (Value->TryGetString(Id))
							{
								Instruments.Add(Id);
							}
						}
					}
				}
				if (!Instruments.IsEmpty())
				{
					Stack.Set(TEXT("minecraft:instrument"), MakeShared<FJsonValueString>(McCache::Namespaced(Instruments[Random.NextInt(Instruments.Num())])));
				}
			}
		}
		else if (Type == TEXT("filtered"))
		{
			// Only the filter on a map id is understood: a map that doesn't exist yet fails it.
			const TSharedPtr<FJsonObject>* Filter;
			bool bPasses = true;
			if (Object->TryGetObjectField(TEXT("item_filter"), Filter))
			{
				const TSharedPtr<FJsonObject>* Predicates;
				if ((*Filter)->TryGetObjectField(TEXT("predicates"), Predicates))
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Predicates)->Values)
					{
						bPasses &= Stack.Has(FName(*McCache::Namespaced(Entry.Key)));
					}
				}
			}
			Modify(bPasses ? Object->TryGetField(TEXT("on_pass")) : Object->TryGetField(TEXT("on_fail")), Random, Stack);
		}
		else if (Type == TEXT("discard"))
		{
			Stack.Clear();
		}
		// Anything else (maps that point at structures, loot that needs a player) leaves the item as it is.
	}

	void RollTable(const FJsonObject& Table, FMcRandom& Random, TArray<FMcItemStack>& Out, int32 Depth);

	// One entry's items (none for an empty entry or an alternatives entry with no child that passes).
	void GenerateEntry(const FJsonObject& Entry, FMcRandom& Random, TArray<FMcItemStack>& Out, int32 Depth)
	{
		const FString Type = McCache::PathOf(Entry.GetStringField(TEXT("type")));
		const int32 Before = Out.Num();
		if (Type == TEXT("item"))
		{
			FMcItemStack Stack;
			Stack.Item = FName(*McCache::Namespaced(Entry.GetStringField(TEXT("name"))));
			Stack.Count = 1;
			Out.Add(Stack);
		}
		else if (Type == TEXT("alternatives") || Type == TEXT("group") || Type == TEXT("sequence"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Children;
			if (Entry.TryGetArrayField(TEXT("children"), Children))
			{
				for (const TSharedPtr<FJsonValue>& Child : *Children)
				{
					const TSharedPtr<FJsonObject> ChildObject = Child->AsObject();
					if (!ChildObject.IsValid() || !Passes(*ChildObject, Random))
					{
						if (Type == TEXT("sequence"))
						{
							break;
						}
						continue;
					}
					GenerateEntry(*ChildObject, Random, Out, Depth);
					if (Type == TEXT("alternatives"))
					{
						break;
					}
				}
			}
		}
		else if (Type == TEXT("loot_table") && Depth < MaxDepth)
		{
			const TSharedPtr<FJsonValue> Value = Entry.TryGetField(TEXT("value"));
			if (Value.IsValid() && Value->Type == EJson::String)
			{
				if (const TSharedPtr<FJsonObject> Nested = FindTable(Value->AsString()))
				{
					RollTable(*Nested, Random, Out, Depth + 1);
				}
			}
			else if (Value.IsValid() && Value->Type == EJson::Object)
			{
				RollTable(*Value->AsObject(), Random, Out, Depth + 1);
			}
		}
		else if (Type == TEXT("tag"))
		{
			// The items of an item tag: one at random with "expand", otherwise every one of them.
			FString Reference;
			Entry.TryGetStringField(TEXT("items"), Reference);
			const TSet<FName>* Members = FMcItemRegistry::Get().GetTag(FName(*Reference.Replace(TEXT("#"), TEXT(""))));
			if (Members && !Members->IsEmpty())
			{
				TArray<FName> Sorted = Members->Array();
				Sorted.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
				bool bExpand = false;
				Entry.TryGetBoolField(TEXT("expand"), bExpand);
				if (bExpand)
				{
					Sorted = { Sorted[Random.NextInt(Sorted.Num())] };
				}
				for (const FName& Item : Sorted)
				{
					FMcItemStack Stack;
					Stack.Item = Item;
					Stack.Count = 1;
					Out.Add(Stack);
				}
			}
		}
		// A dynamic entry (a shulker box or decorated pot giving back what it holds) is read here and gives nothing:
		// stacks don't carry block entity contents yet, so there is nothing to hand back when such a block is broken.
		for (int32 Index = Before; Index < Out.Num(); ++Index)
		{
			Modify(Entry.TryGetField(TEXT("modifier")), Random, Out[Index]);
		}
	}

	void RollTable(const FJsonObject& Table, FMcRandom& Random, TArray<FMcItemStack>& Out, int32 Depth)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pools;
		if (!Table.TryGetArrayField(TEXT("pools"), Pools))
		{
			return;
		}
		const int32 TableStart = Out.Num();
		for (const TSharedPtr<FJsonValue>& PoolValue : *Pools)
		{
			const TSharedPtr<FJsonObject> Pool = PoolValue->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Entries;
			if (!Pool.IsValid() || !Passes(*Pool, Random) || !Pool->TryGetArrayField(TEXT("entries"), Entries))
			{
				continue;
			}
			const int32 PoolStart = Out.Num();
			// Bonus rolls are multiplied by the player's luck, which nothing has at generation.
			const int32 Rolls = Count(Pool->TryGetField(TEXT("rolls")), Random);
			for (int32 Roll = 0; Roll < Rolls; ++Roll)
			{
				// The entries whose conditions let them through, and the total of their weights.
				TArray<const FJsonObject*, TInlineAllocator<32>> Open;
				TArray<int32, TInlineAllocator<32>> Weights;
				int32 Total = 0;
				for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
				{
					const TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
					if (!Entry.IsValid() || !Passes(*Entry, Random))
					{
						continue;
					}
					double Weight = 1.0;
					Entry->TryGetNumberField(TEXT("weight"), Weight);
					Open.Add(Entry.Get());
					Weights.Add(FMath::Max(0, static_cast<int32>(Weight)));
					Total += Weights.Last();
				}
				if (Total <= 0)
				{
					continue;
				}
				int32 Pick = Random.NextInt(Total);
				for (int32 Index = 0; Index < Open.Num(); ++Index)
				{
					Pick -= Weights[Index];
					if (Pick < 0)
					{
						GenerateEntry(*Open[Index], Random, Out, Depth);
						break;
					}
				}
			}
			for (int32 Index = PoolStart; Index < Out.Num(); ++Index)
			{
				Modify(Pool->TryGetField(TEXT("modifier")), Random, Out[Index]);
			}
		}
		for (int32 Index = TableStart; Index < Out.Num(); ++Index)
		{
			Modify(Table.TryGetField(TEXT("modifier")), Random, Out[Index]);
		}
	}
}

void McLoot::ApplyModifier(const TSharedPtr<FJsonValue>& Modifier, FMcRandom& Random, FMcItemStack& Stack)
{
	Modify(Modifier, Random, Stack);
}

bool McLoot::Roll(const FString& Table, FMcRandom& Random, TArray<FMcItemStack>& Out)
{
	const TSharedPtr<FJsonObject> Json = FindTable(Table);
	if (!Json.IsValid())
	{
		return false;
	}
	FMcItemRegistry::Get().EnsureLoaded();
	const int32 Before = Out.Num();
	RollTable(*Json, Random, Out, 0);
	// Stacks a function emptied or discarded are not loot.
	for (int32 Index = Out.Num() - 1; Index >= Before; --Index)
	{
		if (Out[Index].IsEmpty())
		{
			Out.RemoveAt(Index);
		}
	}
	return true;
}

bool McLoot::RollEntity(const FString& Table, bool bCaptain, FMcRandom& Random, TArray<FMcItemStack>& Out)
{
	const int32 Previous = GCaptain;
	GCaptain = bCaptain ? 1 : 0;
	const bool bRolled = Roll(Table, Random, Out);
	GCaptain = Previous;
	return bRolled;
}

bool McLoot::RollBlock(const FBlockContext& Context, FMcRandom& Random, TArray<FMcItemStack>& Out)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Registry.IsValidState(Context.State))
	{
		return false;
	}
	const FString Table = TEXT("blocks/") + McCache::PathOf(Registry.GetBlockOf(Context.State).Name.ToString());
	if (!IFileManager::Get().FileExists(*McCache::Data(TEXT("loot_table/") + Table + TEXT(".json"))))
	{
		// Air, bedrock and the like drop nothing; no warning for that.
		return false;
	}
	const FBlockContext* const Previous = GBlock;
	GBlock = &Context;
	const bool bRolled = Roll(Table, Random, Out);
	GBlock = Previous;
	return bRolled;
}

bool McLoot::Fill(const FString& Table, uint64 Seed, FMcBlockEntity& Container)
{
	FMcRandom Random(Seed);
	TArray<FMcItemStack> Rolled;
	if (!Roll(Table, Random, Rolled))
	{
		return false;
	}
	// Stacks over the limit split; empty ones go.
	TArray<FMcItemStack> Stacks;
	for (const FMcItemStack& Stack : Rolled)
	{
		for (int32 Left = Stack.Count; Left > 0; Left -= Stack.GetMaxStackSize())
		{
			FMcItemStack Part = Stack;
			Part.Count = FMath::Min(Left, Stack.GetMaxStackSize());
			Stacks.Add(Part);
		}
	}
	TArray<int32> Free;
	for (int32 Slot = 0; Slot < Container.Slots.Num(); ++Slot)
	{
		Container.Slots[Slot] = FMcItemStack();
		Free.Add(Slot);
	}
	for (const FMcItemStack& Stack : Stacks)
	{
		if (Free.IsEmpty())
		{
			break;
		}
		// A random free slot: swap it to the end and take it.
		Free.Swap(Random.NextInt(Free.Num()), Free.Num() - 1);
		Container.Slots[Free.Pop(EAllowShrinking::No)] = Stack;
	}
	Container.LootTable = FName(*McCache::Namespaced(Table));
	Container.LootSeed = static_cast<int64>(Seed);
	return true;
}

int32 McBlockEntities::ContainerSlots(FName Block)
{
	static const FName Chest("minecraft:chest");
	static const FName TrappedChest("minecraft:trapped_chest");
	static const FName Barrel("minecraft:barrel");
	static const FName Dispenser("minecraft:dispenser");
	static const FName Dropper("minecraft:dropper");
	static const FName Hopper("minecraft:hopper");
	if (Block == Chest || Block == TrappedChest || Block == Barrel)
	{
		return 27;
	}
	if (Block == Dispenser || Block == Dropper)
	{
		return 9;
	}
	return Block == Hopper ? 5 : 0;
}

FMcBlockEntity McBlockEntities::MakeLootContainer(FName Block, const FString& Table, FMcRandom& Random)
{
	FMcBlockEntity Entity = FMcBlockEntity::MakeContainer(ContainerSlots(Block));
	McLoot::Fill(Table, Random.NextLong(), Entity);
	return Entity;
}

TOptional<FMcBlockEntity> McBlockEntities::FromTemplate(const FMcNbt& Nbt, FName Block, FMcRandom& Random)
{
	if (const int32 Slots = ContainerSlots(Block))
	{
		FMcBlockEntity Entity = FMcBlockEntity::MakeContainer(Slots);
		const FString Table = Nbt.GetString(TEXT("LootTable"));
		if (!Table.IsEmpty())
		{
			// The template may fix the seed; otherwise the stream gives one.
			const int64 Fixed = Nbt.GetInt(TEXT("LootTableSeed"));
			McLoot::Fill(Table, Fixed != 0 ? static_cast<uint64>(Fixed) : Random.NextLong(), Entity);
		}
		else if (const FMcNbt* Items = Nbt.Find(TEXT("Items")))
		{
			for (const FMcNbt& Item : Items->Values)
			{
				const int32 Slot = static_cast<int32>(Item.GetInt(TEXT("Slot"), -1));
				if (Slot >= 0 && Slot < Slots)
				{
					Entity.Slots[Slot].Item = FName(*McCache::Namespaced(Item.GetString(TEXT("id"))));
					Entity.Slots[Slot].Count = static_cast<int32>(Item.GetInt(TEXT("count"), Item.GetInt(TEXT("Count"), 1)));
				}
			}
		}
		return Entity;
	}
	if (Block == FName("minecraft:spawner"))
	{
		FString Id;
		if (const FMcNbt* Spawn = Nbt.Find(TEXT("SpawnData")))
		{
			if (const FMcNbt* Inner = Spawn->Find(TEXT("entity")))
			{
				Id = Inner->GetString(TEXT("id"));
			}
		}
		if (Id.IsEmpty())
		{
			if (const FMcNbt* Potentials = Nbt.Find(TEXT("SpawnPotentials")))
			{
				if (Potentials->Num() > 0)
				{
					if (const FMcNbt* Data = Potentials->Values[0].Find(TEXT("data")))
					{
						if (const FMcNbt* Inner = Data->Find(TEXT("entity")))
						{
							Id = Inner->GetString(TEXT("id"));
						}
					}
				}
			}
		}
		return FMcBlockEntity::MakeSpawner(Id.IsEmpty() ? FName() : FName(*McCache::Namespaced(Id)));
	}
	return {};
}
