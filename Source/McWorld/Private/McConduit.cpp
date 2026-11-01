#include "McConduit.h"
#include "McBlockWorld.h"

namespace
{
	FName NameOf(FMcStateId State)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		return Registry.IsValidState(State) ? Registry.GetBlockOf(State).Name : NAME_None;
	}

	bool IsWet(FMcStateId State)
	{
		return NameOf(State) == FName(TEXT("minecraft:water")) || FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("waterlogged")) == FName(TEXT("true"));
	}
}

bool McConduit::IsConduit(FMcStateId State)
{
	return NameOf(State) == FName(TEXT("minecraft:conduit"));
}

const TArray<FIntVector>& McConduit::GetFramePlaces()
{
	static const TArray<FIntVector> Places = []
	{
		TArray<FIntVector> Result;
		for (int32 X = -2; X <= 2; ++X)
		{
			for (int32 Y = -2; Y <= 2; ++Y)
			{
				for (int32 Z = -2; Z <= 2; ++Z)
				{
					// In one of the three planes through the conduit, on that plane's outer ring.
					const int32 Zeros = (X == 0) + (Y == 0) + (Z == 0);
					const int32 Reach = FMath::Max3(FMath::Abs(X), FMath::Abs(Y), FMath::Abs(Z));
					if (Zeros >= 1 && Reach == 2)
					{
						Result.Add(FIntVector(X, Y, Z));
					}
				}
			}
		}
		return Result;
	}();
	return Places;
}

int32 McConduit::CountFrame(const FMcBlockWorld& World, const FIntVector& Pos)
{
	static const FName Frames[] = { FName(TEXT("minecraft:prismarine")), FName(TEXT("minecraft:prismarine_bricks")),
		FName(TEXT("minecraft:dark_prismarine")), FName(TEXT("minecraft:sea_lantern")) };
	int32 Count = 0;
	for (const FIntVector& Place : GetFramePlaces())
	{
		const FName Name = NameOf(World.GetBlock(Pos + Place));
		Count += (Name == Frames[0] || Name == Frames[1] || Name == Frames[2] || Name == Frames[3]) ? 1 : 0;
	}
	return Count;
}

bool McConduit::IsActive(const FMcBlockWorld& World, const FIntVector& Pos)
{
	if (!IsConduit(World.GetBlock(Pos)))
	{
		return false;
	}
	for (int32 X = -1; X <= 1; ++X)
	{
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 Z = -1; Z <= 1; ++Z)
			{
				if (!IsWet(World.GetBlock(Pos + FIntVector(X, Y, Z))))
				{
					return false;
				}
			}
		}
	}
	return CountFrame(World, Pos) >= MinFrame;
}
