#include "McPathfinding.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "Algo/Reverse.h"

namespace
{
	enum class EFloor : uint8
	{
		// Something to stand on.
		Solid,
		// Nothing there: the mob would fall on.
		Empty,
		// Not to be stood on (cactus, magma, a fence post, the void).
		Unsafe,
	};

	bool CellFree(const FMcBlockShapes& Shapes, FMcStateId State)
	{
		return !Shapes.HasCollision(State) && Shapes.GetFluid(State) == EMcFluid::None && !McPath::IsHazard(State);
	}

	EFloor ClassifyFloor(const FMcBlockWorld& World, const FIntVector& Under)
	{
		if (Under.Y < World.GetDimension().MinY || Under.Y > World.GetDimension().MaxY())
		{
			return Under.Y < World.GetDimension().MinY ? EFloor::Unsafe : EFloor::Empty;
		}
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		const FMcStateId State = World.GetBlock(Under);
		if (!Shapes.HasCollision(State))
		{
			return EFloor::Empty;
		}
		if (McPath::IsHazard(State) || Shapes.GetFluid(State) != EMcFluid::None)
		{
			return EFloor::Unsafe;
		}
		float Top = 0.0f;
		for (const FBox3f& Box : Shapes.GetCollision(State))
		{
			Top = FMath::Max(Top, Box.Max.Y);
		}
		return Top > 1.001f ? EFloor::Unsafe : EFloor::Solid;
	}

	struct FNode
	{
		FIntVector Cell;
		float G = 0.0f;
		float H = 0.0f;
		int32 Parent = INDEX_NONE;
		bool bClosed = false;
	};

	struct FOpenEntry
	{
		float F;
		int32 Node;
	};
}

bool McPath::IsHazard(FMcStateId State)
{
	// Game thread only, like the registries it reads.
	static TMap<FMcStateId, bool> Cache;
	if (const bool* Known = Cache.Find(State))
	{
		return *Known;
	}
	static const TSet<FName> Hazards = {
		TEXT("minecraft:fire"), TEXT("minecraft:soul_fire"), TEXT("minecraft:lava"), TEXT("minecraft:cactus"),
		TEXT("minecraft:sweet_berry_bush"), TEXT("minecraft:cobweb"), TEXT("minecraft:powder_snow"),
		TEXT("minecraft:magma_block"), TEXT("minecraft:campfire"), TEXT("minecraft:soul_campfire"),
		TEXT("minecraft:wither_rose"), TEXT("minecraft:nether_portal"), TEXT("minecraft:end_portal"),
	};
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const bool bHazard = Registry.IsValidState(State) && Hazards.Contains(Registry.GetBlockOf(State).Name);
	Cache.Add(State, bHazard);
	return bHazard;
}

bool McPath::IsOpen(const FMcBlockWorld& World, const FIntVector& Feet, int32 Clearance)
{
	const FMcDimensionType& Dimension = World.GetDimension();
	if (Feet.Y < Dimension.MinY || Feet.Y + Clearance - 1 > Dimension.MaxY()
		|| !World.HasChunk(FIntPoint(Feet.X >> 4, Feet.Z >> 4)))
	{
		return false;
	}
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	for (int32 Up = 0; Up < Clearance; ++Up)
	{
		if (!CellFree(Shapes, World.GetBlock(FIntVector(Feet.X, Feet.Y + Up, Feet.Z))))
		{
			return false;
		}
	}
	return true;
}

bool McPath::IsStandable(const FMcBlockWorld& World, const FIntVector& Feet, int32 Clearance)
{
	return IsOpen(World, Feet, Clearance) && ClassifyFloor(World, FIntVector(Feet.X, Feet.Y - 1, Feet.Z)) == EFloor::Solid;
}

