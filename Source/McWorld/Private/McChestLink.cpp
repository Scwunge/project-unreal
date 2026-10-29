#include "McChestLink.h"
#include "McBlockWorld.h"

namespace
{
	FIntVector Step(FName Facing)
	{
		if (Facing == TEXT("north")) return FIntVector(0, 0, -1);
		if (Facing == TEXT("south")) return FIntVector(0, 0, 1);
		if (Facing == TEXT("west")) return FIntVector(-1, 0, 0);
		if (Facing == TEXT("east")) return FIntVector(1, 0, 0);
		return FIntVector::ZeroValue;
	}

	// The step clockwise of a facing, seen from above.
	FIntVector Clockwise(FName Facing)
	{
		const FIntVector Forward = Step(Facing);
		return FIntVector(-Forward.Z, 0, Forward.X);
	}

	FName Complement(FName Type)
	{
		return Type == TEXT("left") ? FName(TEXT("right")) : FName(TEXT("left"));
	}

	// The same chest with another type (facing and waterlogging kept).
	FMcStateId WithType(FMcStateId State, FName Type)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		TMap<FName, FName> Properties;
		Properties.Add(TEXT("type"), Type);
		Properties.Add(TEXT("facing"), Registry.GetPropertyValue(State, TEXT("facing")));
		Properties.Add(TEXT("waterlogged"), Registry.GetPropertyValue(State, TEXT("waterlogged")));
		return Registry.FindState(Registry.GetBlockOf(State).Name, Properties).Get(State);
	}
}

bool McChestLink::IsLinkable(FName BlockName)
{
	return BlockName == TEXT("minecraft:chest") || BlockName == TEXT("minecraft:trapped_chest");
}

FIntVector McChestLink::GetPartnerOffset(FName Facing, FName Type)
{
	if (Type == TEXT("right"))
	{
		return Clockwise(Facing);
	}
	if (Type == TEXT("left"))
	{
		return FIntVector::ZeroValue - Clockwise(Facing);
	}
	return FIntVector::ZeroValue;
}

TOptional<FIntVector> McChestLink::FindPartner(const FMcBlockWorld& World, const FIntVector& Pos)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId State = World.GetBlock(Pos);
	if (!Registry.IsValidState(State) || !IsLinkable(Registry.GetBlockOf(State).Name))
	{
		return {};
	}
	const FName Type = Registry.GetPropertyValue(State, TEXT("type"));
	const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
	const FIntVector Offset = GetPartnerOffset(Facing, Type);
	if (Offset == FIntVector::ZeroValue)
	{
		return {};
	}
	const FIntVector Other = Pos + Offset;
	const FMcStateId OtherState = World.GetBlock(Other);
	if (Registry.IsValidState(OtherState) && Registry.GetBlockOf(OtherState).Name == Registry.GetBlockOf(State).Name
		&& Registry.GetPropertyValue(OtherState, TEXT("facing")) == Facing && Registry.GetPropertyValue(OtherState, TEXT("type")) == Complement(Type))
	{
		return Other;
	}
	return {};
}

TOptional<TPair<FName, FIntVector>> McChestLink::ChooseJoin(const FMcBlockWorld& World, const FIntVector& Pos, FName BlockName, FName Facing, const FIntVector& Preferred)
{
	if (!IsLinkable(BlockName))
	{
		return {};
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FIntVector Side = Clockwise(Facing);
	// A neighbour clockwise of this chest makes it the right half; one counter-clockwise makes it the left.
	const TPair<FIntVector, FName> Candidates[2] = { { Pos + Side, FName(TEXT("right")) }, { Pos - Side, FName(TEXT("left")) } };
	TOptional<TPair<FName, FIntVector>> Found;
	for (const TPair<FIntVector, FName>& Candidate : Candidates)
	{
		const FMcStateId Other = World.GetBlock(Candidate.Key);
		if (!Registry.IsValidState(Other) || Registry.GetBlockOf(Other).Name != BlockName
			|| Registry.GetPropertyValue(Other, TEXT("facing")) != Facing || Registry.GetPropertyValue(Other, TEXT("type")) != TEXT("single"))
		{
			continue;
		}
		if (Candidate.Key == Preferred)
		{
			return TPair<FName, FIntVector>(Candidate.Value, Candidate.Key);
		}
		if (!Found.IsSet())
		{
			Found = TPair<FName, FIntVector>(Candidate.Value, Candidate.Key);
		}
	}
	return Found;
}

void McChestLink::Sync(const FMcBlockWorld& World, const FIntVector& Pos, FMcStateId Old, FMcStateId State, const TFunction<void(const FIntVector&, FMcStateId)>& SetBlock)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const auto TypeOf = [&](FMcStateId Id) -> FName
	{
		return Registry.IsValidState(Id) && IsLinkable(Registry.GetBlockOf(Id).Name) ? Registry.GetPropertyValue(Id, TEXT("type")) : FName(TEXT("single"));
	};
	const FName NewType = TypeOf(State);
	const FName OldType = TypeOf(Old);
	// A chest that is no longer half of a pair, or is a different one: the old partner is on its own now.
	const bool bSameBlock = Registry.IsValidState(Old) && Registry.IsValidState(State) && Registry.GetBlockOf(Old).Name == Registry.GetBlockOf(State).Name;
	if (OldType != TEXT("single") && !(bSameBlock && NewType == OldType))
	{
		const FName Facing = Registry.GetPropertyValue(Old, TEXT("facing"));
		const FIntVector Other = Pos + GetPartnerOffset(Facing, OldType);
		const FMcStateId OtherState = World.GetBlock(Other);
		if (Registry.IsValidState(OtherState) && Registry.GetBlockOf(OtherState).Name == Registry.GetBlockOf(Old).Name
			&& Registry.GetPropertyValue(OtherState, TEXT("facing")) == Facing && Registry.GetPropertyValue(OtherState, TEXT("type")) == Complement(OldType))
		{
			SetBlock(Other, WithType(OtherState, TEXT("single")));
		}
	}
	// A chest that became half of a pair: its single neighbour becomes the other half.
	if (NewType != TEXT("single"))
	{
		const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
		const FIntVector Other = Pos + GetPartnerOffset(Facing, NewType);
		const FMcStateId OtherState = World.GetBlock(Other);
		if (Registry.IsValidState(OtherState) && Registry.GetBlockOf(OtherState).Name == Registry.GetBlockOf(State).Name
			&& Registry.GetPropertyValue(OtherState, TEXT("facing")) == Facing && Registry.GetPropertyValue(OtherState, TEXT("type")) == TEXT("single"))
		{
			SetBlock(Other, WithType(OtherState, Complement(NewType)));
		}
	}
}
