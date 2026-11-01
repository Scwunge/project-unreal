#include "McAquifer.h"
#include "Dom/JsonObject.h"
#include "McDensityFunction.h"
#include "McGen.h"
#include "McRandom.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// The game's deep lava level.
	constexpr int32 LavaLevel = -54;
	// Aquifers deeper than this may hold lava.
	constexpr int32 LavaAquiferTop = -10;
	// Scale on the floodedness noise. The wiki's thresholds read against the noise's documented amplitude flood too
	// much underground: measured on the reference client (5 worlds, 25 patches each), water in caves at y -64 to -33
	// is 0.01 to 0.13% of the volume and the deep lava 0.67%; at 1.0 we had 0.57% water, at 0.5 0.09%.
	static float FloodScale = 0.5f;
	static int32 SeaMargin = 28;
	static int32 LevelBase = 10;
	// Under the sea floor: the thresholds at the floor and the depth below it at which they reach the land's.
	static float SeaFull0 = -0.3f;
	static float SeaPartial0 = -0.8f;
	static float SeaDepth = 64.0f;
	static FAutoConsoleVariableRef CVarFloodScale(TEXT("mc.aq.floodscale"), FloodScale, TEXT("sweep"));
	static FAutoConsoleVariableRef CVarSeaMargin(TEXT("mc.aq.seamargin"), SeaMargin, TEXT("sweep"));
	static FAutoConsoleVariableRef CVarSeaFull0(TEXT("mc.aq.seafull0"), SeaFull0, TEXT("sweep"));
	static FAutoConsoleVariableRef CVarSeaPartial0(TEXT("mc.aq.seapartial0"), SeaPartial0, TEXT("sweep"));
	static FAutoConsoleVariableRef CVarSeaDepth(TEXT("mc.aq.seadepth"), SeaDepth, TEXT("sweep"));
	static FAutoConsoleVariableRef CVarLevelBase(TEXT("mc.aq.levelbase"), LevelBase, TEXT("sweep"));
	// Squared distances closer than this to the nearest centre's count as a boundary.
	constexpr int32 BoundaryDistance = 25;

	// Chunks around a cell whose surfaces decide whether it's open to the sea: its own, then three west, one
	// east, and one north and south.
	const FIntPoint SurfaceOffsets[] = {
		{ 0, 0 }, { -2, -1 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { -3, 0 }, { -2, 0 }, { -1, 0 }, { 1, 0 }, { -2, 1 }, { -1, 1 }, { 0, 1 }, { 1, 1 },
	};
}

bool FMcAquifer::Load(const TSharedPtr<FJsonValue>& Json, int32 SurfaceLevelNode, FMcDensityGraph& InGraph, uint64 Seed, int32 InSeaLevel,
	FMcStateId InWater, FMcStateId InLava, FMcStateId InAir, FMcStateId InStone)
{
	const TSharedPtr<FJsonObject> Object = Json.IsValid() ? Json->AsObject() : nullptr;
	if (!Object.IsValid() || SurfaceLevelNode == INDEX_NONE)
	{
		return false;
	}
	Graph = &InGraph;
	CentreSeed = FMcRandom(Seed).Fork(TEXT("minecraft:aquifer")).NextLong();
	SeaLevel = InSeaLevel;
	Water = InWater;
	Lava = InLava;
	Air = InAir;
	Stone = InStone;
	SurfaceLevel = SurfaceLevelNode;
	Barrier = InGraph.Parse(Object->TryGetField(TEXT("barrier")));
	Floodedness = InGraph.Parse(Object->TryGetField(TEXT("fluid_level_floodedness")));
	Spread = InGraph.Parse(Object->TryGetField(TEXT("fluid_level_spread")));
	LavaNoise = InGraph.Parse(Object->TryGetField(TEXT("lava")));
	Exclusion = Object->HasField(TEXT("exclusion")) ? InGraph.Parse(Object->TryGetField(TEXT("exclusion"))) : INDEX_NONE;
	if (Barrier == INDEX_NONE || Floodedness == INDEX_NONE || Spread == INDEX_NONE || LavaNoise == INDEX_NONE)
	{
		UE_LOG(LogMcGen, Error, TEXT("Aquifer functions didn't all load"));
		Graph = nullptr;
		return false;
	}
	return true;
}

FIntVector FMcAquifer::CellOf(int32 X, int32 Y, int32 Z)
{
	return FIntVector(FMath::FloorToInt32((X + 5) / 16.0), FMath::FloorToInt32((Y + 1) / 12.0), FMath::FloorToInt32((Z + 5) / 16.0));
}

