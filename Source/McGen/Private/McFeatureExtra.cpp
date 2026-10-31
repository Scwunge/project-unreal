// More feature types: patches (moss), bamboo, mushrooms, ice, dripstone, geodes, monster rooms and a few
// nether ones. What each does is from minecraft.wiki; the shapes are our own, sized like the wiki says.

#include "McFeatureCommon.h"
#include "McFeatureTypes.h"
#include "Dom/JsonObject.h"
#include "McBlockPredicate.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McProviders.h"
#include "McRandom.h"
#include "McStateProvider.h"

namespace
{
	using namespace McFeatureCommon;

	// ---- vegetation_patch ----------------------------------------------------------------------------------

	// A layer of ground blocks (moss, clay) laid over a cave floor or ceiling or the land, with plants on it.
	class FVegetationPatch final : public FMcFeature
	{
	public:
		FBlockSet Replaceable;
		TSharedPtr<const FMcStateProvider> Ground;
		TSharedPtr<const FMcPlacedFeature> Vegetation;
		bool bCeiling = false;
		bool bWaterlogged = false;
		int32 Depth = 1;
		int32 VerticalRange = 5;
		double ExtraBottom = 0.0;
		double ExtraEdge = 0.0;
		double VegetationChance = 0.0;
		FMcIntProvider Radius;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			const int32 RX = Radius.Sample(Random) + 1;
			const int32 RZ = Radius.Sample(Random) + 1;
			const FIntVector Toward = bCeiling ? Up : Down;
			bool bPlaced = false;
			int64 GroundBlocks = 0;
			for (int32 DX = -RX; DX <= RX; ++DX)
			{
				for (int32 DZ = -RZ; DZ <= RZ; ++DZ)
				{
					const bool bEdgeX = FMath::Abs(DX) == RX;
					const bool bEdgeZ = FMath::Abs(DZ) == RZ;
					if ((bEdgeX && bEdgeZ) || ((bEdgeX || bEdgeZ) && Random.NextDouble() >= ExtraEdge))
					{
						continue;
					}
					// The air block next to this column's surface, searched out from the origin's height.
					FIntVector Pos(Origin.X + DX, Origin.Y, Origin.Z + DZ);
					const auto IsOpen = [&](const FIntVector& At) { return Region.IsAir(Region.GetBlock(At)) || (bWaterlogged && IsWater(Region.GetBlock(At))); };
					bool bFound = false;
					for (int32 Step = 0; Step < VerticalRange && !bFound; ++Step)
					{
						if (IsOpen(Pos))
						{
							const FIntVector Beyond = Pos + Toward;
							if (!IsOpen(Beyond))
							{
								bFound = Replaceable.Has(Region.GetBlock(Beyond));
								if (!bFound)
								{
									break;
								}
							}
							else
							{
								Pos += Toward;
							}
						}
						else
						{
							Pos -= Toward;
						}
					}
					if (!bFound)
					{
						continue;
					}
					int32 Layers = Depth + (Random.NextDouble() < ExtraBottom ? 1 : 0);
					FIntVector At = Pos + Toward;
					for (; Layers > 0 && Replaceable.Has(Region.GetBlock(At)); --Layers, At += Toward)
					{
						if (const TOptional<FMcStateId> State = Ground->Get(Region, Random, At))
						{
							bPlaced |= Region.SetBlock(At, State.GetValue());
							++GroundBlocks;
						}
					}
					if (Layers < Depth + 1 && Random.NextDouble() < VegetationChance && Vegetation.IsValid())
					{
						FMcFeatureContext Inner{ Region, Random, Context.Library, Context.SeaLevel, nullptr };
						Vegetation->Place(Inner, Pos);
					}
				}
			}
			McCountBlocks(bCeiling ? TEXT("vegetation_patch ceiling") : TEXT("vegetation_patch floor"), GroundBlocks);
			return bPlaced;
		}
	};

	// ---- bamboo --------------------------------------------------------------------------------------------

	class FBamboo final : public FMcFeature
	{
	public:
		double PodzolChance = 0.0;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			static const FMcStateId Bamboo = StateOf(TEXT("minecraft:bamboo"));
			static const FMcStateId PodzolState = StateOf(TEXT("minecraft:podzol"));
			if (!Region.IsAir(Region.GetBlock(Origin)) || !FMcBlockPredicate::CanSurvive(Region, Origin, Bamboo))
			{
				return false;
			}
			const int32 Height = Random.NextIntBetween(5, 14);
			if (PodzolChance > 0.0 && Random.NextDouble() < PodzolChance)
			{
				for (int32 DX = -3; DX <= 3; ++DX)
				{
					for (int32 DZ = -3; DZ <= 3; ++DZ)
					{
						const FIntVector Ground = Origin + FIntVector(DX, -1, DZ);
						if (DX * DX + DZ * DZ <= 9 && FMcBlockTags::Get().Has(TEXT("minecraft:dirt"), Region.GetBlock(Ground)) && Region.IsAir(Region.GetBlock(Ground + Up)))
						{
							Region.SetBlock(Ground, PodzolState);
						}
					}
				}
			}
			const FMcStateId Thick = McWithProperty(Bamboo, TEXT("age"), TEXT("1"));
			for (int32 Y = 0; Y < Height; ++Y)
			{
				const FIntVector Pos = Origin + FIntVector(0, Y, 0);
				if (!Region.IsAir(Region.GetBlock(Pos)))
				{
					break;
				}
				FMcStateId State = Thick;
				if (Y == Height - 1)
				{
					State = McWithProperty(McWithProperty(State, TEXT("leaves"), TEXT("large")), TEXT("stage"), TEXT("1"));
				}
				else if (Y >= Height - 3)
				{
					State = McWithProperty(State, TEXT("leaves"), TEXT("small"));
				}
				Region.SetBlock(Pos, State);
			}
			return true;
		}
	};

	// ---- underwater_magma ----------------------------------------------------------------------------------

	class FUnderwaterMagma final : public FMcFeature
	{
	public:
		int32 SearchRange = 5;
		int32 Radius = 1;
		double Chance = 0.5;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			static const FMcStateId Magma = StateOf(TEXT("minecraft:magma_block"));
			FIntVector Floor = Origin;
			int32 Steps = 0;
			while (IsWater(Region.GetBlock(Floor + Down)) && Steps++ < SearchRange)
			{
				Floor += Down;
			}
			if (!IsWater(Region.GetBlock(Floor)) || !McIsSolid(Region.GetBlock(Floor + Down)))
			{
				return false;
			}
			bool bPlaced = false;
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				for (int32 DY = -Radius - 1; DY <= 0; ++DY)
				{
					for (int32 DZ = -Radius; DZ <= Radius; ++DZ)
					{
						const FIntVector Pos = Floor + FIntVector(DX, DY, DZ);
						if (McIsSolid(Region.GetBlock(Pos)) && IsWater(Region.GetBlock(Pos + Up)) && McFeature::CanReplace(Region.GetBlock(Pos)) && Context.Random.NextDouble() < Chance)
						{
							bPlaced |= Region.SetBlock(Pos, Magma);
						}
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- huge mushrooms ------------------------------------------------------------------------------------

	class FHugeMushroom final : public FMcFeature
	{
	public:
		bool bRed = true;
		TSharedPtr<const FMcBlockPredicate> CanPlaceOn;
		TSharedPtr<const FMcStateProvider> Cap;
		TSharedPtr<const FMcStateProvider> Stem;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			const int32 Height = Random.NextInt(3) + 4 + (Random.NextInt(12) == 0 ? Height2(Random) : 0);
			const int32 CapRadius = bRed ? 2 : 3;
			if (!CanPlaceOn->Test(Region, Origin + Down))
			{
				return false;
			}
			// Room for the stem and the cap: air or leaves only.
			for (int32 Y = 0; Y <= Height + 1; ++Y)
			{
				const int32 Reach = Y < Height - (bRed ? 3 : 0) ? 0 : CapRadius;
				for (int32 DX = -Reach; DX <= Reach; ++DX)
				{
					for (int32 DZ = -Reach; DZ <= Reach; ++DZ)
					{
						const FMcStateId State = Region.GetBlock(Origin + FIntVector(DX, Y, DZ));
						if (!Region.IsAir(State) && !FMcBlockTags::Get().Has(TEXT("minecraft:leaves"), State))
						{
							return false;
						}
					}
				}
			}
			const auto Put = [&](const FIntVector& Pos, const TSharedPtr<const FMcStateProvider>& Provider)
			{
				if (const TOptional<FMcStateId> State = Provider->Get(Region, Random, Pos))
				{
					Region.SetBlock(Pos, State.GetValue());
				}
			};
			for (int32 Y = 0; Y < Height; ++Y)
			{
				Put(Origin + FIntVector(0, Y, 0), Stem);
			}
			if (bRed)
			{
				// A dome: a ring of Radius 2 at the top four layers, narrowing to the top.
				for (int32 Y = Height - 3; Y <= Height; ++Y)
				{
					const int32 Reach = Y == Height ? 1 : 2;
					for (int32 DX = -Reach; DX <= Reach; ++DX)
					{
						for (int32 DZ = -Reach; DZ <= Reach; ++DZ)
						{
							const bool bInside = FMath::Abs(DX) < Reach && FMath::Abs(DZ) < Reach;
							if ((Y < Height && bInside) || (FMath::Abs(DX) == 2 && FMath::Abs(DZ) == 2))
							{
								continue;
							}
							Put(Origin + FIntVector(DX, Y, DZ), Cap);
						}
					}
				}
			}
			else
			{
				for (int32 DX = -CapRadius; DX <= CapRadius; ++DX)
				{
					for (int32 DZ = -CapRadius; DZ <= CapRadius; ++DZ)
					{
						if (FMath::Abs(DX) == CapRadius && FMath::Abs(DZ) == CapRadius)
						{
							continue;
						}
						Put(Origin + FIntVector(DX, Height, DZ), Cap);
					}
				}
			}
			return true;
		}

	private:
		static int32 Height2(FMcRandom& Random) { return Random.NextInt(3); }
	};

	// ---- spike (ice spikes) --------------------------------------------------------------------------------

	class FSpike final : public FMcFeature
	{
	public:
		FMcStateId State = 0;
		TSharedPtr<const FMcBlockPredicate> CanPlaceOn;
		TSharedPtr<const FMcBlockPredicate> CanReplace;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			FIntVector Base = Origin;
			while (Region.IsAir(Region.GetBlock(Base)) && Base.Y > Region.GetMinY() + 2)
			{
				Base += Down;
			}
			if (!CanPlaceOn->Test(Region, Base))
			{
				return false;
			}
			Base += Up * 1;
			const auto Cone = [&](const FIntVector& At, int32 Height, int32 Width)
			{
				for (int32 Y = -2; Y < Height; ++Y)
				{
					const int32 Reach = Y < 0 ? Width : FMath::CeilToInt32(Width * (1.0f - static_cast<float>(Y) / Height));
					for (int32 DX = -Reach; DX <= Reach; ++DX)
					{
						for (int32 DZ = -Reach; DZ <= Reach; ++DZ)
						{
							if (DX * DX + DZ * DZ > Reach * Reach + Reach / 2)
							{
								continue;
							}
							const FIntVector Pos = At + FIntVector(DX, Y, DZ);
							if (CanReplace->Test(Region, Pos) || Region.IsAir(Region.GetBlock(Pos)))
							{
								Region.SetBlock(Pos, State);
							}
						}
					}
				}
			};
			Cone(Base, Random.NextInt(4) + 10 + Random.NextInt(24), 1 + Random.NextInt(3));
			for (int32 Index = Random.NextInt(4); Index > 0; --Index)
			{
				const FIntVector At = Base + FIntVector(Random.NextInt(9) - 4, 0, Random.NextInt(9) - 4);
				Cone(At, 3 + Random.NextInt(5), 0);
			}
			return true;
		}
	};

	// ---- blue_ice ------------------------------------------------------------------------------------------

	class FBlueIce final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			static const FMcStateId Blue = StateOf(TEXT("minecraft:blue_ice"));
			if (Origin.Y > Context.SeaLevel - 1 || !IsWater(Region.GetBlock(Origin)))
			{
				return false;
			}
			bool bPlaced = false;
			for (int32 Attempt = 0; Attempt < 200; ++Attempt)
			{
				const FIntVector Pos = Origin + FIntVector(Random.NextInt(8) - Random.NextInt(8), -Random.NextInt(3), Random.NextInt(8) - Random.NextInt(8));
				const FName Name = FMcBlockRegistry::Get().GetBlockOf(Region.GetBlock(Pos)).Name;
				// Only inside existing ice (icebergs): turning open water blue gave 39% of a frozen ocean's surface
				// blue ice against none in the reference client.
				if (Name == TEXT("minecraft:packed_ice") || Name == TEXT("minecraft:ice") || Name == TEXT("minecraft:snow_block"))
				{
					bPlaced |= Region.SetBlock(Pos, Blue);
				}
			}
			return bPlaced;
		}
	};

	// ---- multiface_growth (glow lichen, sculk veins) -------------------------------------------------------

	class FMultiface final : public FMcFeature
	{
	public:
		FMcStateId Block = 0;
		FBlockSet PlaceOn;
		bool bFloor = true;
		bool bCeiling = true;
		bool bWall = true;
		int32 SearchRange = 10;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			const FIntVector Directions[6] = { Up, Down, Sides[0], Sides[1], Sides[2], Sides[3] };
			static const TCHAR* Faces[6] = { TEXT("up"), TEXT("down"), TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east") };
			const FName BlockName = FMcBlockRegistry::Get().GetBlockOf(Block).Name;
			// A face is allowed on this side if the allowed floor/ceiling/wall kinds say so.
			const auto Allowed = [&](int32 Side) { return Side == 0 ? bCeiling : (Side == 1 ? bFloor : bWall); };
			const auto TryAt = [&](const FIntVector& Pos) -> bool
			{
				const FMcStateId Here = Region.GetBlock(Pos);
				const bool bSame = FMcBlockRegistry::Get().GetBlockOf(Here).Name == BlockName;
				if (!Region.IsAir(Here) && !bSame)
				{
					return false;
				}
				const int32 Start = Random.NextInt(6);
				for (int32 Index = 0; Index < 6; ++Index)
				{
					const int32 Side = (Start + Index) % 6;
					if (!Allowed(Side) || !PlaceOn.Has(Region.GetBlock(Pos + Directions[Side])))
					{
						continue;
					}
					FMcStateId Cleared = bSame ? Here : Block;
					if (!bSame)
					{
						for (const TCHAR* Face : Faces)
						{
							Cleared = McWithProperty(Cleared, Face, TEXT("false"));
						}
					}
					const FMcStateId State = McWithProperty(Cleared, Faces[Side], TEXT("true"));
					return State != Here && Region.SetBlock(Pos, State);
				}
				return false;
			};
			bool bPlaced = TryAt(Origin);
			// Then spread out from where it took.
			const int32 Spread = FMath::Min(SearchRange, 8);
			for (int32 Attempt = 0; Attempt < 12 && bPlaced; ++Attempt)
			{
				bPlaced |= TryAt(Origin + FIntVector(Random.NextInt(2 * Spread + 1) - Spread, Random.NextInt(5) - 2, Random.NextInt(2 * Spread + 1) - Spread));
			}
			return bPlaced;
		}
	};

	// ---- random_neighbor_spread (glowstone blobs) ----------------------------------------------------------

	class FNeighborSpread final : public FMcFeature
	{
	public:
		FMcStateId State = 0;
		FBlockSet Accepted;
		TSharedPtr<const FMcBlockPredicate> CanReplace;
		int32 Attempts = 0;
		FMcIntProvider XZ;
		FMcIntProvider Y;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			if (!CanReplace->Test(Region, Origin))
			{
				return false;
			}
			Region.SetBlock(Origin, State);
			for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
			{
				const FIntVector Pos = Origin + FIntVector(XZ.Sample(Random), Y.Sample(Random), XZ.Sample(Random));
				if (!CanReplace->Test(Region, Pos))
				{
					continue;
				}
				for (const FIntVector& Side : { Up, Down, Sides[0], Sides[1], Sides[2], Sides[3] })
				{
					if (Accepted.Has(Region.GetBlock(Pos + Side)))
					{
						Region.SetBlock(Pos, State);
						break;
					}
				}
			}
			return true;
		}
	};

	// ---- speleothem (pointed dripstone) --------------------------------------------------------------------

	class FSpeleothem final : public FMcFeature
	{
	public:
		FMcStateId Base = 0;
		FMcStateId Pointed = 0;
		FBlockSet Replaceable;

		// A stalagmite (Direction up) or stalactite (down) of Length with its root at Pos.
		static void Grow(FMcGenRegion& Region, const FIntVector& Pos, int32 Direction, int32 Length, FMcStateId Pointed)
		{
			GrowPointed(Region, Pos, Direction, Length, Pointed);
		}

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			int32 Direction = 0;
			if (McIsSolid(Region.GetBlock(Origin + Down)) && Replaceable.Has(Region.GetBlock(Origin + Down)))
			{
				Direction = 1;
			}
			else if (McIsSolid(Region.GetBlock(Origin + Up)) && Replaceable.Has(Region.GetBlock(Origin + Up)))
			{
				Direction = -1;
			}
			if (Direction == 0 || !(Region.IsAir(Region.GetBlock(Origin)) || IsWater(Region.GetBlock(Origin))))
			{
				return false;
			}
			Region.SetBlock(Origin - FIntVector(0, Direction, 0), Base);
			Grow(Region, Origin, Direction, 1 + Random.NextInt(3) + (Random.NextInt(4) == 0 ? 1 : 0), Pointed);
			return true;
		}
	};

	// ---- geode ---------------------------------------------------------------------------------------------

	class FGeode final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcStateProvider> Filling, Inner, AlternateInner, Middle, Outer;
		TArray<FMcStateId> Buds;
		FBlockSet Invalid;
		FBlockSet CannotReplace;
		FMcIntProvider Distance;
		double CrackChance = 0.0;
		double AlternateChance = 0.0;
		int32 InvalidThreshold = 1;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			// Layer radii: the wiki's 1.7 / 2.2 / 3.2 / 4.2 gave 1/12 of the reference client's amethyst and 1/3 of its calcite
			// per column, so the shells are widened to fit those counts (5 worlds, 25 patches each).
			constexpr double FillingR = 1.3;
			constexpr double InnerR = 3.8;
			constexpr double MiddleR = 4.7;
			constexpr double OuterR = 5.4;
			// The geode sits a random distance in from the edge of a box: check the box corners and middle.
			const double Scale = Distance.Sample(Random) / 4.2;
			const int32 Reach = FMath::CeilToInt32(OuterR * Scale) + 1;
			int32 Bad = 0;
			for (int32 Index = 0; Index < 14; ++Index)
			{
				const FIntVector Probe = Origin + FIntVector((Index & 1 ? 1 : -1) * Reach, (Index & 2 ? 1 : -1) * Reach, (Index & 4 ? 1 : -1) * Reach);
				const FIntVector Pos = Index < 8 ? Probe : (Origin + Sides[Index - 8 < 4 ? Index - 8 : 0] * Reach);
				if (Pos.Y < Region.GetMinY() || Pos.Y > Region.GetMaxY() || Invalid.Has(Region.GetBlock(Pos)))
				{
					if (++Bad >= InvalidThreshold)
					{
						return false;
					}
				}
			}
			const FIntVector Crack(Random.NextDouble() < CrackChance ? 1 : 0, 0, 0);
			TArray<FIntVector> InnerCells;
			bool bPlaced = false;
			for (int32 DX = -Reach; DX <= Reach; ++DX)
			{
				for (int32 DY = -Reach; DY <= Reach; ++DY)
				{
					for (int32 DZ = -Reach; DZ <= Reach; ++DZ)
					{
						const double D = FMath::Sqrt(static_cast<double>(DX * DX + DY * DY + DZ * DZ)) / Scale;
						if (D > OuterR)
						{
							continue;
						}
						const FIntVector Pos = Origin + FIntVector(DX, DY, DZ);
						if (CannotReplace.Has(Region.GetBlock(Pos)))
						{
							continue;
						}
						// A thin slot cut through one side (the crack).
						if (Crack.X && DZ == 0 && DY >= 0 && DX > 0 && FMath::Abs(DY) <= 1 && D > FillingR)
						{
							Region.SetBlock(Pos, Filling->Get(Region, Random, Pos).Get(0));
							continue;
						}
						TSharedPtr<const FMcStateProvider> Provider;
						if (D <= FillingR) Provider = Filling;
						else if (D <= InnerR) Provider = Random.NextDouble() < AlternateChance ? AlternateInner : Inner;
						else if (D <= MiddleR) Provider = Middle;
						else Provider = Outer;
						if (const TOptional<FMcStateId> State = Provider->Get(Region, Random, Pos))
						{
							bPlaced |= Region.SetBlock(Pos, State.GetValue());
						}
						if (D > FillingR && D <= InnerR)
						{
							InnerCells.Add(Pos);
						}
					}
				}
			}
			static const FIntVector Directions[6] = { Up, Down, Sides[0], Sides[1], Sides[2], Sides[3] };
			static const TCHAR* Facing[6] = { TEXT("up"), TEXT("down"), TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east") };
			for (const FIntVector& Cell : InnerCells)
			{
				if (Buds.IsEmpty() || Random.NextDouble() >= 0.35)
				{
					continue;
				}
				const int32 Side = Random.NextInt(6);
				const FIntVector At = Cell + Directions[Side];
				if (Region.IsAir(Region.GetBlock(At)))
				{
					Region.SetBlock(At, McWithProperty(Buds[Random.NextInt(Buds.Num())], TEXT("facing"), Facing[Side]));
				}
			}
			return bPlaced;
		}
	};

	// ---- monster_room --------------------------------------------------------------------------------------

	class FMonsterRoom final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			static const FMcStateId Cobble = StateOf(TEXT("minecraft:cobblestone"));
			static const FMcStateId Mossy = StateOf(TEXT("minecraft:mossy_cobblestone"));
			static const FMcStateId Air = StateOf(TEXT("minecraft:cave_air"));
			static const FMcStateId Chest = StateOf(TEXT("minecraft:chest"));
			static const FMcStateId Spawner = StateOf(TEXT("minecraft:spawner"));
			const int32 RX = Random.NextInt(2) + 2;
			const int32 RZ = Random.NextInt(2) + 2;
			int32 Openings = 0;
			for (int32 X = -RX - 1; X <= RX + 1; ++X)
			{
				for (int32 Y = -1; Y <= 4; ++Y)
				{
					for (int32 Z = -RZ - 1; Z <= RZ + 1; ++Z)
					{
						const FIntVector Pos = Origin + FIntVector(X, Y, Z);
						const bool bSolid = Region.GetBlock(Pos) != Air && McIsSolid(Region.GetBlock(Pos));
						if ((Y == -1 || Y == 4) && !bSolid)
						{
							return false;
						}
						const bool bWall = FMath::Abs(X) == RX + 1 || FMath::Abs(Z) == RZ + 1;
						if (bWall && Y == 0 && Region.IsAir(Region.GetBlock(Pos)) && Region.IsAir(Region.GetBlock(Pos + Up)))
						{
							++Openings;
						}
					}
				}
			}
			if (Openings < 1 || Openings > 5)
			{
				return false;
			}
			for (int32 X = -RX - 1; X <= RX + 1; ++X)
			{
				for (int32 Y = 3; Y >= -1; --Y)
				{
					for (int32 Z = -RZ - 1; Z <= RZ + 1; ++Z)
					{
						const FIntVector Pos = Origin + FIntVector(X, Y, Z);
						const bool bShell = FMath::Abs(X) == RX + 1 || FMath::Abs(Z) == RZ + 1 || Y == -1;
						if (!bShell)
						{
							Region.SetBlock(Pos, Air);
						}
						else if (Y >= 0 && !McIsSolid(Region.GetBlock(Pos)))
						{
							Region.SetBlock(Pos, Air);
						}
						else if (McIsSolid(Region.GetBlock(Pos)) && McFeature::CanReplace(Region.GetBlock(Pos)))
						{
							Region.SetBlock(Pos, Y == -1 && Random.NextInt(4) != 0 ? Mossy : Cobble);
						}
					}
				}
			}
			for (int32 Chests = 0; Chests < 2; ++Chests)
			{
				for (int32 Try = 0; Try < 3; ++Try)
				{
					const FIntVector Pos = Origin + FIntVector(Random.NextInt(2 * RX + 1) - RX, 0, Random.NextInt(2 * RZ + 1) - RZ);
					if (Region.IsAir(Region.GetBlock(Pos)) && (FMath::Abs(Pos.X - Origin.X) == RX || FMath::Abs(Pos.Z - Origin.Z) == RZ))
					{
						Region.SetBlock(Pos, McWithProperty(Chest, TEXT("facing"), TEXT("south")));
						break;
					}
				}
			}
			Region.SetBlock(Origin, Spawner);
			return true;
		}
	};

	// ---- fallen_tree ---------------------------------------------------------------------------------------

	class FFallenTree final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcStateProvider> Trunk;
		FMcIntProvider Length;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			const int32 Direction = Random.NextInt(4);
			const FIntVector Step = Sides[Direction];
			const int32 Count = Length.Sample(Random);
			const TCHAR* Axis = Direction < 2 ? TEXT("z") : TEXT("x");
			// The stump stands at the origin; the log lies on the ground beside it.
			for (int32 Index = 0; Index <= Count; ++Index)
			{
				const FIntVector Pos = Origin + Step * Index;
				if (!Region.IsAir(Region.GetBlock(Pos)) || !McHasSturdyFace(Region.GetBlock(Pos + Down), Up))
				{
					return false;
				}
			}
			for (int32 Index = 0; Index <= Count; ++Index)
			{
				const FIntVector Pos = Origin + Step * Index;
				if (const TOptional<FMcStateId> State = Trunk->Get(Region, Random, Pos))
				{
					Region.SetBlock(Pos, Index == 0 ? State.GetValue() : McWithProperty(State.GetValue(), TEXT("axis"), Axis));
				}
			}
			return true;
		}
	};

	TSharedPtr<const FMcBlockPredicate> Pred(const FJsonObject& Json, const TCHAR* Field, const FMcFeatureLibrary& Library)
	{
		return FMcBlockPredicate::Parse(Json.TryGetField(Field), Library.GetSeaLevel());
	}

	TSharedPtr<const FMcFeature> ParsePatch(const FJsonObject& Json, FMcFeatureLibrary& Library, bool bWaterlogged)
	{
		TSharedRef<FVegetationPatch> Result = MakeShared<FVegetationPatch>();
		Result->bWaterlogged = bWaterlogged;
		Result->bCeiling = Json.GetStringField(TEXT("surface")) == TEXT("ceiling");
		Json.TryGetNumberField(TEXT("depth"), Result->Depth);
		Json.TryGetNumberField(TEXT("vertical_range"), Result->VerticalRange);
		Json.TryGetNumberField(TEXT("extra_bottom_block_chance"), Result->ExtraBottom);
		Json.TryGetNumberField(TEXT("extra_edge_column_chance"), Result->ExtraEdge);
		Json.TryGetNumberField(TEXT("vegetation_chance"), Result->VegetationChance);
		Result->Ground = FMcStateProvider::Parse(Json.TryGetField(TEXT("ground_state")));
		Result->Vegetation = Library.ParsePlaced(Json.TryGetField(TEXT("vegetation_feature")));
		const bool bValid = Result->Replaceable.Parse(Json.TryGetField(TEXT("replaceable"))) && Result->Ground.IsValid() && Result->Vegetation.IsValid()
			&& Result->Radius.Parse(Json.TryGetField(TEXT("xz_radius")));
		return bValid ? Result : TSharedPtr<FVegetationPatch>();
	}

	bool ParseMushroom(const FJsonObject& Json, FHugeMushroom& Out, const FMcFeatureLibrary& Library)
	{
		Out.Cap = FMcStateProvider::Parse(Json.TryGetField(TEXT("cap_provider")));
		Out.Stem = FMcStateProvider::Parse(Json.TryGetField(TEXT("stem_provider")));
		Out.CanPlaceOn = Pred(Json, TEXT("can_place_on"), Library);
		return Out.Cap.IsValid() && Out.Stem.IsValid() && Out.CanPlaceOn.IsValid();
	}
}

