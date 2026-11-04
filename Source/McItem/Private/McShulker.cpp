#include "McShulker.h"
#include "Dom/JsonObject.h"

namespace McShulker
{
	bool IsBox(FName Item)
	{
		return Item.ToString().EndsWith(TEXT("shulker_box"));
	}

	TSharedPtr<FJsonValue> Pack(const TArray<FMcItemStack>& Slots)
	{
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (int32 Slot = 0; Slot < Slots.Num() && Slot < NumSlots; ++Slot)
		{
			const FMcItemStack& Stack = Slots[Slot];
			if (Stack.IsEmpty())
			{
				continue;
			}
			const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("id"), Stack.Item.ToString());
			Item->SetNumberField(TEXT("count"), Stack.Count);
			if (!Stack.Components.IsEmpty())
			{
				const TSharedRef<FJsonObject> Components = MakeShared<FJsonObject>();
				for (const TPair<FName, TSharedPtr<FJsonValue>>& Pair : Stack.Components)
				{
					Components->SetField(Pair.Key.ToString(), Pair.Value.IsValid() ? Pair.Value : MakeShared<FJsonValueNull>());
				}
				Item->SetObjectField(TEXT("components"), Components);
			}
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("slot"), Slot);
			Entry->SetObjectField(TEXT("item"), Item);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (Entries.IsEmpty())
		{
			return nullptr;
		}
		return MakeShared<FJsonValueArray>(Entries);
	}

	TArray<FMcItemStack> Unpack(const FMcItemStack& Box)
	{
		TArray<FMcItemStack> Slots;
		Slots.SetNum(NumSlots);
		const TSharedPtr<FJsonValue> Value = Box.Get(TEXT("minecraft:container"));
		if (!Value.IsValid() || Value->Type != EJson::Array)
		{
			return Slots;
		}
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			const TSharedPtr<FJsonObject>* Item = nullptr;
			double Slot = -1.0;
			FString Id;
			if (!Entry->TryGetObject(Object) || !(*Object)->TryGetNumberField(TEXT("slot"), Slot) || !(*Object)->TryGetObjectField(TEXT("item"), Item)
				|| !(*Item)->TryGetStringField(TEXT("id"), Id) || Slot < 0 || Slot >= NumSlots)
			{
				continue;
			}
			FMcItemStack Stack(FName(*Id), 1);
			double Count = 1.0;
			if ((*Item)->TryGetNumberField(TEXT("count"), Count))
			{
				Stack.Count = static_cast<int32>(Count);
			}
			const TSharedPtr<FJsonObject>* Components = nullptr;
			if ((*Item)->TryGetObjectField(TEXT("components"), Components))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Components)->Values)
				{
					Stack.Set(FName(*Pair.Key), Pair.Value);
				}
			}
			Slots[static_cast<int32>(Slot)] = Stack;
		}
		return Slots;
	}
}