FIntVector FMcAquifer::GetCentre(const FIntVector& Cell, FCache& Cache) const
{
	if (const FIntVector* Found = Cache.Centres.Find(Cell))
	{
		return *Found;
	}
	FMcRandom Random = FMcRandom(CentreSeed).Fork(Cell.X, Cell.Y, Cell.Z);
	const int32 OffsetX = Random.NextInt(10);
	const int32 OffsetY = Random.NextInt(9);
	const int32 OffsetZ = Random.NextInt(10);
	const FIntVector Centre(Cell.X * 16 - 5 + OffsetX, Cell.Y * 12 - 1 + OffsetY, Cell.Z * 16 - 5 + OffsetZ);
	Cache.Centres.Add(Cell, Centre);
	return Centre;
}

int32 FMcAquifer::SurfaceAt(int32 ChunkX, int32 ChunkZ, FMcDensityContext& Context, FCache& Cache) const
{
	const FIntPoint Key(ChunkX, ChunkZ);
	if (const int32* Found = Cache.Surfaces.Find(Key))
	{
		return *Found;
	}
	Context.SetPos(ChunkX * 16, 0, ChunkZ * 16);
	const int32 Surface = FMath::FloorToInt32(Graph->Evaluate(SurfaceLevel, Context));
	Cache.Surfaces.Add(Key, Surface);
	return Surface;
}

FMcAquifer::FStatus FMcAquifer::GetStatus(const FIntVector& Cell, FMcDensityContext& Context, FCache& Cache) const
{
	if (const FStatus* Found = Cache.Statuses.Find(Cell))
	{
		return *Found;
	}
	const FIntVector Centre = GetCentre(Cell, Cache);
	const int32 ChunkX = FMath::FloorToInt32(Centre.X / 16.0);
	const int32 ChunkZ = FMath::FloorToInt32(Centre.Z / 16.0);

	FStatus Global;
	Global.bGlobal = true;
	Global.Level = SeaLevel;
	Global.Fluid = Water;
	FStatus Result;

	const int32 OwnSurface = SurfaceAt(ChunkX, ChunkZ, Context, Cache);
	int32 LowestSurface = MAX_int32;
	bool bOpenToSea = false;
	for (const FIntPoint& Offset : SurfaceOffsets)
	{
		const int32 Surface = SurfaceAt(ChunkX + Offset.X, ChunkZ + Offset.Y, Context, Cache);
		LowestSurface = FMath::Min(LowestSurface, Surface);
		bOpenToSea |= Surface < SeaLevel && Centre.Y + 12 > Surface + SeaMargin;
	}

	if (Centre.Y - 12 > OwnSurface + 8 || bOpenToSea)
	{
		// High above ground or open to the sea: the sea decides.
		Result = Global;
	}
	else
	{
		Context.SetPos(Centre.X, Centre.Y, Centre.Z);
		const bool bExcluded = Exclusion != INDEX_NONE && Graph->Evaluate(Exclusion, Context) > 0.0;
		const double Flood = FMath::Clamp(Graph->Evaluate(Floodedness, Context) * FloodScale, -1.0, 1.0);
		double Full = 0.8;
		double Partial = 0.4;
		if (OwnSurface < SeaLevel)
		{
			// Under the sea floor, cells near the floor flood easily; 64 blocks down they flood as on land.
			const double Depth = FMath::Clamp((OwnSurface - Centre.Y) / static_cast<double>(SeaDepth), 0.0, 1.0);
			Full = FMath::Lerp(static_cast<double>(SeaFull0), 0.8, Depth);
			Partial = FMath::Lerp(static_cast<double>(SeaPartial0), 0.4, Depth);
		}
		if (bExcluded)
		{
			// Dry.
		}
		else if (Flood > Full)
		{
			Result = Global;
		}
		else if (Flood > Partial)
		{
			// A local level, shared through each 16x40x16 region and stepped by 3.
			const int32 RegionY = FMath::FloorToInt32(Centre.Y / 40.0);
			Context.SetPos(ChunkX * 16, RegionY * 40, ChunkZ * 16);
			const double SpreadValue = Graph->Evaluate(Spread, Context) * 10.0;
			const int32 Step = FMath::Clamp(FMath::FloorToInt32(SpreadValue / 3.0) * 3, -9, 9);
			Result.Level = FMath::Min(RegionY * 40 + LevelBase + Step, LowestSurface);
			Result.Fluid = Water;
			if (Result.Level <= LavaAquiferTop)
			{
				Context.SetPos(FMath::FloorToInt32(Centre.X / 64.0) * 64, RegionY * 40, FMath::FloorToInt32(Centre.Z / 64.0) * 64);
				if (FMath::Abs(Graph->Evaluate(LavaNoise, Context)) > 0.3)
				{
					Result.Fluid = Lava;
				}
			}
		}
	}
	Cache.Statuses.Add(Cell, Result);
	return Result;
}