void McRegisterExtraFeatures()
{
	using FLibrary = FMcFeatureLibrary;
	FLibrary::RegisterType(TEXT("vegetation_patch"), [](const FJsonObject& Json, FLibrary& Library) { return ParsePatch(Json, Library, false); });
	FLibrary::RegisterType(TEXT("waterlogged_vegetation_patch"), [](const FJsonObject& Json, FLibrary& Library) { return ParsePatch(Json, Library, true); });
	FLibrary::RegisterType(TEXT("bamboo"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FBamboo> Result = MakeShared<FBamboo>();
		Json.TryGetNumberField(TEXT("probability"), Result->PodzolChance);
		return Result;
	});
	FLibrary::RegisterType(TEXT("underwater_magma"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FUnderwaterMagma> Result = MakeShared<FUnderwaterMagma>();
		Json.TryGetNumberField(TEXT("floor_search_range"), Result->SearchRange);
		Json.TryGetNumberField(TEXT("placement_radius_around_floor"), Result->Radius);
		Json.TryGetNumberField(TEXT("placement_probability_per_valid_position"), Result->Chance);
		return Result;
	});
	for (const bool bRed : { true, false })
	{
		FLibrary::RegisterType(bRed ? TEXT("huge_red_mushroom") : TEXT("huge_brown_mushroom"), [bRed](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
		{
			TSharedRef<FHugeMushroom> Result = MakeShared<FHugeMushroom>();
			Result->bRed = bRed;
			return ParseMushroom(Json, *Result, Library) ? Result : TSharedPtr<FHugeMushroom>();
		});
	}
	FLibrary::RegisterType(TEXT("spike"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FSpike> Result = MakeShared<FSpike>();
		const TOptional<FMcStateId> State = McParseBlockState(Json.TryGetField(TEXT("state")));
		Result->CanPlaceOn = Pred(Json, TEXT("can_place_on"), Library);
		Result->CanReplace = Pred(Json, TEXT("can_replace"), Library);
		if (!State.IsSet() || !Result->CanPlaceOn.IsValid() || !Result->CanReplace.IsValid())
		{
			return nullptr;
		}
		Result->State = State.GetValue();
		return Result;
	});
	FLibrary::RegisterType(TEXT("blue_ice"), [](const FJsonObject&, FLibrary&) { return MakeShared<FBlueIce>(); });
	FLibrary::RegisterType(TEXT("multiface_growth"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FMultiface> Result = MakeShared<FMultiface>();
		FString Block;
		Json.TryGetStringField(TEXT("block"), Block);
		const TOptional<FMcStateId> State = FMcBlockRegistry::Get().FindDefaultState(FName(*McCache::Namespaced(Block)));
		if (!State.IsSet())
		{
			return nullptr;
		}
		Result->Block = State.GetValue();
		Result->PlaceOn.Parse(Json.TryGetField(TEXT("can_be_placed_on")));
		Json.TryGetBoolField(TEXT("can_place_on_floor"), Result->bFloor);
		Json.TryGetBoolField(TEXT("can_place_on_ceiling"), Result->bCeiling);
		Json.TryGetBoolField(TEXT("can_place_on_wall"), Result->bWall);
		Json.TryGetNumberField(TEXT("search_range"), Result->SearchRange);
		return Result;
	});
	FLibrary::RegisterType(TEXT("random_neighbor_spread"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FNeighborSpread> Result = MakeShared<FNeighborSpread>();
		const TOptional<FMcStateId> State = McParseBlockState(Json.TryGetField(TEXT("block")));
		Result->CanReplace = Pred(Json, TEXT("can_replace"), Library);
		Json.TryGetNumberField(TEXT("attempts"), Result->Attempts);
		const bool bValid = State.IsSet() && Result->CanReplace.IsValid() && Result->Accepted.Parse(Json.TryGetField(TEXT("accepted_neighbors")))
			&& Result->XZ.Parse(Json.TryGetField(TEXT("xz_offset"))) && Result->Y.Parse(Json.TryGetField(TEXT("y_offset")));
		if (!bValid)
		{
			return nullptr;
		}
		Result->State = State.GetValue();
		return Result;
	});
	FLibrary::RegisterType(TEXT("speleothem"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FSpeleothem> Result = MakeShared<FSpeleothem>();
		const TOptional<FMcStateId> Base = McParseBlockState(Json.TryGetField(TEXT("base_block")));
		const TOptional<FMcStateId> Pointed = McParseBlockState(Json.TryGetField(TEXT("pointed_block")));
		if (!Base.IsSet() || !Pointed.IsSet() || !Result->Replaceable.Parse(Json.TryGetField(TEXT("replaceable_blocks"))))
		{
			return nullptr;
		}
		Result->Base = Base.GetValue();
		Result->Pointed = Pointed.GetValue();
		return Result;
	});
	FLibrary::RegisterType(TEXT("geode"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FGeode> Result = MakeShared<FGeode>();
		const TSharedPtr<FJsonObject>* Blocks;
		if (!Json.TryGetObjectField(TEXT("blocks"), Blocks))
		{
			return nullptr;
		}
		const FJsonObject& B = **Blocks;
		Result->Filling = FMcStateProvider::Parse(B.TryGetField(TEXT("filling_provider")));
		Result->Inner = FMcStateProvider::Parse(B.TryGetField(TEXT("inner_layer_provider")));
		Result->AlternateInner = FMcStateProvider::Parse(B.TryGetField(TEXT("alternate_inner_layer_provider")));
		Result->Middle = FMcStateProvider::Parse(B.TryGetField(TEXT("middle_layer_provider")));
		Result->Outer = FMcStateProvider::Parse(B.TryGetField(TEXT("outer_layer_provider")));
		Result->Invalid.Parse(B.TryGetField(TEXT("invalid_blocks")));
		Result->CannotReplace.Parse(B.TryGetField(TEXT("cannot_replace")));
		for (const TSharedPtr<FJsonValue>& Bud : B.GetArrayField(TEXT("inner_placements")))
		{
			if (const TOptional<FMcStateId> State = McParseBlockState(Bud))
			{
				Result->Buds.Add(State.GetValue());
			}
		}
		const TSharedPtr<FJsonObject>* Crack;
		if (Json.TryGetObjectField(TEXT("crack"), Crack))
		{
			(*Crack)->TryGetNumberField(TEXT("generate_crack_chance"), Result->CrackChance);
		}
		Json.TryGetNumberField(TEXT("use_alternate_layer0_chance"), Result->AlternateChance);
		Json.TryGetNumberField(TEXT("invalid_blocks_threshold"), Result->InvalidThreshold);
		const bool bValid = Result->Filling.IsValid() && Result->Inner.IsValid() && Result->AlternateInner.IsValid() && Result->Middle.IsValid()
			&& Result->Outer.IsValid() && Result->Distance.Parse(Json.TryGetField(TEXT("outer_wall_distance")));
		return bValid ? Result : TSharedPtr<FGeode>();
	});
	FLibrary::RegisterType(TEXT("monster_room"), [](const FJsonObject&, FLibrary&) { return MakeShared<FMonsterRoom>(); });
	FLibrary::RegisterType(TEXT("fallen_tree"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FFallenTree> Result = MakeShared<FFallenTree>();
		Result->Trunk = FMcStateProvider::Parse(Json.TryGetField(TEXT("trunk_provider")));
		return Result->Trunk.IsValid() && Result->Length.Parse(Json.TryGetField(TEXT("log_length"))) ? Result : TSharedPtr<FFallenTree>();
	});
	McRegisterTemplateFeatures();
	McRegisterWorldFeatures();
}
