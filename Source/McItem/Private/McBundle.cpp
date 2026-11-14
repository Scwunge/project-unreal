#include "McBundle.h"
#include "Dom/JsonObject.h"
#include "McShulker.h"

namespace
{
	constexpr double Epsilon = 1.0e-9;

	TSharedPtr<FJsonValue> ToJson(const FMcItemStack& Stack)
	{
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
		return MakeShared<FJsonValueObject>(Item);
	}

	bool FromJson(const TSharedPtr<FJsonValue>& Value, FMcItemStack& Out)
	{
		const TSharedPtr<FJsonObject>* Item = nullptr;
		FString Id;
		if (!Value.IsValid() || !Value->TryGetObject(Item) || !(*Item)->TryGetStringField(TEXT("id"), Id))
		{
			return false;
		}
		Out = FMcItemStack(FName(*Id), 1);
		double Count = 1.0;
		if ((*Item)->TryGetNumberField(TEXT("count"), Count))
		{
			Out.Count = static_cast<int32>(Count);
		}
		const TSharedPtr<FJsonObject>* Components = nullptr;
		if ((*Item)->TryGetObjectField(TEXT("components"), Components))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>> Pair : (*Components)->Values)
			{
				Out.Set(FName(*Pair.Key), Pair.Value);
			}
		}
		return !Out.IsEmpty();
	}

	double ContentsWeight(const TArray<FMcItemStack>& Contents)
	{
		double Sum = 0.0;
		for (const FMcItemStack& Stack : Contents)
		{
			Sum += McBundle::GetItemWeight(Stack) * Stack.Count;
		}
		return Sum;
	}
}

namespace McBundle
{
	bool IsBundle(FName Item)
	{
		const FString Name = Item.ToString();
		return Name == TEXT("minecraft:bundle") || Name.EndsWith(TEXT("_bundle"));
	}

	TArray<FMcItemStack> GetContents(const FMcItemStack& Bundle)
	{
		TArray<FMcItemStack> Out;
		const TSharedPtr<FJsonValue> Value = Bundle.Get(TEXT("minecraft:bundle_contents"));
		if (Value.IsValid() && Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				FMcItemStack Stack;
				if (FromJson(Entry, Stack))
				{
					Out.Add(Stack);
				}
			}
		}
		return Out;
	}

	void SetContents(FMcItemStack& Bundle, const TArray<FMcItemStack>& Contents)
	{
		if (Contents.IsEmpty())
		{
			Bundle.Reset(TEXT("minecraft:bundle_contents"));
			return;
		}
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FMcItemStack& Stack : Contents)
		{
			Entries.Add(ToJson(Stack));
		}
		Bundle.Set(TEXT("minecraft:bundle_contents"), MakeShared<FJsonValueArray>(Entries));
	}

	double GetItemWeight(const FMcItemStack& Stack)
	{
		if (IsBundle(Stack.Item))
		{
			return 1.0 / 16.0 + ContentsWeight(GetContents(Stack));
		}
		const FString Name = Stack.Item.ToString();
		if ((Name == TEXT("minecraft:beehive") || Name == TEXT("minecraft:bee_nest")) && Stack.Has(TEXT("minecraft:bees")))
		{
			return 1.0;
		}
		return 1.0 / FMath::Max(1, Stack.GetMaxStackSize());
	}

	double GetFullness(const FMcItemStack& Bundle)
	{
		return FMath::Min(1.0, ContentsWeight(GetContents(Bundle)));
	}

	bool CanBundle(const FMcItemStack& Stack)
	{
		return !Stack.IsEmpty() && !McShulker::IsBox(Stack.Item);
	}

	int32 Insert(FMcItemStack& Bundle, FMcItemStack& Stack)
	{
		if (!CanBundle(Stack) || !IsBundle(Bundle.Item))
		{
			return 0;
		}
		TArray<FMcItemStack> Contents = GetContents(Bundle);
		const double Weight = GetItemWeight(Stack);
		const int32 Fit = FMath::Min(Stack.Count, FMath::FloorToInt32((1.0 - ContentsWeight(Contents) + Epsilon) / Weight));
		if (Fit <= 0)
		{
			return 0;
		}
		// The same stack already in is added to and moved to the front.
		int32 Found = INDEX_NONE;
		for (int32 Index = 0; Index < Contents.Num(); ++Index)
		{
			if (Contents[Index].IsSameItemSameComponents(Stack) && Contents[Index].Count + Fit <= Contents[Index].GetMaxStackSize())
			{
				Found = Index;
				break;
			}
		}
		FMcItemStack Moved = Stack;
		Moved.Count = Fit;
		if (Found != INDEX_NONE)
		{
			Moved.Count += Contents[Found].Count;
			Contents.RemoveAt(Found);
		}
		Contents.Insert(Moved, 0);
		SetContents(Bundle, Contents);
		Stack.Count -= Fit;
		if (Stack.Count <= 0)
		{
			Stack.Clear();
		}
		return Fit;
	}

	FMcItemStack RemoveNewest(FMcItemStack& Bundle)
	{
		TArray<FMcItemStack> Contents = GetContents(Bundle);
		if (Contents.IsEmpty())
		{
			return FMcItemStack();
		}
		const FMcItemStack Out = Contents[0];
		Contents.RemoveAt(0);
		SetContents(Bundle, Contents);
		return Out;
	}

	FMcItemStack RemoveAt(FMcItemStack& Bundle, int32 Index)
	{
		TArray<FMcItemStack> Contents = GetContents(Bundle);
		if (!Contents.IsValidIndex(Index))
		{
			return RemoveNewest(Bundle);
		}
		const FMcItemStack Out = Contents[Index];
		Contents.RemoveAt(Index);
		SetContents(Bundle, Contents);
		return Out;
	}
}
