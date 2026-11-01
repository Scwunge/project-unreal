#include "McBlockEntity.h"
#include "Serialization/Archive.h"

namespace
{
	// A chest holds a few dozen slots at most; anything bigger in a save is damage.
	constexpr int32 MaxSlots = 256;
	// The editor stops a line at the sign's width, a few dozen characters; anything far longer is damage.
	constexpr int32 MaxSignLineLength = 384;

	void SerializeName(FArchive& Ar, FName& Name)
	{
		FString Text = Ar.IsSaving() ? (Name.IsNone() ? FString() : Name.ToString()) : FString();
		Ar << Text;
		if (Ar.IsLoading())
		{
			Name = Text.IsEmpty() ? FName() : FName(*Text);
		}
	}
}

FMcBlockEntity FMcBlockEntity::MakeContainer(int32 NumSlots)
{
	FMcBlockEntity Result;
	Result.Kind = EMcBlockEntityKind::Container;
	Result.Slots.SetNum(NumSlots);
	return Result;
}

FMcBlockEntity FMcBlockEntity::MakeSpawner(FName InEntityId)
{
	FMcBlockEntity Result;
	Result.Kind = EMcBlockEntityKind::Spawner;
	Result.EntityId = InEntityId;
	return Result;
}

FMcBlockEntity FMcBlockEntity::MakeSign()
{
	FMcBlockEntity Result;
	Result.Kind = EMcBlockEntityKind::Sign;
	return Result;
}

FMcBlockEntity FMcBlockEntity::MakeLectern(const FMcItemStack& Book)
{
	FMcBlockEntity Result;
	Result.Kind = EMcBlockEntityKind::Lectern;
	Result.Slots.Add(Book);
	return Result;
}

FMcBlockEntity FMcBlockEntity::MakePot(const TArray<FName>& Sides)
{
	FMcBlockEntity Result;
	Result.Kind = EMcBlockEntityKind::Pot;
	for (const FName Side : Sides)
	{
		Result.Slots.Add(FMcItemStack(Side, 1));
	}
	return Result;
}

bool FMcSignSide::HasText() const
{
	for (const FString& Line : Lines)
	{
		if (!Line.IsEmpty())
		{
			return true;
		}
	}
	return false;
}

bool FMcSignSide::operator==(const FMcSignSide& Other) const
{
	for (int32 Line = 0; Line < NumLines; ++Line)
	{
		if (!Lines[Line].Equals(Other.Lines[Line], ESearchCase::CaseSensitive))
		{
			return false;
		}
	}
	return Color == Other.Color && bGlowing == Other.bGlowing;
}

bool FMcBlockEntity::Serialize(FArchive& Ar)
{
	uint8 KindByte = static_cast<uint8>(Kind);
	Ar << KindByte;
	if (Ar.IsLoading())
	{
		if (KindByte > static_cast<uint8>(EMcBlockEntityKind::Pot))
		{
			return false;
		}
		Kind = static_cast<EMcBlockEntityKind>(KindByte);
	}
	int32 NumSlots = Slots.Num();
	Ar << NumSlots;
	if (Ar.IsLoading())
	{
		if (NumSlots < 0 || NumSlots > MaxSlots)
		{
			return false;
		}
		Slots.SetNum(NumSlots);
	}
	for (FMcItemStack& Slot : Slots)
	{
		if (!Slot.Serialize(Ar))
		{
			return false;
		}
	}
	SerializeName(Ar, LootTable);
	Ar << LootSeed;
	SerializeName(Ar, EntityId);
	// Only furnaces and brewing stands carry data, and it comes last, so saves from before it read the same.
	if (Kind == EMcBlockEntityKind::Furnace || Kind == EMcBlockEntityKind::BrewingStand)
	{
		int32 NumBytes = Data.Num();
		Ar << NumBytes;
		if (Ar.IsLoading())
		{
			if (NumBytes < 0 || NumBytes > 65536)
			{
				return false;
			}
			Data.SetNumUninitialized(NumBytes);
		}
		if (NumBytes > 0)
		{
			Ar.Serialize(Data.GetData(), NumBytes);
		}
	}
	// A sign's text, likewise only for signs and at the end: saves from before signs had text have no sign entities.
	if (Kind == EMcBlockEntityKind::Sign)
	{
		for (FMcSignSide& Side : SignSides)
		{
			for (FString& Line : Side.Lines)
			{
				Ar << Line;
				if (Ar.IsLoading() && Line.Len() > MaxSignLineLength)
				{
					return false;
				}
			}
			SerializeName(Ar, Side.Color);
			if (Side.Color.IsNone())
			{
				Side.Color = TEXT("black");
			}
			Ar << Side.bGlowing;
		}
		Ar << bWaxed;
	}
	if (Kind == EMcBlockEntityKind::Lectern)
	{
		Ar << Page;
	}
	return !Ar.IsError();
}

bool FMcBlockEntity::operator==(const FMcBlockEntity& Other) const
{
	return Kind == Other.Kind && Slots == Other.Slots && LootTable == Other.LootTable && LootSeed == Other.LootSeed && EntityId == Other.EntityId && Data == Other.Data
		&& SignSides[0] == Other.SignSides[0] && SignSides[1] == Other.SignSides[1] && bWaxed == Other.bWaxed && Page == Other.Page;
}
