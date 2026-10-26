#include "McRaycast.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

namespace
{
	bool IsFluid(const FString& Name)
	{
		return Name == TEXT("minecraft:water") || Name == TEXT("minecraft:lava") || Name == TEXT("minecraft:bubble_column");
	}

	// Where the ray Origin + t * Direction enters Box, and through which face; t in [0, MaxT].
	bool IntersectBox(const FVector& Origin, const FVector& Direction, const FBox& Box, double MaxT, double& OutT, int32& OutFace)
	{
		double Near = 0.0;
		double Far = MaxT;
		int32 Face = -1;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(Direction[Axis]) < 1e-12)
			{
				if (Origin[Axis] < Box.Min[Axis] || Origin[Axis] > Box.Max[Axis])
				{
					return false;
				}
				continue;
			}
			const double Inverse = 1.0 / Direction[Axis];
			double T0 = (Box.Min[Axis] - Origin[Axis]) * Inverse;
			double T1 = (Box.Max[Axis] - Origin[Axis]) * Inverse;
			// Entering through the min side means moving in +axis, so the face looks towards -axis.
			int32 EnterFace = Axis == 0 ? 4 : Axis == 1 ? 0 : 2;
			if (T0 > T1)
			{
				Swap(T0, T1);
				EnterFace += 1;
			}
			if (T0 > Near)
			{
				Near = T0;
				Face = EnterFace;
			}
			Far = FMath::Min(Far, T1);
			if (Near > Far)
			{
				return false;
			}
		}
		if (Face < 0)
		{
			// The ray starts inside the box: no face to hit, so it doesn't count (as in Minecraft).
			return false;
		}
		OutT = Near;
		OutFace = Face;
		return true;
	}
}

FIntVector McRaycast::FaceDirection(int32 Face)
{
	static const FIntVector Directions[6] = {
		FIntVector(0, -1, 0), FIntVector(0, 1, 0), FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0),
	};
	return Directions[Face];
}

FIntVector FMcBlockHit::GetAdjacent() const
{
	return Block + McRaycast::FaceDirection(Face);
}

void McRaycast::GetOutline(FMcStateId State, TArray<FBox3f, TInlineAllocator<8>>& Out)
{
	Out.Reset();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (Registry.IsAir(State) || IsFluid(Registry.GetBlockOf(State).Name.ToString()))
	{
		return;
	}
	const TConstArrayView<FBox3f> Collision = FMcBlockShapes::Get().GetCollision(State);
	if (Collision.Num() == 0)
	{
		Out.Add(FBox3f(FVector3f::ZeroVector, FVector3f::OneVector));
		return;
	}
	for (const FBox3f& Box : Collision)
	{
		// Outlines stay inside the block: fences and walls are only 1.5 high to stop jumping.
		Out.Add(FBox3f(Box.Min, FVector3f(Box.Max.X, FMath::Min(Box.Max.Y, 1.0f), Box.Max.Z)));
	}
}

bool McRaycast::IsReplaceable(FMcStateId State)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (Registry.IsAir(State))
	{
		return true;
	}
	static const TSet<FName> Replaceable = {
		TEXT("minecraft:water"), TEXT("minecraft:lava"), TEXT("minecraft:bubble_column"), TEXT("minecraft:short_grass"),
		TEXT("minecraft:tall_grass"), TEXT("minecraft:fern"), TEXT("minecraft:large_fern"), TEXT("minecraft:dead_bush"),
		TEXT("minecraft:bush"), TEXT("minecraft:short_dry_grass"), TEXT("minecraft:tall_dry_grass"), TEXT("minecraft:seagrass"),
		TEXT("minecraft:tall_seagrass"), TEXT("minecraft:vine"), TEXT("minecraft:glow_lichen"), TEXT("minecraft:fire"),
		TEXT("minecraft:soul_fire"), TEXT("minecraft:light"), TEXT("minecraft:structure_void"), TEXT("minecraft:hanging_roots"),
		TEXT("minecraft:crimson_roots"), TEXT("minecraft:warped_roots"), TEXT("minecraft:nether_sprouts"), TEXT("minecraft:leaf_litter"),
	};
	const FName Name = Registry.GetBlockOf(State).Name;
	if (Name == TEXT("minecraft:snow"))
	{
		return Registry.GetPropertyValue(State, TEXT("layers")) == TEXT("1");
	}
	return Replaceable.Contains(Name);
}

TOptional<FMcBlockHit> McRaycast::Trace(const FMcBlockWorld& World, const FVector& Origin, const FVector& Direction, double MaxDistance)
{
	const FVector Dir = Direction.GetSafeNormal();
	if (Dir.IsZero())
	{
		return {};
	}

	// Walk the cells the ray passes through, nearest first (Amanatides and Woo).
	FIntVector Cell(FMath::FloorToInt32(Origin.X), FMath::FloorToInt32(Origin.Y), FMath::FloorToInt32(Origin.Z));
	FIntVector Step;
	FVector NextT;
	FVector DeltaT;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Dir[Axis] > 0.0)
		{
			Step[Axis] = 1;
			DeltaT[Axis] = 1.0 / Dir[Axis];
			NextT[Axis] = (Cell[Axis] + 1 - Origin[Axis]) * DeltaT[Axis];
		}
		else if (Dir[Axis] < 0.0)
		{
			Step[Axis] = -1;
			DeltaT[Axis] = -1.0 / Dir[Axis];
			NextT[Axis] = (Origin[Axis] - Cell[Axis]) * DeltaT[Axis];
		}
		else
		{
			Step[Axis] = 0;
			DeltaT[Axis] = TNumericLimits<double>::Max();
			NextT[Axis] = TNumericLimits<double>::Max();
		}
	}

	TArray<FBox3f, TInlineAllocator<8>> Outline;
	double CellEnter = 0.0;
	while (CellEnter <= MaxDistance)
	{
		const FMcStateId State = World.GetBlock(Cell);
		GetOutline(State, Outline);
		double BestT = TNumericLimits<double>::Max();
		int32 BestFace = -1;
		const FVector Corner(Cell);
		for (const FBox3f& Local : Outline)
		{
			double T;
			int32 Face;
			if (IntersectBox(Origin, Dir, FBox(Corner + FVector(Local.Min), Corner + FVector(Local.Max)), MaxDistance, T, Face) && T < BestT)
			{
				BestT = T;
				BestFace = Face;
			}
		}
		if (BestFace >= 0)
		{
			FMcBlockHit Hit;
			Hit.Block = Cell;
			Hit.Face = BestFace;
			Hit.Point = Origin + Dir * BestT;
			Hit.State = State;
			return Hit;
		}

		// On to the next cell, across whichever boundary comes first.
		int32 Axis = NextT.X < NextT.Y ? (NextT.X < NextT.Z ? 0 : 2) : (NextT.Y < NextT.Z ? 1 : 2);
		CellEnter = NextT[Axis];
		Cell[Axis] += Step[Axis];
		NextT[Axis] += DeltaT[Axis];
	}
	return {};
}
