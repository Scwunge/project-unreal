// The feature types of the caves, the sea, the Nether and the End that the other files leave out: big dripstone,
// dripstone clusters, icebergs, chorus plants, basalt deltas, the End's spikes, islands, gateways and platforms,
// huge fungi, nether blobs and root systems. What each does is from minecraft.wiki; the shapes are our own.

#include "McFeatureTypes.h"
#include "Dom/JsonObject.h"
#include "McBlockPredicate.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McFeature.h"
#include "McFeatureCommon.h"
#include "McGenRegion.h"
#include "McProviders.h"
#include "McRandom.h"
#include "McStateProvider.h"

namespace
{
	using namespace McFeatureCommon;
	using FLibrary = FMcFeatureLibrary;

	bool IsOpen(FMcGenRegion& Region, const FIntVector& Pos)
	{
		const FMcStateId State = Region.GetBlock(Pos);
		return Region.IsAir(State) || IsWater(State);
	}

	FName BlockName(FMcStateId State)
	{
		return FMcBlockRegistry::Get().GetBlockOf(State).Name;
	}

	// The first solid block under (Direction -1) or over (1) Pos within Range, or INDEX_NONE for no such y.
	int32 FindSolid(FMcGenRegion& Region, const FIntVector& Pos, int32 Direction, int32 Range)
	{
		for (int32 Step = 0; Step <= Range; ++Step)
		{
			const FIntVector At = Pos + FIntVector(0, Direction * Step, 0);
			if (!IsOpen(Region, At))
			{
				return At.Y;
			}
		}
		return INDEX_NONE;
	}

	// ---- large_dripstone -----------------------------------------------------------------------------------

	class FLargeDripstone final : public FMcFeature
	{
	public:
		FMcIntProvider ColumnRadius;
		FMcFloatProvider HeightScale;
		FMcFloatProvider StalactiteBluntness;
		FMcFloatProvider StalagmiteBluntness;
		double MaxRadiusRatio = 0.33;
		FBlockSet Replaceable;

