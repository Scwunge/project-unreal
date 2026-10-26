#include "McLoot.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "McCache.h"
#include "McGen.h"
#include "McNbt.h"
#include "McRandom.h"
#include "Misc/ScopeLock.h"

namespace
{
	constexpr int32 MaxDepth = 8;
	constexpr int32 MaxStack = 64;

	FCriticalSection TablesLock;
	TMap<FString, TSharedPtr<FJsonObject>> Tables;

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
			return 1;
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

	// The effects of a modifier (or list of them) on a stack that matter here: set_count.
	void Modify(const TSharedPtr<FJsonValue>& Modifier, FMcRandom& Random, FMcItemStack& Stack)
	{
		if (!Modifier.IsValid())
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
		if (McCache::PathOf(Object->GetStringField(TEXT("type"))) != TEXT("set_count") || !Passes(*Object, Random))
		{
			return;
		}
		const int32 Amount = Count(Object->TryGetField(TEXT("count")), Random);
		bool bAdd = false;
		Object->TryGetBoolField(TEXT("add"), bAdd);
		Stack.Count = bAdd ? Stack.Count + Amount : Amount;
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
		// Tag and dynamic entries give nothing here.
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

bool McLoot::Roll(const FString& Table, FMcRandom& Random, TArray<FMcItemStack>& Out)
{
	const TSharedPtr<FJsonObject> Json = FindTable(Table);
	if (!Json.IsValid())
	{
		return false;
	}
	RollTable(*Json, Random, Out, 0);
	return true;
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
		for (int32 Left = Stack.Count; Left > 0; Left -= MaxStack)
		{
			Stacks.Add({ Stack.Item, FMath::Min(Left, MaxStack) });
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
