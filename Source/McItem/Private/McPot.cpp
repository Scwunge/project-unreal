#include "McPot.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const FName Brick(TEXT("minecraft:brick"));
}

const TCHAR* McPot::GetSideName(int32 Side)
{
	static const TCHAR* const Names[NumSides] = { TEXT("back"), TEXT("left"), TEXT("right"), TEXT("front") };
	return Names[FMath::Clamp(Side, 0, NumSides - 1)];
}

TArray<FName> McPot::GetSides(const FMcItemStack& Pot)
{
	TArray<FName> Sides;
	Sides.Init(Brick, NumSides);
	const TSharedPtr<FJsonValue> Value = Pot.Get(TEXT("minecraft:pot_decorations"));
	const TSharedPtr<FJsonObject>* Object;
	if (!Value.IsValid() || !Value->TryGetObject(Object))
	{
		return Sides;
	}
	for (int32 Side = 0; Side < NumSides; ++Side)
	{
		const TSharedPtr<FJsonObject>* Entry;
		FString Id;
		if ((*Object)->TryGetObjectField(GetSideName(Side), Entry) && (*Entry)->TryGetStringField(TEXT("id"), Id) && !Id.IsEmpty())
		{
			Sides[Side] = FName(*Id);
		}
	}
	return Sides;
}

void McPot::SetSides(FMcItemStack& Pot, const TArray<FName>& Sides)
{
	bool bPlain = true;
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	for (int32 Side = 0; Side < NumSides; ++Side)
	{
		const FName Item = Sides.IsValidIndex(Side) && !Sides[Side].IsNone() ? Sides[Side] : Brick;
		bPlain &= Item == Brick;
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Item.ToString());
		Object->SetObjectField(GetSideName(Side), Entry);
	}
	if (bPlain)
	{
		Pot.Components.Remove(TEXT("minecraft:pot_decorations"));
		return;
	}
	Pot.Set(TEXT("minecraft:pot_decorations"), MakeShared<FJsonValueObject>(Object));
}

FString McPot::GetPattern(FName Side)
{
	FString Name = Side.ToString();
	if (Name.RemoveFromStart(TEXT("minecraft:")) && Name.EndsWith(TEXT("_pottery_sherd")))
	{
		Name.RemoveFromEnd(TEXT("_sherd"));
		return TEXT("entity/decorated_pot/") + Name + TEXT("_pattern");
	}
	return TEXT("entity/decorated_pot/decorated_pot_side");
}