		// A cone of dripstone rooted at Root (the first open block next to the cave wall), pointing Direction
		// (-1 down from a ceiling, 1 up from a floor), Height long, Radius wide at the root.
		static void Cone(FMcGenRegion& Region, const FIntVector& Root, int32 Direction, int32 Height, double Radius, double Bluntness, const FBlockSet& Replaceable, FMcStateId Block, FMcStateId Pointed)
		{
			int64 Count = 0;
			for (int32 Layer = 0; Layer < Height; ++Layer)
			{
				const double Fraction = static_cast<double>(Layer) / Height;
				// A wide base that narrows to a point; blunt ones narrow linearly, sharp ones faster. Fitted to the dripstone block count
				// per column of the reference client (ours was 4.4 times it).
				const double R = FMath::Max(0.0, Radius * FMath::Pow(1.0 - Fraction, 1.0 + (1.0 - Bluntness)));
				const int32 Reach = FMath::CeilToInt32(R);
				for (int32 DX = -Reach; DX <= Reach; ++DX)
				{
					for (int32 DZ = -Reach; DZ <= Reach; ++DZ)
					{
						if (DX * DX + DZ * DZ > R * R + 0.25)
						{
							continue;
						}
						const FIntVector At = Root + FIntVector(DX, Direction * Layer, DZ);
						const FMcStateId Old = Region.GetBlock(At);
						if (IsOpen(Region, At) || Replaceable.Has(Old))
						{
							Region.SetBlock(At, Block);
							++Count;
						}
					}
				}
			}
			McCountBlocks(TEXT("large_dripstone cone"), Count);
			// The tip.
			GrowPointed(Region, Root + FIntVector(0, Direction * Height, 0), Direction, 1, Pointed);
		}

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			if (!IsOpen(Region, Origin))
			{
				return false;
			}
			const int32 Ceiling = FindSolid(Region, Origin, 1, 30);
			const int32 Floor = FindSolid(Region, Origin, -1, 30);
			if (Ceiling == INDEX_NONE || Floor == INDEX_NONE || !Replaceable.Has(Region.GetBlock(FIntVector(Origin.X, Ceiling, Origin.Z))) || !Replaceable.Has(Region.GetBlock(FIntVector(Origin.X, Floor, Origin.Z))))
			{
				return false;
			}
			const int32 CaveHeight = Ceiling - Floor - 1;
			if (CaveHeight < 4)
			{
				return false;
			}
			// Radius x0.7 and heights x0.7: fitted to the reference client's dripstone block count per column.
			const int32 Radius = FMath::Max(1, FMath::RoundToInt32(0.7 * FMath::Min(ColumnRadius.Sample(Random), FMath::Max(1, FMath::FloorToInt32(CaveHeight * MaxRadiusRatio)))));
			const double Scale = HeightScale.Sample(Random);
			int32 Hang = FMath::Max(2, FMath::RoundToInt32(Radius * Scale * 0.7));
			int32 Rise = FMath::Max(2, FMath::RoundToInt32(Radius * Scale * 0.7 * (0.6 + Random.NextDouble() * 0.8)));
			if (Hang + Rise >= CaveHeight)
			{
				// They meet: one column, thickest in the middle.
				const int32 Total = CaveHeight - 1;
				Hang = FMath::Max(1, Total / 2);
				Rise = FMath::Max(1, Total - Hang);
			}
			const FMcStateId Block = StateOf(TEXT("minecraft:dripstone_block"));
			const FMcStateId Pointed = StateOf(TEXT("minecraft:pointed_dripstone"));
			Cone(Region, FIntVector(Origin.X, Ceiling - 1, Origin.Z), -1, Hang, Radius, StalactiteBluntness.Sample(Random), Replaceable, Block, Pointed);
			Cone(Region, FIntVector(Origin.X, Floor + 1, Origin.Z), 1, Rise, Radius, StalagmiteBluntness.Sample(Random), Replaceable, Block, Pointed);
			return true;
		}
	};

	// ---- speleothem_cluster (dripstone and sulfur clusters) --------------------------------------------------

	class FSpeleothemCluster final : public FMcFeature
	{
	public:
		FMcStateId Base = 0;
		FMcStateId Pointed = 0;
		FBlockSet Replaceable;
		FMcIntProvider Radius;
		FMcIntProvider Height;
		FMcIntProvider Thickness;
		FMcFloatProvider Density;
		int32 SearchRange = 12;
		int32 HeightDeviation = 3;
		double EdgeChance = 0.1;
		int32 MaxHeightDifference = 1;
		double Wetness = 0.0;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			if (!IsOpen(Region, Origin))
			{
				return false;
			}
			const int32 R = FMath::Max(1, FMath::RoundToInt32(0.85 * Radius.Sample(Random)));
			const double Dense = Density.Sample(Random);
			bool bPlaced = false;
			int64 BaseBlocks = 0;
			for (int32 DX = -R; DX <= R; ++DX)
			{
				for (int32 DZ = -R; DZ <= R; ++DZ)
				{
					const double Distance = FMath::Sqrt(static_cast<double>(DX * DX + DZ * DZ));
					if (Distance > R || Random.NextDouble() > Dense)
					{
						continue;
					}
					const FIntVector Column(Origin.X + DX, Origin.Y, Origin.Z + DZ);
					const int32 Floor = FindSolid(Region, Column, -1, SearchRange);
					const int32 Ceiling = FindSolid(Region, Column, 1, SearchRange);
					const double Edge = FMath::Lerp(1.0, EdgeChance, Distance / FMath::Max(1, R));
					// Halved: the block count per column ran 3.1 times the reference client's with the data's thickness.
					const int32 Layers = FMath::Max(1, (Thickness.Sample(Random) + 1) / 2);
					for (int32 Side = 0; Side < 2; ++Side)
					{
						const int32 Solid = Side == 0 ? Floor : Ceiling;
						const int32 Direction = Side == 0 ? 1 : -1;
						if (Solid == INDEX_NONE || !Replaceable.Has(Region.GetBlock(FIntVector(Column.X, Solid, Column.Z))))
						{
							continue;
						}
						// A layer of the base block into the cave wall, then the points growing out of it.
						for (int32 Depth = 0; Depth < Layers; ++Depth)
						{
							const FIntVector At(Column.X, Solid - Direction * Depth, Column.Z);
							if (Replaceable.Has(Region.GetBlock(At)))
							{
								Region.SetBlock(At, Base);
								++BaseBlocks;
							}
						}
						if (Random.NextDouble() < Edge)
						{
							int32 Length = FMath::Clamp(Height.Sample(Random) + Random.NextInt(2 * HeightDeviation + 1) - HeightDeviation, 1, 12);
							Length = FMath::Max(1, Length - FMath::RoundToInt32(Distance / FMath::Max(1, R) * 2.0));
							GrowPointed(Region, FIntVector(Column.X, Solid + Direction, Column.Z), Direction, Length, Pointed);
							bPlaced = true;
						}
					}
				}
			}
			McCountBlocks(TEXT("speleothem_cluster"), BaseBlocks);
			return bPlaced;
		}
	};

	// ---- iceberg -------------------------------------------------------------------------------------------

	class FIceberg final : public FMcFeature
	{
	public:
		FMcStateId State = 0;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			const int32 Sea = Context.SeaLevel;
			if (!IsWater(Region.GetBlock(FIntVector(Origin.X, Sea - 1, Origin.Z))))
			{
				return false;
			}
			static const FMcStateId Snow = StateOf(TEXT("minecraft:snow_block"));
			// Fitted: icebergs of radius 4 to 10 covered 4% of a frozen ocean's surface (168 columns each, one per 16 chunks) against
			// the reference client's 14% packed ice, so radius 7 to 18 (about 3.2x the area).
			const int32 Radius = 7 + Random.NextInt(12);
			const int32 Above = 6 + Random.NextInt(Radius * 2 + 2);
			const int32 Below = FMath::Max(3, Above * 2 / 3);
			// Two lumps: the main one and a smaller one beside it.
			const FIntVector Centres[2] = { FIntVector(Origin.X, 0, Origin.Z), FIntVector(Origin.X + Random.NextInt(5) - 2 + Radius / 2, 0, Origin.Z + Random.NextInt(5) - 2) };
			bool bPlaced = false;
			for (int32 Lump = 0; Lump < 2; ++Lump)
			{
				const double LumpRadius = Lump == 0 ? Radius : Radius * 0.6;
				const int32 LumpAbove = Lump == 0 ? Above : Above * 2 / 3;
				const int32 LumpBelow = Lump == 0 ? Below : Below * 2 / 3;
				for (int32 Y = -LumpBelow; Y <= LumpAbove; ++Y)
				{
					const double T = Y >= 0 ? static_cast<double>(Y) / LumpAbove : static_cast<double>(-Y) / LumpBelow;
					const double R = LumpRadius * (Y >= 0 ? 1.0 - 0.7 * T * T : 1.0 - T * T);
					const int32 Reach = FMath::CeilToInt32(R);
					for (int32 DX = -Reach; DX <= Reach; ++DX)
					{
						for (int32 DZ = -Reach; DZ <= Reach; ++DZ)
						{
							if (DX * DX + DZ * DZ > R * R)
							{
								continue;
							}
							const FIntVector At(Centres[Lump].X + DX, Sea - 1 + Y, Centres[Lump].Z + DZ);
							if (!IsOpen(Region, At))
							{
								continue;
							}
							const bool bTop = Y > 0 && Y >= LumpAbove - 1;
							bPlaced |= Region.SetBlock(At, bTop ? Snow : State);
						}
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- chorus_plant --------------------------------------------------------------------------------------

	class FChorusPlant final : public FMcFeature
	{
	public:
		static void Grow(FMcGenRegion& Region, FMcRandom& Random, TSet<FIntVector>& Plant, TSet<FIntVector>& Flowers, const FIntVector& Start, int32 Height, int32 Depth, int32 MaxDepth)
		{
			static const FMcStateId EndStone = StateOf(TEXT("minecraft:end_stone"));
			for (int32 Step = 0; Step < Height; ++Step)
			{
				const FIntVector At = Start + FIntVector(0, Step, 0);
				if (!Region.IsAir(Region.GetBlock(At)))
				{
					Height = Step;
					break;
				}
				Plant.Add(At);
			}
			const FIntVector Top = Start + FIntVector(0, Height, 0);
			bool bBranched = false;
			if (Depth < MaxDepth && (Depth == 0 || Random.NextInt(4) != 0))
			{
				const int32 Branches = Random.NextInt(4) + 1;
				for (int32 Branch = 0; Branch < Branches && Height > 0; ++Branch)
				{
					const FIntVector Side = Sides[Random.NextInt(4)];
					const FIntVector Next = Top + Side;
					if (Region.IsAir(Region.GetBlock(Next)) && Region.IsAir(Region.GetBlock(Next + Down)) && Region.IsAir(Region.GetBlock(Next + Side)) && Region.IsAir(Region.GetBlock(Next + Side + Down)))
					{
						Plant.Add(Next);
						Grow(Region, Random, Plant, Flowers, Next + Side * 0 + Up * 0 + Side, 1 + Random.NextInt(3) + 0, Depth + 1, MaxDepth);
						Plant.Add(Next + Side);
						bBranched = true;
					}
				}
			}
			if (!bBranched && Height > 0 && Region.IsAir(Region.GetBlock(Top)))
			{
				Flowers.Add(Top);
			}
		}

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			static const FMcStateId EndStone = StateOf(TEXT("minecraft:end_stone"));
			static const FMcStateId PlantBlock = StateOf(TEXT("minecraft:chorus_plant"));
			static const FMcStateId FlowerBlock = StateOf(TEXT("minecraft:chorus_flower"));
			if (!Region.IsAir(Region.GetBlock(Origin)) || Region.GetBlock(Origin + Down) != EndStone)
			{
				return false;
			}
			TSet<FIntVector> Plant, Flowers;
			Grow(Region, Random, Plant, Flowers, Origin, 1 + Random.NextInt(4), 0, 4);
			if (Plant.IsEmpty() && Flowers.IsEmpty())
			{
				return false;
			}
			const auto IsPart = [&](const FIntVector& At) { return Plant.Contains(At) || Flowers.Contains(At); };
			for (const FIntVector& At : Plant)
			{
				FMcStateId State = PlantBlock;
				State = McWithProperty(State, TEXT("up"), IsPart(At + Up) ? TEXT("true") : TEXT("false"));
				State = McWithProperty(State, TEXT("down"), (IsPart(At + Down) || Region.GetBlock(At + Down) == EndStone) ? TEXT("true") : TEXT("false"));
				static const TCHAR* Faces[4] = { TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east") };
				for (int32 Side = 0; Side < 4; ++Side)
				{
					State = McWithProperty(State, Faces[Side], IsPart(At + Sides[Side]) ? TEXT("true") : TEXT("false"));
				}
				Region.SetBlock(At, State);
			}
			for (const FIntVector& At : Flowers)
			{
				Region.SetBlock(At, McWithProperty(FlowerBlock, TEXT("age"), TEXT("5")));
			}
			return true;
		}
	};

	// ---- delta_feature (basalt deltas' lava pools) ------------------------------------------------------------

	class FDelta final : public FMcFeature
	{
	public:
		FMcStateId Contents = 0;
		FMcStateId Rim = 0;
		FMcIntProvider Size;
		FMcIntProvider RimSize;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			// Per the wiki's Delta page: every target block (here the one under the origin, so one layer for the whole
			// pool) needs air above it and no horizontally adjacent air; the interior takes the contents and the
			// edge the rim.
			const int32 SX = Size.Sample(Random);
			const int32 SZ = Size.Sample(Random);
			const int32 RX = RimSize.Sample(Random);
			const int32 RZ = RimSize.Sample(Random);
			const auto Valid = [&](const FIntVector& Target)
			{
				if (!Region.IsAir(Region.GetBlock(Target + Up)))
				{
					return false;
				}
				for (int32 Side = 0; Side < 4; ++Side)
				{
					if (Region.IsAir(Region.GetBlock(Target + Sides[Side])))
					{
						return false;
					}
				}
				return true;
			};
			bool bPlaced = false;
			for (int32 DX = -SX - RX; DX <= SX + RX; ++DX)
			{
				for (int32 DZ = -SZ - RZ; DZ <= SZ + RZ; ++DZ)
				{
					const double Inner = (static_cast<double>(DX * DX) / (SX * SX + 1)) + (static_cast<double>(DZ * DZ) / (SZ * SZ + 1));
					const double Outer = (static_cast<double>(DX * DX) / ((SX + RX) * (SX + RX) + 1)) + (static_cast<double>(DZ * DZ) / ((SZ + RZ) * (SZ + RZ) + 1));
					if (Outer > 1.0)
					{
						continue;
					}
					const FIntVector Target(Origin.X + DX, Origin.Y - 1, Origin.Z + DZ);
					if (Valid(Target))
					{
						bPlaced |= Region.SetBlock(Target, Inner <= 1.0 ? Contents : Rim);
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- end_spike -----------------------------------------------------------------------------------------

	class FEndSpike final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			static const FMcStateId Obsidian = StateOf(TEXT("minecraft:obsidian"));
			static const FMcStateId Bedrock = StateOf(TEXT("minecraft:bedrock"));
			static const FMcStateId Bars = StateOf(TEXT("minecraft:iron_bars"));
			static const FMcStateId Fire = StateOf(TEXT("minecraft:fire"));
			const FIntPoint Chunk(FMath::FloorToInt32(Origin.X / 16.0), FMath::FloorToInt32(Origin.Z / 16.0));
			bool bPlaced = false;
			// Ten pillars in a ring 42 blocks out, getting taller round the ring.
			for (int32 Index = 0; Index < 10; ++Index)
			{
				const double Angle = 2.0 * PI * Index / 10.0;
				const int32 CX = FMath::RoundToInt32(42.0 * FMath::Cos(Angle));
				const int32 CZ = FMath::RoundToInt32(42.0 * FMath::Sin(Angle));
				if (FMath::FloorToInt32(CX / 16.0) != Chunk.X || FMath::FloorToInt32(CZ / 16.0) != Chunk.Y)
				{
					continue;
				}
				const int32 Radius = 2 + Index / 3;
				const int32 Height = 76 + Index * 3;
				const bool bGuarded = Index == 1 || Index == 2;
				for (int32 Y = Region.GetMinY(); Y < Height; ++Y)
				{
					for (int32 DX = -Radius; DX <= Radius; ++DX)
					{
						for (int32 DZ = -Radius; DZ <= Radius; ++DZ)
						{
							if (DX * DX + DZ * DZ <= Radius * Radius + 1)
							{
								bPlaced |= Region.SetBlock(FIntVector(CX + DX, Y, CZ + DZ), Obsidian);
							}
						}
					}
				}
				if (bGuarded)
				{
					for (int32 DX = -2; DX <= 2; ++DX)
					{
						for (int32 DZ = -2; DZ <= 2; ++DZ)
						{
							for (int32 DY = 0; DY <= 3; ++DY)
							{
								if (FMath::Abs(DX) == 2 || FMath::Abs(DZ) == 2 || DY == 3)
								{
									Region.SetBlock(FIntVector(CX + DX, Height + DY, CZ + DZ), Bars);
								}
							}
						}
					}
				}
				Region.SetBlock(FIntVector(CX, Height, CZ), Bedrock);
				Region.SetBlock(FIntVector(CX, Height + 1, CZ), Fire);
			}
			return bPlaced;
		}
	};

	// ---- end_island ----------------------------------------------------------------------------------------

	class FEndIsland final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			static const FMcStateId EndStone = StateOf(TEXT("minecraft:end_stone"));
			float Radius = static_cast<float>(Random.NextInt(3) + 4);
			for (int32 Y = 0; Radius > 0.5f; --Y)
			{
				const int32 Reach = FMath::FloorToInt32(-Radius);
				for (int32 DX = Reach; DX <= FMath::CeilToInt32(Radius); ++DX)
				{
					for (int32 DZ = Reach; DZ <= FMath::CeilToInt32(Radius); ++DZ)
					{
						if (DX * DX + DZ * DZ <= (Radius + 1.0f) * (Radius + 1.0f))
						{
							Region.SetBlock(Origin + FIntVector(DX, Y, DZ), EndStone);
						}
					}
				}
				Radius -= Random.NextInt(2) + 0.5f;
			}
			return true;
		}
	};

	// ---- end_gateway, end_platform, end_podium -------------------------------------------------------------

	class FEndGateway final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			static const FMcStateId Bedrock = StateOf(TEXT("minecraft:bedrock"));
			static const FMcStateId Gateway = StateOf(TEXT("minecraft:end_gateway"));
			static const FMcStateId Air = StateOf(TEXT("minecraft:air"));
			for (int32 DY = -2; DY <= 2; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					for (int32 DZ = -1; DZ <= 1; ++DZ)
					{
						const FIntVector At = Origin + FIntVector(DX, DY, DZ);
						if (DX == 0 && DZ == 0 && DY == 0)
						{
							Region.SetBlock(At, Gateway);
						}
						else if (DY == 0)
						{
							Region.SetBlock(At, Air);
						}
						else if (DX == 0 && DZ == 0)
						{
							Region.SetBlock(At, Bedrock);
						}
						else
						{
							Region.SetBlock(At, Air);
						}
					}
				}
			}
			return true;
		}
	};

	class FEndPlatform final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			static const FMcStateId Obsidian = StateOf(TEXT("minecraft:obsidian"));
			static const FMcStateId Air = StateOf(TEXT("minecraft:air"));
			for (int32 DX = -2; DX <= 2; ++DX)
			{
				for (int32 DZ = -2; DZ <= 2; ++DZ)
				{
					Region.SetBlock(Origin + FIntVector(DX, -1, DZ), Obsidian);
					for (int32 DY = 0; DY <= 2; ++DY)
					{
						Region.SetBlock(Origin + FIntVector(DX, DY, DZ), Air);
					}
				}
			}
			return true;
		}
	};

	class FEndPodium final : public FMcFeature
	{
	public:
		bool bActive = false;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			static const FMcStateId Bedrock = StateOf(TEXT("minecraft:bedrock"));
			static const FMcStateId EndStone = StateOf(TEXT("minecraft:end_stone"));
			static const FMcStateId Portal = StateOf(TEXT("minecraft:end_portal"));
			static const FMcStateId Air = StateOf(TEXT("minecraft:air"));
			static const FMcStateId Torch = StateOf(TEXT("minecraft:wall_torch"));
			for (int32 DY = -1; DY <= 32; ++DY)
			{
				for (int32 DX = -4; DX <= 4; ++DX)
				{
					for (int32 DZ = -4; DZ <= 4; ++DZ)
					{
						const double Distance = FMath::Sqrt(static_cast<double>(DX * DX + DZ * DZ));
						if (Distance > 3.5)
						{
							continue;
						}
						const FIntVector At = Origin + FIntVector(DX, DY, DZ);
						if (DY < 0)
						{
							Region.SetBlock(At, Distance <= 2.5 ? Bedrock : EndStone);
						}
						else if (DY == 0 && Distance <= 2.5)
						{
							Region.SetBlock(At, bActive ? Portal : Air);
						}
						else if (DY > 0)
						{
							Region.SetBlock(At, Air);
						}
					}
				}
			}
			for (int32 DY = 0; DY < 4; ++DY)
			{
				Region.SetBlock(Origin + FIntVector(0, DY, 0), Bedrock);
			}
			static const TCHAR* Facings[4] = { TEXT("south"), TEXT("north"), TEXT("east"), TEXT("west") };
			for (int32 Side = 0; Side < 4; ++Side)
			{
				Region.SetBlock(Origin + FIntVector(0, 2, 0) + Sides[Side], McWithProperty(Torch, TEXT("facing"), Facings[Side]));
			}
			return true;
		}
	};

	// ---- huge_fungus ---------------------------------------------------------------------------------------

	class FHugeFungus final : public FMcFeature
	{
	public:
		FMcStateId Stem = 0;
		FMcStateId Hat = 0;
		FMcStateId Decor = 0;
		FMcStateId ValidBase = 0;
		TSharedPtr<const FMcBlockPredicate> Replaceable;
		bool bPlanted = false;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			if (Region.GetBlock(Origin + Down) != ValidBase)
			{
				return false;
			}
			int32 Height = Random.NextInt(13) + 4;
			if (Random.NextInt(12) == 0)
			{
				Height *= 2;
			}
			Height = FMath::Min(Height, Region.GetMaxY() - Origin.Y - 10);
			const bool bThick = !bPlanted && Random.NextFloat() < 0.06f;
			const int32 Width = bThick ? 2 : 1;
			const auto CanGrow = [&](const FIntVector& At) { return Region.IsAir(Region.GetBlock(At)) || (Replaceable.IsValid() && Replaceable->Test(Region, At)); };
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 DX = 0; DX < Width; ++DX)
				{
					for (int32 DZ = 0; DZ < Width; ++DZ)
					{
						if (!CanGrow(Origin + FIntVector(DX, Y, DZ)))
						{
							if (Y < 4)
							{
								return false;
							}
							Height = Y;
						}
					}
				}
			}
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 DX = 0; DX < Width; ++DX)
				{
					for (int32 DZ = 0; DZ < Width; ++DZ)
					{
						Region.SetBlock(Origin + FIntVector(DX, Y, DZ), Stem);
					}
				}
			}
			// The hat: wide layers at the bottom and a cap, with shroomlights hanging under the rim.
			const int32 HatHeight = bPlanted ? 2 : 3;
			for (int32 Layer = 0; Layer < HatHeight; ++Layer)
			{
				const int32 Y = Height - 1 - Layer + 1 - (HatHeight - 1);
				const double Radius = (Layer == HatHeight - 1 ? 2.0 : 3.5) + (bThick ? 1.0 : 0.0);
				const int32 Reach = FMath::CeilToInt32(Radius);
				for (int32 DX = -Reach; DX <= Reach + Width - 1; ++DX)
				{
					for (int32 DZ = -Reach; DZ <= Reach + Width - 1; ++DZ)
					{
						const double CX = DX - (Width - 1) * 0.5;
						const double CZ = DZ - (Width - 1) * 0.5;
						if (CX * CX + CZ * CZ > Radius * Radius || Random.NextFloat() < 0.04f)
						{
							continue;
						}
						const FIntVector At = Origin + FIntVector(DX, Y, DZ);
						if (CanGrow(At))
						{
							Region.SetBlock(At, Hat);
							if (Layer == 0 && Random.NextFloat() < 0.12f && CanGrow(At + Down))
							{
								Region.SetBlock(At + Down, Decor);
							}
						}
					}
				}
			}
			return true;
		}
	};

	// ---- netherrack_replace_blobs ------------------------------------------------------------------------------

	class FNetherBlob final : public FMcFeature
	{
	public:
		FMcStateId Target = 0;
		FMcStateId State = 0;
		FMcIntProvider Radius;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			FIntVector Start = Origin;
			while (Start.Y > Region.GetMinY() && Region.IsAir(Region.GetBlock(Start)))
			{
				Start.Y--;
			}
			if (Region.GetBlock(Start) != Target)
			{
				return false;
			}
			const int32 RX = Radius.Sample(Random);
			const int32 RY = Radius.Sample(Random);
			const int32 RZ = Radius.Sample(Random);
			const int32 Max = FMath::Max3(RX, RY, RZ);
			bool bPlaced = false;
			for (int32 DX = -RX; DX <= RX; ++DX)
			{
				for (int32 DY = -RY; DY <= RY; ++DY)
				{
					for (int32 DZ = -RZ; DZ <= RZ; ++DZ)
					{
						if (DX * DX + DY * DY + DZ * DZ > Max * Max)
						{
							continue;
						}
						const FIntVector At = Start + FIntVector(DX, DY, DZ);
						if (Region.GetBlock(At) == Target)
						{
							bPlaced |= Region.SetBlock(At, State);
						}
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- root_system (rooted azalea trees) ---------------------------------------------------------------------

	class FRootSystem final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcPlacedFeature> Tree;
		TSharedPtr<const FMcBlockPredicate> AllowedPosition;
		TSharedPtr<const FMcStateProvider> RootState;
		TSharedPtr<const FMcStateProvider> HangingState;
		FBlockSet RootReplaceable;
		int32 RootRadius = 3;
		int32 RootAttempts = 20;
		int32 HangingRadius = 3;
		int32 HangingAttempts = 20;
		int32 HangingSpan = 2;
		int32 ColumnMaxHeight = 100;
		int32 RequiredSpace = 3;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			if (!Region.IsAir(Region.GetBlock(Origin)) && !IsWater(Region.GetBlock(Origin)))
			{
				return false;
			}
			// Up through the roots' soil to the first place a tree may stand.
			FIntVector TreePos = Origin;
			bool bFound = false;
			for (int32 Step = 0; Step < ColumnMaxHeight && TreePos.Y < Region.GetMaxY() - RequiredSpace; ++Step)
			{
				if (AllowedPosition.IsValid() && AllowedPosition->Test(Region, TreePos))
				{
					bFound = true;
					break;
				}
				if (!RootReplaceable.Has(Region.GetBlock(TreePos)) && !Region.IsAir(Region.GetBlock(TreePos)) && !IsWater(Region.GetBlock(TreePos)))
				{
					return false;
				}
				TreePos.Y++;
			}
			if (!bFound || !Tree.IsValid() || !Tree->Place(Context, TreePos))
			{
				return false;
			}
			if (RootState.IsValid())
			{
				for (int32 Attempt = 0; Attempt < RootAttempts; ++Attempt)
				{
					const FIntVector At(Origin.X + Random.NextInt(2 * RootRadius + 1) - RootRadius, Origin.Y + Random.NextInt(FMath::Max(1, TreePos.Y - Origin.Y)), Origin.Z + Random.NextInt(2 * RootRadius + 1) - RootRadius);
					if (RootReplaceable.Has(Region.GetBlock(At)))
					{
						if (const TOptional<FMcStateId> State = RootState->Get(Region, Random, At))
						{
							Region.SetBlock(At, *State);
						}
					}
				}
			}
			if (HangingState.IsValid())
			{
				for (int32 Attempt = 0; Attempt < HangingAttempts; ++Attempt)
				{
					const FIntVector At(TreePos.X + Random.NextInt(2 * HangingRadius + 1) - HangingRadius, TreePos.Y - 1 - Random.NextInt(HangingSpan), TreePos.Z + Random.NextInt(2 * HangingRadius + 1) - HangingRadius);
					if (Region.IsAir(Region.GetBlock(At)) && McIsSolid(Region.GetBlock(At + Up)))
					{
						if (const TOptional<FMcStateId> State = HangingState->Get(Region, Random, At))
						{
							Region.SetBlock(At, *State);
						}
					}
				}
			}
			return true;
		}
	};

	// ---- void_start_platform, bonus_chest ----------------------------------------------------------------------

	class FVoidStartPlatform final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			static const FMcStateId Stone = StateOf(TEXT("minecraft:stone"));
			static const FMcStateId Cobblestone = StateOf(TEXT("minecraft:cobblestone"));
			bool bPlaced = false;
			for (int32 DX = -16; DX <= 16; ++DX)
			{
				for (int32 DZ = -16; DZ <= 16; ++DZ)
				{
					bPlaced |= Region.SetBlock(FIntVector(DX, Origin.Y, DZ), (FMath::Abs(DX) <= 1 && FMath::Abs(DZ) <= 1) ? Cobblestone : Stone);
				}
			}
			return bPlaced;
		}
	};

	// ---- coral_tree, coral_claw, coral_mushroom -------------------------------------------------------------
	// Each shape is a set of positions the configured placed feature (a coral block and what grows on it) is
	// tried at; the feature's own filter keeps it to water.

	class FCoralShape final : public FMcFeature
	{
	public:
		enum class EShape : uint8 { Tree, Claw, Mushroom };
		EShape Shape = EShape::Tree;
		TSharedPtr<const FMcPlacedFeature> Block;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcRandom& Random = Context.Random;
			bool bPlaced = false;
			const auto Try = [&](const FIntVector& At) { return Block->Place(Context, At); };
			// The four horizontal directions, in a random order.
			FIntVector Order[4] = { Sides[0], Sides[1], Sides[2], Sides[3] };
			for (int32 Index = 3; Index > 0; --Index)
			{
				Swap(Order[Index], Order[Random.NextInt(Index + 1)]);
			}
			if (Shape == EShape::Tree)
			{
				const int32 Height = Random.NextInt(3) + 1;
				FIntVector At = Origin;
				for (int32 Step = 0; Step < Height; ++Step)
				{
					if (!Try(At))
					{
						return bPlaced;
					}
					bPlaced = true;
					At += Up;
				}
				const FIntVector Top = At;
				const int32 Branches = Random.NextInt(3) + 2;
				for (int32 Branch = 0; Branch < Branches; ++Branch)
				{
					FIntVector Cursor = Top + Order[Branch];
					const int32 Length = Random.NextInt(5) + 2;
					int32 Rises = 0;
					for (int32 Step = 0; Step < Length; ++Step)
					{
						if (!Try(Cursor))
						{
							break;
						}
						bPlaced = true;
						Cursor += Up;
						if (Step == 0 || Random.NextFloat() < 0.25f)
						{
							Cursor += Order[Branch];
						}
						else if (Rises++ > 2)
						{
							break;
						}
					}
				}
				return bPlaced;
			}
			if (Shape == EShape::Claw)
			{
				if (!Try(Origin))
				{
					return false;
				}
				const int32 Arms = Random.NextInt(2) + 2;
				for (int32 Arm = 0; Arm < Arms; ++Arm)
				{
					const FIntVector Heading = Order[Arm];
					FIntVector Cursor = Origin;
					const int32 Along = Random.NextInt(2) + 2;
					for (int32 Step = 0; Step < Along; ++Step)
					{
						Cursor += Heading;
						if (!Try(Cursor))
						{
							break;
						}
					}
					const FIntVector Curl = Order[(Arm + 1) % 4];
					Cursor += Curl;
					Try(Cursor);
					const int32 Rise = Random.NextInt(2) + 2;
					for (int32 Step = 0; Step < Rise; ++Step)
					{
						Cursor += Up;
						if (!Try(Cursor))
						{
							break;
						}
					}
				}
				return true;
			}
			// A mushroom: a dome of blocks over a short stem.
			const int32 SizeX = Random.NextInt(3) + 3;
			const int32 SizeY = Random.NextInt(3) + 3;
			const int32 SizeZ = Random.NextInt(3) + 3;
			for (int32 DX = 0; DX < SizeX; ++DX)
			{
				for (int32 DY = 0; DY < SizeY; ++DY)
				{
					for (int32 DZ = 0; DZ < SizeZ; ++DZ)
					{
						const bool bEdgeX = DX == 0 || DX == SizeX - 1;
						const bool bEdgeY = DY == 0 || DY == SizeY - 1;
						const bool bEdgeZ = DZ == 0 || DZ == SizeZ - 1;
						if ((bEdgeX && bEdgeY && bEdgeZ) || (!bEdgeX && !bEdgeY && !bEdgeZ) || (bEdgeX && bEdgeZ && !bEdgeY && false))
						{
							continue;
						}
						if (Random.NextFloat() < 0.1f)
						{
							continue;
						}
						bPlaced |= Try(Origin + FIntVector(DX - SizeX / 2, DY, DZ - SizeZ / 2));
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- single_block_pillar -------------------------------------------------------------------------------

	class FSinglePillar final : public FMcFeature
	{
	public:
		FMcStateId State = 0;
		TSharedPtr<const FMcBlockPredicate> CanReplace;
		TSharedPtr<const FMcPlacedFeature> Cap;
		int32 Direction = -1;
		double ContinueChance = 1.0;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			FIntVector At = Origin;
			FIntVector Last = Origin;
			bool bPlaced = false;
			for (int32 Step = 0; Step < 64; ++Step)
			{
				if (!Region.Contains(At.X, At.Z) || At.Y <= Region.GetMinY() || At.Y >= Region.GetMaxY() || !CanReplace->Test(Region, At))
				{
					break;
				}
				Region.SetBlock(At, State);
				bPlaced = true;
				Last = At;
				if (Random.NextDouble() >= ContinueChance)
				{
					break;
				}
				At.Y += Direction;
			}
			if (bPlaced && Cap.IsValid())
			{
				Cap->Place(Context, Last);
			}
			return bPlaced;
		}
	};

	// ---- stepped_column_cluster (basalt columns) ------------------------------------------------------------

	class FSteppedColumns final : public FMcFeature
	{
	public:
		FMcStateId State = 0;
		TSharedPtr<const FMcBlockPredicate> CanReplace;
		TSharedPtr<const FMcBlockPredicate> ContinueThrough;
		FBlockSet CannotPlaceOn;
		FMcIntProvider ClusterReach;
		FMcIntProvider ColumnReach;
		FMcIntProvider Height;
		int32 ColumnCount = 50;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			const int32 Cluster = FMath::Max(1, ClusterReach.Sample(Random));
			bool bPlaced = false;
			for (int32 Attempt = 0; Attempt < ColumnCount; ++Attempt)
			{
				const int32 DX = Random.NextInt(2 * Cluster + 1) - Cluster;
				const int32 DZ = Random.NextInt(2 * Cluster + 1) - Cluster;
				const int32 Tall = Height.Sample(Random);
				const int32 Reach = FMath::Max(0, ColumnReach.Sample(Random) - 1);
				// Columns get shorter away from the middle of the cluster.
				const int32 Away = FMath::Max(FMath::Abs(DX), FMath::Abs(DZ));
				for (int32 FX = -Reach; FX <= Reach; ++FX)
				{
					for (int32 FZ = -Reach; FZ <= Reach; ++FZ)
					{
						const int32 X = Origin.X + DX + FX;
						const int32 Z = Origin.Z + DZ + FZ;
						if (!Region.Contains(X, Z))
						{
							continue;
						}
						// Down from the origin's height to the first thing that isn't open.
						int32 Y = Origin.Y;
						while (Y > Region.GetMinY() + 1 && CanReplace->Test(Region, FIntVector(X, Y - 1, Z)))
						{
							--Y;
						}
						if (!CanReplace->Test(Region, FIntVector(X, Y, Z)) || CannotPlaceOn.Has(Region.GetBlock(FIntVector(X, Y - 1, Z))))
						{
							continue;
						}
						const int32 Rise = FMath::Max(1, Tall - Away / 2 - (FMath::Abs(FX) + FMath::Abs(FZ)));
						for (int32 Step = 0; Step < Rise; ++Step)
						{
							const FIntVector At(X, Y + Step, Z);
							if (!CanReplace->Test(Region, At) && !(ContinueThrough.IsValid() && ContinueThrough->Test(Region, At)))
							{
								break;
							}
							bPlaced |= Region.SetBlock(At, State);
						}
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- sculk_patch ---------------------------------------------------------------------------------------

	class FSculkPatch final : public FMcFeature
	{
	public:
		int32 ChargeCount = 10;
		int32 SpreadAttempts = 64;
		int32 SpreadRounds = 1;
		FBlockSet Replaceable;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			static const FMcStateId Sculk = StateOf(TEXT("minecraft:sculk"));
			static const FMcStateId Vein = FMcBlockRegistry::Get().ParseState(TEXT("minecraft:sculk_vein[down=true]")).Get(0);
			// Only where a cave is: the patch starts at floor level.
			if (!Region.IsAir(Region.GetBlock(Origin)) && !Replaceable.Has(Region.GetBlock(Origin)))
			{
				return false;
			}
			TArray<FIntVector> Spread;
			Spread.Add(Origin);
			const auto IsSurface = [&](const FIntVector& At)
			{
				if (!Region.Contains(At.X, At.Z) || !Replaceable.Has(Region.GetBlock(At)))
				{
					return false;
				}
				for (const FIntVector& Side : { Up, Down, Sides[0], Sides[1], Sides[2], Sides[3] })
				{
					if (Region.IsAir(Region.GetBlock(At + Side)))
					{
						return true;
					}
				}
				return false;
			};
			bool bPlaced = false;
			const int32 Attempts = ChargeCount * (SpreadAttempts / 4 + 4) * FMath::Max(1, SpreadRounds);
			for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
			{
				const FIntVector From = Spread[Random.NextInt(Spread.Num())];
				const FIntVector At = From + FIntVector(Random.NextInt(5) - 2, Random.NextInt(3) - 1, Random.NextInt(5) - 2);
				if (FMath::Abs(At.X - Origin.X) > 8 || FMath::Abs(At.Z - Origin.Z) > 8 || !IsSurface(At))
				{
					continue;
				}
				Region.SetBlock(At, Sculk);
				bPlaced = true;
				Spread.Add(At);
				if (Region.IsAir(Region.GetBlock(At + Up)) && Random.NextInt(7) == 0)
				{
					Region.SetBlock(At + Up, Vein);
				}
			}
			return bPlaced;
		}
	};

	// ---- projected_random_patchy_square (the cap of basalt pillars) ---------------------------------------------

	class FPatchySquare final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcStateProvider> Block;
		TSharedPtr<const FMcBlockPredicate> ProjectThrough;
		FMcIntProvider Size;
		int32 MaxProjection = 3;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			const int32 Radius = Size.Sample(Random);
			bool bPlaced = false;
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				for (int32 DZ = -Radius; DZ <= Radius; ++DZ)
				{
					// Patchy: less likely further out.
					const double Distance = FMath::Sqrt(static_cast<double>(DX * DX + DZ * DZ));
					if (Random.NextDouble() > 1.0 - Distance / (Radius + 1.0))
					{
						continue;
					}
					FIntVector At(Origin.X + DX, Origin.Y, Origin.Z + DZ);
					if (!Region.Contains(At.X, At.Z))
					{
						continue;
					}
					// Down through the open to the first thing, and the block goes on top of it.
					for (int32 Step = 0; Step <= MaxProjection; ++Step)
					{
						if (!ProjectThrough->Test(Region, At))
						{
							break;
						}
						if (!ProjectThrough->Test(Region, At + Down))
						{
							if (const TOptional<FMcStateId> State = Block->Get(Region, Random, At))
							{
								bPlaced |= Region.SetBlock(At, *State);
							}
							break;
						}
						At += Down;
					}
				}
			}
			return bPlaced;
		}
	};

	class FNoOpFeature final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext&, const FIntVector&) const override { return false; }
	};

	// ---- parsing helpers -------------------------------------------------------------------------------------

	FMcStateId StateField(const FJsonObject& Json, const TCHAR* Field, FMcStateId Default = 0)
	{
		const TOptional<FMcStateId> State = McParseBlockState(Json.TryGetField(Field));
		return State.Get(Default);
	}
}

