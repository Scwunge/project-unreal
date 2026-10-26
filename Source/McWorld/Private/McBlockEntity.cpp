#include "McBlockEntity.h"
#include "Serialization/Archive.h"

namespace
{
	// A chest holds a few dozen slots at most; anything bigger in a save is damage.
	constexpr int32 MaxSlots = 256;

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

bool FMcBlockEntity::Serialize(FArchive& Ar)
{
	uint8 KindByte = static_cast<uint8>(Kind);
	Ar << KindByte;
	if (Ar.IsLoading())
	{
		if (KindByte > static_cast<uint8>(EMcBlockEntityKind::Spawner))
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
	return !Ar.IsError();
}

bool FMcBlockEntity::operator==(const FMcBlockEntity& Other) const
{
	return Kind == Other.Kind && Slots == Other.Slots && LootTable == Other.LootTable && LootSeed == Other.LootSeed && EntityId == Other.EntityId;
}
