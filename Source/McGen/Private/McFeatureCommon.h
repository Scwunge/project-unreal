// What the feature files share: directions, state lookup, block sets.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McGenRegion.h"
#include "McStateProvider.h"

namespace McFeatureCommon
{
	inline const FIntVector Up(0, 1, 0);
	inline const FIntVector Down(0, -1, 0);
	inline const FIntVector Sides[4] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };

	inline FMcStateId StateOf(const TCHAR* Block)
	{
		return FMcBlockRegistry::Get().FindDefaultState(Block).Get(0);
	}

	// A "#tag", a block id or a list of them.
	struct FBlockSet
	{
		TArray<FString> Tags;
		TSet<int32> Blocks;

		bool Parse(const TSharedPtr<FJsonValue>& Json)
		{
			if (!Json.IsValid())
			{
				return false;
			}
			TArray<FString> Names;
			if (Json->Type == EJson::Array)
			{
				for (const TSharedPtr<FJsonValue>& Name : Json->AsArray())
				{
					Names.Add(Name->AsString());
				}
			}
			else
			{
				Names.Add(Json->AsString());
			}
			for (const FString& Name : Names)
			{
				if (Name.StartsWith(TEXT("#")))
				{
					Tags.Add(Name.Mid(1));
				}
				else if (const FMcBlock* Block = FMcBlockRegistry::Get().FindBlock(FName(*McCache::Namespaced(Name))))
				{
					Blocks.Add(Block->Index);
				}
			}
			return true;
		}

		bool Has(FMcStateId State) const
		{
			if (Blocks.Contains(FMcBlockRegistry::Get().GetState(State).Block))
			{
				return true;
			}
			for (const FString& Tag : Tags)
			{
				if (FMcBlockTags::Get().Has(Tag, State))
				{
					return true;
				}
			}
			return false;
		}
	};

	inline bool IsWater(FMcStateId State)
	{
		return FMcBlockShapes::Get().GetFluid(State) == EMcFluid::Water;
	}

	// A stalagmite (Direction up) or stalactite (down) of Length with its root at Pos.
	inline void GrowPointed(FMcGenRegion& Region, const FIntVector& Pos, int32 Direction, int32 Length, FMcStateId Pointed)
	{
		const FIntVector Step(0, Direction, 0);
		const TCHAR* Facing = Direction > 0 ? TEXT("up") : TEXT("down");
		for (int32 Index = 0; Index < Length; ++Index)
		{
			const int32 FromTip = Length - 1 - Index;
			const TCHAR* Thickness = FromTip == 0 ? TEXT("tip") : (FromTip == 1 ? TEXT("frustum") : (Index == 0 ? TEXT("base") : TEXT("middle")));
			const FIntVector At = Pos + Step * Index;
			if (!Region.IsAir(Region.GetBlock(At)) && !IsWater(Region.GetBlock(At)))
			{
				return;
			}
			FMcStateId State = McWithProperty(McWithProperty(Pointed, TEXT("thickness"), Thickness), TEXT("vertical_direction"), Facing);
			if (IsWater(Region.GetBlock(At)))
			{
				State = McWithProperty(State, TEXT("waterlogged"), TEXT("true"));
			}
			Region.SetBlock(At, State);
		}
	}
}
