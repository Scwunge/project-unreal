#include "McItemStack.h"
#include "Dom/JsonObject.h"
#include "McCache.h"
#include "McEnchantEffects.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
	// A stack in a chest holds a few components at most; anything bigger in a save is damage.
	constexpr int32 MaxComponents = 256;
	constexpr int32 MaxComponentText = 1 << 20;

	void SerializeName(FArchive& Ar, FName& Name)
	{
		FString Text = Ar.IsSaving() ? (Name.IsNone() ? FString() : Name.ToString()) : FString();
		Ar << Text;
		if (Ar.IsLoading())
		{
			Name = Text.IsEmpty() ? FName() : FName(*Text);
		}
	}

	bool IsRemoved(const TSharedPtr<FJsonValue>& Value)
	{
		return Value.IsValid() && Value->Type == EJson::Null;
	}
}

namespace McComponents
{
	bool ValuesEqual(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return A.IsValid() == B.IsValid();
		}
		if (A->Type != B->Type)
		{
			return false;
		}
		switch (A->Type)
		{
		case EJson::Null:
			return true;
		case EJson::Number:
			return A->AsNumber() == B->AsNumber();
		case EJson::String:
			return A->AsString() == B->AsString();
		case EJson::Boolean:
			return A->AsBool() == B->AsBool();
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& Left = A->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& Right = B->AsArray();
			if (Left.Num() != Right.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < Left.Num(); ++Index)
			{
				if (!ValuesEqual(Left[Index], Right[Index]))
				{
					return false;
				}
			}
			return true;
		}
		case EJson::Object:
		{
			const auto& Left = A->AsObject()->Values;
			const auto& Right = B->AsObject()->Values;
			if (Left.Num() != Right.Num())
			{
				return false;
			}
			for (const auto& Entry : Left)
			{
				const TSharedPtr<FJsonValue>* Other = Right.Find(Entry.Key);
				if (!Other || !ValuesEqual(Entry.Value, *Other))
				{
					return false;
				}
			}
			return true;
		}
		default:
			return false;
		}
	}

	bool MapsEqual(const FMcComponents& A, const FMcComponents& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const TPair<FName, TSharedPtr<FJsonValue>>& Entry : A)
		{
			const TSharedPtr<FJsonValue>* Other = B.Find(Entry.Key);
			if (!Other || !ValuesEqual(Entry.Value, *Other))
			{
				return false;
			}
		}
		return true;
	}

	FString ToText(const TSharedPtr<FJsonValue>& Value)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		// The writer wants a key or an array element around a bare value, so a value rides in a one-element array.
		Writer->WriteArrayStart();
		FJsonSerializer::Serialize(Value.IsValid() ? Value : MakeShared<FJsonValueNull>(), TEXT(""), Writer, false);
		Writer->WriteArrayEnd();
		Writer->Close();
		return Text;
	}

	TSharedPtr<FJsonValue> FromText(const FString& Text)
	{
		TArray<TSharedPtr<FJsonValue>> Wrapper;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Wrapper) || Wrapper.Num() != 1)
		{
			return nullptr;
		}
		return Wrapper[0];
	}
}

FMcItemStack::FMcItemStack() = default;
FMcItemStack::~FMcItemStack() = default;
FMcItemStack::FMcItemStack(const FMcItemStack& Other) = default;
FMcItemStack::FMcItemStack(FMcItemStack&& Other) = default;
FMcItemStack& FMcItemStack::operator=(const FMcItemStack& Other) = default;
FMcItemStack& FMcItemStack::operator=(FMcItemStack&& Other) = default;

TSharedPtr<FJsonValue> FMcItemStack::Get(FName Component) const
{
	if (const TSharedPtr<FJsonValue>* Own = Components.Find(Component))
	{
		return IsRemoved(*Own) ? nullptr : *Own;
	}
	return FMcItemRegistry::Get().GetDefault(Item, Component);
}

void FMcItemStack::Remove(FName Component)
{
	if (FMcItemRegistry::Get().GetDefault(Item, Component).IsValid())
	{
		Components.Add(Component, MakeShared<FJsonValueNull>());
	}
	else
	{
		Components.Remove(Component);
	}
}

void FMcItemStack::Normalize()
{
	const FMcItemRegistry& Registry = FMcItemRegistry::Get();
	for (auto It = Components.CreateIterator(); It; ++It)
	{
		const TSharedPtr<FJsonValue> Default = Registry.GetDefault(Item, It.Key());
		const bool bRemoved = IsRemoved(It.Value());
		if (bRemoved ? !Default.IsValid() : McComponents::ValuesEqual(It.Value(), Default))
		{
			It.RemoveCurrent();
		}
	}
}

int32 FMcItemStack::GetMaxStackSize() const
{
	const TSharedPtr<FJsonValue> Value = Get(TEXT("minecraft:max_stack_size"));
	return Value.IsValid() && Value->Type == EJson::Number ? FMath::Max(1, static_cast<int32>(Value->AsNumber())) : 64;
}

int32 FMcItemStack::GetMaxDamage() const
{
	const TSharedPtr<FJsonValue> Value = Get(TEXT("minecraft:max_damage"));
	return Value.IsValid() && Value->Type == EJson::Number ? static_cast<int32>(Value->AsNumber()) : 0;
}

int32 FMcItemStack::GetDamage() const
{
	const TSharedPtr<FJsonValue> Value = Get(TEXT("minecraft:damage"));
	return Value.IsValid() && Value->Type == EJson::Number ? FMath::Clamp(static_cast<int32>(Value->AsNumber()), 0, GetMaxDamage()) : 0;
}