void McRegisterWorldFeatures()
{
	FLibrary::RegisterType(TEXT("large_dripstone"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FLargeDripstone> Result = MakeShared<FLargeDripstone>();
		Result->ColumnRadius.Parse(Json.TryGetField(TEXT("column_radius")));
		Result->HeightScale.Parse(Json.TryGetField(TEXT("height_scale")));
		Result->StalactiteBluntness.Parse(Json.TryGetField(TEXT("stalactite_bluntness")));
		Result->StalagmiteBluntness.Parse(Json.TryGetField(TEXT("stalagmite_bluntness")));
		Json.TryGetNumberField(TEXT("max_column_radius_to_cave_height_ratio"), Result->MaxRadiusRatio);
		if (!Result->Replaceable.Parse(Json.TryGetField(TEXT("replaceable_blocks"))))
		{
			return nullptr;
		}
		return Result;
	});
	FLibrary::RegisterType(TEXT("speleothem_cluster"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FSpeleothemCluster> Result = MakeShared<FSpeleothemCluster>();
		Result->Base = StateField(Json, TEXT("base_block"));
		Result->Pointed = StateField(Json, TEXT("pointed_block"));
		Result->Radius.Parse(Json.TryGetField(TEXT("radius")));
		Result->Height.Parse(Json.TryGetField(TEXT("height")));
		Result->Thickness.Parse(Json.TryGetField(TEXT("speleothem_block_layer_thickness")));
		Result->Density.Parse(Json.TryGetField(TEXT("density")));
		Json.TryGetNumberField(TEXT("floor_to_ceiling_search_range"), Result->SearchRange);
		Json.TryGetNumberField(TEXT("height_deviation"), Result->HeightDeviation);
		Json.TryGetNumberField(TEXT("chance_of_speleothem_at_max_distance_from_center"), Result->EdgeChance);
		if (!Result->Replaceable.Parse(Json.TryGetField(TEXT("replaceable_blocks"))))
		{
			return nullptr;
		}
		return Result;
	});
	FLibrary::RegisterType(TEXT("iceberg"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FIceberg> Result = MakeShared<FIceberg>();
		Result->State = StateField(Json, TEXT("state"));
		return Result;
	});
	FLibrary::RegisterType(TEXT("chorus_plant"), [](const FJsonObject&, FLibrary&) { return MakeShared<FChorusPlant>(); });
	FLibrary::RegisterType(TEXT("delta_feature"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FDelta> Result = MakeShared<FDelta>();
		Result->Contents = StateField(Json, TEXT("contents"));
		Result->Rim = StateField(Json, TEXT("rim"));
		Result->Size.Parse(Json.TryGetField(TEXT("size")));
		Result->RimSize.Parse(Json.TryGetField(TEXT("rim_size")));
		return Result;
	});
	FLibrary::RegisterType(TEXT("end_spike"), [](const FJsonObject&, FLibrary&) { return MakeShared<FEndSpike>(); });
	FLibrary::RegisterType(TEXT("end_island"), [](const FJsonObject&, FLibrary&) { return MakeShared<FEndIsland>(); });
	FLibrary::RegisterType(TEXT("end_gateway"), [](const FJsonObject&, FLibrary&) { return MakeShared<FEndGateway>(); });
	FLibrary::RegisterType(TEXT("end_platform"), [](const FJsonObject&, FLibrary&) { return MakeShared<FEndPlatform>(); });
	FLibrary::RegisterType(TEXT("end_podium"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FEndPodium> Result = MakeShared<FEndPodium>();
		Json.TryGetBoolField(TEXT("active"), Result->bActive);
		return Result;
	});
	FLibrary::RegisterType(TEXT("huge_fungus"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FHugeFungus> Result = MakeShared<FHugeFungus>();
		Result->Stem = StateField(Json, TEXT("stem_state"));
		Result->Hat = StateField(Json, TEXT("hat_state"));
		Result->Decor = StateField(Json, TEXT("decor_state"));
		Result->ValidBase = StateField(Json, TEXT("valid_base_block"));
		Result->Replaceable = FMcBlockPredicate::Parse(Json.TryGetField(TEXT("replaceable_blocks")), Library.GetSeaLevel());
		Json.TryGetBoolField(TEXT("planted"), Result->bPlanted);
		return Result;
	});
	FLibrary::RegisterType(TEXT("netherrack_replace_blobs"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FNetherBlob> Result = MakeShared<FNetherBlob>();
		Result->Target = StateField(Json, TEXT("target"));
		Result->State = StateField(Json, TEXT("state"));
		Result->Radius.Parse(Json.TryGetField(TEXT("radius")));
		return Result;
	});
	FLibrary::RegisterType(TEXT("root_system"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FRootSystem> Result = MakeShared<FRootSystem>();
		const TSharedPtr<FJsonValue> Tree = Json.TryGetField(TEXT("feature"));
		Result->Tree = Tree.IsValid() ? Library.ParsePlaced(Tree) : nullptr;
		Result->AllowedPosition = FMcBlockPredicate::Parse(Json.TryGetField(TEXT("allowed_tree_position")), Library.GetSeaLevel());
		Result->RootState = FMcStateProvider::Parse(Json.TryGetField(TEXT("root_state_provider")));
		Result->HangingState = FMcStateProvider::Parse(Json.TryGetField(TEXT("hanging_root_state_provider")));
		Result->RootReplaceable.Parse(Json.TryGetField(TEXT("root_replaceable")));
		Json.TryGetNumberField(TEXT("root_radius"), Result->RootRadius);
		Json.TryGetNumberField(TEXT("root_placement_attempts"), Result->RootAttempts);
		Json.TryGetNumberField(TEXT("hanging_root_radius"), Result->HangingRadius);
		Json.TryGetNumberField(TEXT("hanging_root_placement_attempts"), Result->HangingAttempts);
		Json.TryGetNumberField(TEXT("hanging_roots_vertical_span"), Result->HangingSpan);
		Json.TryGetNumberField(TEXT("root_column_max_height"), Result->ColumnMaxHeight);
		Json.TryGetNumberField(TEXT("required_vertical_space_for_tree"), Result->RequiredSpace);
		if (!Result->Tree.IsValid())
		{
			return nullptr;
		}
		return Result;
	});
	const auto RegisterCoral = [](const TCHAR* Type, FCoralShape::EShape Shape)
	{
		FLibrary::RegisterType(Type, [Shape](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
		{
			TSharedRef<FCoralShape> Result = MakeShared<FCoralShape>();
			Result->Shape = Shape;
			const TSharedPtr<FJsonValue> Block = Json.TryGetField(TEXT("feature"));
			Result->Block = Block.IsValid() ? Library.ParsePlaced(Block) : nullptr;
			if (!Result->Block.IsValid())
			{
				return nullptr;
			}
			return Result;
		});
	};
	RegisterCoral(TEXT("coral_tree"), FCoralShape::EShape::Tree);
	RegisterCoral(TEXT("coral_claw"), FCoralShape::EShape::Claw);
	RegisterCoral(TEXT("coral_mushroom"), FCoralShape::EShape::Mushroom);
	FLibrary::RegisterType(TEXT("single_block_pillar"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FSinglePillar> Result = MakeShared<FSinglePillar>();
		Result->State = StateField(Json, TEXT("block"));
		Result->CanReplace = FMcBlockPredicate::Parse(Json.TryGetField(TEXT("can_replace")), Library.GetSeaLevel());
		const TSharedPtr<FJsonValue> Cap = Json.TryGetField(TEXT("cap_feature"));
		Result->Cap = Cap.IsValid() ? Library.ParsePlaced(Cap) : nullptr;
		FString Direction;
		Result->Direction = Json.TryGetStringField(TEXT("direction"), Direction) && Direction == TEXT("up") ? 1 : -1;
		Json.TryGetNumberField(TEXT("chance_to_continue"), Result->ContinueChance);
		if (!Result->CanReplace.IsValid())
		{
			return nullptr;
		}
		return Result;
	});
	FLibrary::RegisterType(TEXT("projected_random_patchy_square"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FPatchySquare> Result = MakeShared<FPatchySquare>();
		Result->Block = FMcStateProvider::Parse(Json.TryGetField(TEXT("block")));
		Result->ProjectThrough = FMcBlockPredicate::Parse(Json.TryGetField(TEXT("project_through")), Library.GetSeaLevel());
		Result->Size.Parse(Json.TryGetField(TEXT("size")));
		Json.TryGetNumberField(TEXT("max_projection_height"), Result->MaxProjection);
		if (!Result->Block.IsValid() || !Result->ProjectThrough.IsValid())
		{
			return nullptr;
		}
		return Result;
	});
	FLibrary::RegisterType(TEXT("stepped_column_cluster"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FSteppedColumns> Result = MakeShared<FSteppedColumns>();
		Result->State = StateField(Json, TEXT("block"));
		Result->CanReplace = FMcBlockPredicate::Parse(Json.TryGetField(TEXT("can_replace")), Library.GetSeaLevel());
		Result->ContinueThrough = FMcBlockPredicate::Parse(Json.TryGetField(TEXT("continue_through")), Library.GetSeaLevel());
		Result->CannotPlaceOn.Parse(Json.TryGetField(TEXT("cannot_place_on")));
		Result->ClusterReach.Parse(Json.TryGetField(TEXT("cluster_reach")));
		Result->ColumnReach.Parse(Json.TryGetField(TEXT("column_reach")));
		Result->Height.Parse(Json.TryGetField(TEXT("height")));
		Json.TryGetNumberField(TEXT("column_count"), Result->ColumnCount);
		if (!Result->CanReplace.IsValid())
		{
			return nullptr;
		}
		return Result;
	});
	FLibrary::RegisterType(TEXT("sculk_patch"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FSculkPatch> Result = MakeShared<FSculkPatch>();
		Json.TryGetNumberField(TEXT("charge_count"), Result->ChargeCount);
		Json.TryGetNumberField(TEXT("spread_attempts"), Result->SpreadAttempts);
		Json.TryGetNumberField(TEXT("spread_rounds"), Result->SpreadRounds);
		Result->Replaceable.Parse(MakeShared<FJsonValueString>(TEXT("#minecraft:sculk_replaceable")));
		return Result;
	});
	FLibrary::RegisterType(TEXT("void_start_platform"), [](const FJsonObject&, FLibrary&) { return MakeShared<FVoidStartPlatform>(); });
	// Chests are block entities, which chunks don't hold yet.
	FLibrary::RegisterType(TEXT("bonus_chest"), [](const FJsonObject&, FLibrary&) { return MakeShared<FNoOpFeature>(); });
}