bool McPath::Find(const FMcBlockWorld& World, const FIntVector& Start, const FIntVector& Goal, const FMcPathOptions& Options, TArray<FIntVector>& Path)
{
	Path.Reset();
	const int32 Clearance = FMath::Max(Options.Clearance, 1);

	const auto Heuristic = [&Goal](const FIntVector& Cell)
	{
		const float DX = FMath::Abs(Cell.X - Goal.X);
		const float DZ = FMath::Abs(Cell.Z - Goal.Z);
		const float Lo = FMath::Min(DX, DZ);
		return (FMath::Max(DX, DZ) - Lo) + 1.4142f * Lo + 0.5f * FMath::Abs(Cell.Y - Goal.Y);
	};
	const auto Reached = [&](const FIntVector& Cell)
	{
		return FMath::Abs(Cell.X - Goal.X) <= Options.GoalRadius && FMath::Abs(Cell.Z - Goal.Z) <= Options.GoalRadius && FMath::Abs(Cell.Y - Goal.Y) <= 1;
	};
	const auto ByCost = [](const FOpenEntry& A, const FOpenEntry& B) { return A.F < B.F; };

	TArray<FNode> Nodes;
	TMap<FIntVector, int32> Lookup;
	TArray<FOpenEntry> Open;
	{
		FNode First;
		First.Cell = Start;
		First.H = Heuristic(Start);
		Nodes.Add(First);
		Lookup.Add(Start, 0);
		Open.HeapPush(FOpenEntry{ First.H, 0 }, ByCost);
	}

	int32 Best = 0;
	int32 Found = INDEX_NONE;
	int32 Expanded = 0;
	while (!Open.IsEmpty() && Expanded < Options.MaxNodes)
	{
		FOpenEntry Top;
		Open.HeapPop(Top, ByCost, EAllowShrinking::No);
		if (Nodes[Top.Node].bClosed)
		{
			continue;
		}
		const int32 Current = Top.Node;
		Nodes[Current].bClosed = true;
		++Expanded;
		const FIntVector Cell = Nodes[Current].Cell;
		if (Reached(Cell))
		{
			Found = Current;
			break;
		}
		if (Nodes[Current].H < Nodes[Best].H || (Nodes[Current].H == Nodes[Best].H && Nodes[Current].G < Nodes[Best].G))
		{
			Best = Current;
		}

		const auto Consider = [&](const FIntVector& Next, float Cost)
		{
			const float G = Nodes[Current].G + Cost;
			if (const int32* Existing = Lookup.Find(Next))
			{
				if (Nodes[*Existing].bClosed || G >= Nodes[*Existing].G)
				{
					return;
				}
				Nodes[*Existing].G = G;
				Nodes[*Existing].Parent = Current;
				Open.HeapPush(FOpenEntry{ G + Nodes[*Existing].H, *Existing }, ByCost);
				return;
			}
			FNode Node;
			Node.Cell = Next;
			Node.G = G;
			Node.H = Heuristic(Next);
			Node.Parent = Current;
			const int32 Index = Nodes.Add(Node);
			Lookup.Add(Next, Index);
			Open.HeapPush(FOpenEntry{ G + Node.H, Index }, ByCost);
		};
		const auto IsOpenAt = [&](int32 X, int32 Y, int32 Z) { return McPath::IsOpen(World, FIntVector(X, Y, Z), Clearance); };

		static const FIntPoint Sides[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		static const FIntPoint Corners[4] = { FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1) };
		for (const FIntPoint& Side : Sides)
		{
			const int32 NX = Cell.X + Side.X;
			const int32 NZ = Cell.Z + Side.Y;
			if (IsOpenAt(NX, Cell.Y, NZ))
			{
				// Level ground, or a drop to some.
				int32 NY = Cell.Y;
				int32 Drop = 0;
				EFloor Floor = ClassifyFloor(World, FIntVector(NX, NY - 1, NZ));
				while (Floor == EFloor::Empty && Drop < Options.MaxDrop && IsOpenAt(NX, NY - 1, NZ))
				{
					--NY;
					++Drop;
					Floor = ClassifyFloor(World, FIntVector(NX, NY - 1, NZ));
				}
				if (Floor == EFloor::Solid)
				{
					Consider(FIntVector(NX, NY, NZ), 1.0f + 0.5f * Drop);
				}
			}
			else if (Options.MaxStepUp >= 1 && IsOpenAt(Cell.X, Cell.Y + 1, Cell.Z) && McPath::IsStandable(World, FIntVector(NX, Cell.Y + 1, NZ), Clearance))
			{
				Consider(FIntVector(NX, Cell.Y + 1, NZ), 1.5f);
			}
		}
		for (const FIntPoint& Corner : Corners)
		{
			// Only across level ground, and only where both ways round are free, or the body would clip the corner.
			if (IsOpenAt(Cell.X + Corner.X, Cell.Y, Cell.Z) && IsOpenAt(Cell.X, Cell.Y, Cell.Z + Corner.Y)
				&& McPath::IsStandable(World, FIntVector(Cell.X + Corner.X, Cell.Y, Cell.Z + Corner.Y), Clearance))
			{
				Consider(FIntVector(Cell.X + Corner.X, Cell.Y, Cell.Z + Corner.Y), 1.4142f);
			}
		}
	}

	const int32 End = Found != INDEX_NONE ? Found : Best;
	for (int32 Index = End; Index != INDEX_NONE && Index != 0; Index = Nodes[Index].Parent)
	{
		Path.Add(Nodes[Index].Cell);
	}
	Algo::Reverse(Path);
	return Found != INDEX_NONE;
}