FMcStateId FMcAquifer::GlobalAt(int32 Y) const
{
	if (Y < FMath::Min(LavaLevel, SeaLevel))
	{
		return Lava;
	}
	return Y < SeaLevel ? Water : Air;
}

FMcStateId FMcAquifer::FluidAt(const FStatus& Status, int32 Y) const
{
	if (Status.bGlobal)
	{
		return GlobalAt(Y);
	}
	// A dry cell below the lava level holds lava too (measured: caves at y -64 to -49 are 0.67% lava, 1.69% air).
	if (Status.Level == MIN_int32 && Y < LavaLevel)
	{
		return Lava;
	}
	return Y < Status.Level ? Status.Fluid : Air;
}

double FMcAquifer::Pressure(int32 Y, const FStatus& A, const FStatus& B, double BarrierNoise) const
{
	const FMcStateId FluidA = FluidAt(A, Y);
	const FMcStateId FluidB = FluidAt(B, Y);
	if (FluidA == FluidB)
	{
		return 0.0;
	}
	if (FluidA != Air && FluidB != Air)
	{
		// Water against lava: nearly always a wall.
		return 2.0;
	}
	// Fluid against air: a wall, strongest midway between the two levels and roughened by the barrier noise.
	const int32 LevelA = A.bGlobal ? SeaLevel : A.Level;
	const int32 LevelB = B.bGlobal ? SeaLevel : B.Level;
	const int32 High = FMath::Max(LevelA, LevelB);
	const int32 Low = FMath::Max(FMath::Min(LevelA, LevelB), High - 64);
	const double Middle = (High + Low) * 0.5;
	const double HalfSpan = FMath::Max((High - Low) * 0.5, 1.0);
	const double Peak = 1.0 - FMath::Abs(Y - Middle) / (HalfSpan + 4.0);
	return 0.5 + 0.5 * FMath::Max(Peak, 0.0) + BarrierNoise * 0.5;
}

FMcStateId FMcAquifer::Compute(int32 X, int32 Y, int32 Z, double Density, FMcDensityContext& Context, FCache& Cache) const
{
	if (!Graph)
	{
		return GlobalAt(Y);
	}
	// The three nearest centres among the 2x3x2 cells around the block.
	struct FNear
	{
		int32 Distance = MAX_int32;
		FIntVector Cell;
	};
	FNear Near[3];
	const int32 BaseX = FMath::FloorToInt32(X / 16.0);
	const int32 BaseY = FMath::FloorToInt32((Y + 1) / 12.0);
	const int32 BaseZ = FMath::FloorToInt32(Z / 16.0);
	for (int32 CellX = BaseX; CellX <= BaseX + 1; ++CellX)
	{
		for (int32 CellY = BaseY - 1; CellY <= BaseY + 1; ++CellY)
		{
			for (int32 CellZ = BaseZ; CellZ <= BaseZ + 1; ++CellZ)
			{
				const FIntVector Cell(CellX, CellY, CellZ);
				const FIntVector Offset = GetCentre(Cell, Cache) - FIntVector(X, Y, Z);
				FNear Candidate{ Offset.X * Offset.X + Offset.Y * Offset.Y + Offset.Z * Offset.Z, Cell };
				for (FNear& Slot : Near)
				{
					if (Candidate.Distance < Slot.Distance)
					{
						Swap(Candidate, Slot);
					}
				}
			}
		}
	}

	const FStatus First = GetStatus(Near[0].Cell, Context, Cache);
	const FMcStateId Fill = FluidAt(First, Y);
	if (Near[1].Distance - Near[0].Distance >= BoundaryDistance)
	{
		return Fill;
	}

	// At a boundary: walls between differing neighbours.
	Context.SetPos(X, Y, Z);
	const double BarrierNoise = Graph->Evaluate(Barrier, Context);
	double Total = 0.0;
	for (int32 Index = 1; Index < 3; ++Index)
	{
		const int32 Gap = Near[Index].Distance - Near[0].Distance;
		if (Gap >= BoundaryDistance)
		{
			break;
		}
		const double Closeness = 1.0 - static_cast<double>(Gap) / BoundaryDistance;
		Total += Closeness * Pressure(Y, First, GetStatus(Near[Index].Cell, Context, Cache), BarrierNoise);
	}
	return Density + Total > 0.0 ? Stone : Fill;
}