void FMcItemStack::SetDamage(int32 Damage)
{
	Set(TEXT("minecraft:damage"), MakeShared<FJsonValueNumber>(FMath::Clamp(Damage, 0, GetMaxDamage())));
	Normalize();
}

bool FMcItemStack::HurtAndBreak(int32 Amount)
{
	if (!IsDamageable() || Amount <= 0)
	{
		return false;
	}
	// Unbreaking, from the enchantment's own data: each point of wear can be saved.
	static FRandomStream WearRandom(0x7ec4);
	Amount = McEnchantEffects::GetItemDamage(*this, Amount, WearRandom);
	if (Amount <= 0)
	{
		return false;
	}
	const int32 Damage = GetDamage() + Amount;
	if (Damage >= GetMaxDamage())
	{
		return true;
	}
	SetDamage(Damage);
	return false;
}

TMap<FName, int32> FMcItemStack::GetEnchantments() const
{
	TMap<FName, int32> Result;
	const TSharedPtr<FJsonValue> Value = Get(TEXT("minecraft:enchantments"));
	if (Value.IsValid() && Value->Type == EJson::Object)
	{
		for (const auto& Entry : Value->AsObject()->Values)
		{
			if (Entry.Value.IsValid() && Entry.Value->Type == EJson::Number && Entry.Value->AsNumber() > 0)
			{
				Result.Add(FName(*McCache::Namespaced(Entry.Key)), static_cast<int32>(Entry.Value->AsNumber()));
			}
		}
	}
	return Result;
}

void FMcItemStack::SetEnchantments(const TMap<FName, int32>& Enchantments)
{
	if (Enchantments.IsEmpty())
	{
		Reset(TEXT("minecraft:enchantments"));
		return;
	}
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	for (const TPair<FName, int32>& Entry : Enchantments)
	{
		Object->SetNumberField(Entry.Key.ToString(), Entry.Value);
	}
	Set(TEXT("minecraft:enchantments"), MakeShared<FJsonValueObject>(Object));
	Normalize();
}

int32 FMcItemStack::GetEnchantmentLevel(FName Enchantment) const
{
	const int32* Level = GetEnchantments().Find(Enchantment);
	return Level ? *Level : 0;
}

FString FMcItemStack::GetCustomName() const
{
	const TSharedPtr<FJsonValue> Value = Get(TEXT("minecraft:custom_name"));
	FString Text;
	if (!Value.IsValid())
	{
		return Text;
	}
	if (!Value->TryGetString(Text) && Value->Type == EJson::Object)
	{
		Value->AsObject()->TryGetStringField(TEXT("text"), Text);
	}
	return Text;
}

void FMcItemStack::SetCustomName(const FString& Name)
{
	if (Name.IsEmpty())
	{
		Remove(TEXT("minecraft:custom_name"));
	}
	else
	{
		Set(TEXT("minecraft:custom_name"), MakeShared<FJsonValueString>(Name));
	}
}

bool FMcItemStack::IsSameItemSameComponents(const FMcItemStack& Other) const
{
	if (Item != Other.Item)
	{
		return false;
	}
	// The effective components: a patch entry that spells out a default is the same as having none.
	TSet<FName> Keys;
	Components.GetKeys(Keys);
	for (const TPair<FName, TSharedPtr<FJsonValue>>& Entry : Other.Components)
	{
		Keys.Add(Entry.Key);
	}
	for (const FName Key : Keys)
	{
		if (!McComponents::ValuesEqual(Get(Key), Other.Get(Key)))
		{
			return false;
		}
	}
	return true;
}

int32 FMcItemStack::GetSpaceFor(const FMcItemStack& Other, int32 SlotLimit) const
{
	if (Other.IsEmpty())
	{
		return 0;
	}
	if (IsEmpty())
	{
		return FMath::Min(Other.Count, FMath::Min(Other.GetMaxStackSize(), SlotLimit));
	}
	if (!IsSameItemSameComponents(Other))
	{
		return 0;
	}
	return FMath::Clamp(FMath::Min(GetMaxStackSize(), SlotLimit) - Count, 0, Other.Count);
}

FMcItemStack FMcItemStack::Split(int32 Amount)
{
	FMcItemStack Result = *this;
	Result.Count = FMath::Clamp(Amount, 0, Count);
	Count -= Result.Count;
	if (Count <= 0)
	{
		Clear();
	}
	return Result;
}

bool FMcItemStack::Serialize(FArchive& Ar)
{
	SerializeName(Ar, Item);
	Ar << Count;
	int32 NumComponents = Components.Num();
	Ar << NumComponents;
	if (Ar.IsSaving())
	{
		// In name order, so equal stacks save to equal bytes.
		TArray<FName> Names;
		Components.GetKeys(Names);
		Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		for (FName Name : Names)
		{
			SerializeName(Ar, Name);
			FString Text = McComponents::ToText(Components[Name]);
			Ar << Text;
		}
		return !Ar.IsError();
	}
	if (NumComponents < 0 || NumComponents > MaxComponents)
	{
		return false;
	}
	Components.Reset();
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		FName Name;
		SerializeName(Ar, Name);
		FString Text;
		Ar << Text;
		if (Ar.IsError() || Name.IsNone() || Text.Len() > MaxComponentText)
		{
			return false;
		}
		const TSharedPtr<FJsonValue> Value = McComponents::FromText(Text);
		if (!Value.IsValid())
		{
			return false;
		}
		Components.Add(Name, Value);
	}
	return !Ar.IsError();
}
