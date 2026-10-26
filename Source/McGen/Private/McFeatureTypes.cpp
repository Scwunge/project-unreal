// The simpler feature types. What each does is from minecraft.wiki ("Configured feature" and the pages of
// the blocks they place); the shapes (ore blobs, lakes, piles, blobs) are our own, sized like the wiki says.

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
	const FIntVector Up(0, 1, 0);
	const FIntVector Down(0, -1, 0);
	const FIntVector Horizontal[4] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };

	FMcStateId DefaultState(const TCHAR* Block)
	{
		return FMcBlockRegistry::Get().FindDefaultState(Block).Get(0);
	}

	bool IsSourceOf(FMcStateId State, EMcFluid Fluid)
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		return Shapes.GetFluid(State) == Fluid && Shapes.GetFluidLevel(State) == 0;
	}

	class FNoOp final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext&, const FIntVector&) const override { return false; }
	};

	// ---- simple_block --------------------------------------------------------------------------------------

	class FSimpleBlock final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcStateProvider> ToPlace;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			const TOptional<FMcStateId> State = ToPlace->Get(Context.Region, Context.Random, Origin);
			return State.IsSet() && McFeature::PlacePlant(Context.Region, Origin, State.GetValue());
		}
	};

	// ---- ore, scattered_ore --------------------------------------------------------------------------------

	struct FOreTarget
	{
		TSharedPtr<const FMcRuleTest> Test;
		FMcStateId State = 0;
	};

	class FOreBase : public FMcFeature
	{
	public:
		int32 Size = 0;
		double DiscardOnAir = 0.0;
		TArray<FOreTarget> Targets;

		// Swaps the block at Pos for the first target that matches it. Blocks touching air are skipped with
		// the discard chance (buried ores).
		bool TryPlace(FMcFeatureContext& Context, const FIntVector& Pos) const
		{
			FMcGenRegion& Region = Context.Region;
			const FMcStateId Here = Region.GetBlock(Pos);
			for (const FOreTarget& Target : Targets)
			{
				if (!Target.Test->Test(Here, Pos, Context.Random))
				{
					continue;
				}
				if (DiscardOnAir > 0.0 && Context.Random.NextDouble() < DiscardOnAir)
				{
					bool bExposed = Region.IsAir(Region.GetBlock(Pos + Up)) || Region.IsAir(Region.GetBlock(Pos + Down));
					for (const FIntVector& Side : Horizontal)
					{
						bExposed |= Region.IsAir(Region.GetBlock(Pos + Side));
					}
					if (bExposed)
					{
						return false;
					}
				}
				return Region.SetBlock(Pos, Target.State);
			}
			return false;
		}
	};

	// A blob along a short random line: spheres that swell in the middle, Size of them.
	class FOre final : public FOreBase
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcRandom& Random = Context.Random;
			if (Size <= 0)
			{
				return false;
			}
			const double Angle = Random.NextDouble() * UE_DOUBLE_PI;
			const double Spread = Size / 8.0;
			const FVector Start(Origin.X + FMath::Sin(Angle) * Spread, Origin.Y + Random.NextInt(3) - 2, Origin.Z + FMath::Cos(Angle) * Spread);
			const FVector End(Origin.X - FMath::Sin(Angle) * Spread, Origin.Y + Random.NextInt(3) - 2, Origin.Z - FMath::Cos(Angle) * Spread);
			TSet<FIntVector> Done;
			bool bPlaced = false;
			for (int32 Index = 0; Index < Size; ++Index)
			{
				const double T = static_cast<double>(Index) / Size;
				const FVector Centre = FMath::Lerp(Start, End, T) + FVector(0.5);
				const double Radius = ((FMath::Sin(UE_DOUBLE_PI * T) + 1.0) * Random.NextDouble() * Size / 16.0 + 1.0) / 2.0;
				const FIntVector Min(FMath::FloorToInt32(Centre.X - Radius), FMath::FloorToInt32(Centre.Y - Radius), FMath::FloorToInt32(Centre.Z - Radius));
				const FIntVector Max(FMath::FloorToInt32(Centre.X + Radius), FMath::FloorToInt32(Centre.Y + Radius), FMath::FloorToInt32(Centre.Z + Radius));
				for (int32 X = Min.X; X <= Max.X; ++X)
				{
					for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
					{
						for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
						{
							const FVector Cell = (FVector(static_cast<double>(X), static_cast<double>(Y), static_cast<double>(Z)) + FVector(0.5) - Centre) / Radius;
							const FIntVector Pos(X, Y, Z);
							if (Cell.SizeSquared() < 1.0 && !Done.Contains(Pos))
							{
								Done.Add(Pos);
								bPlaced |= TryPlace(Context, Pos);
							}
						}
					}
				}
			}
			return bPlaced;
		}
	};

	// Up to Size single blocks scattered around the origin, wider the more there are (ancient debris).
	class FScatteredOre final : public FOreBase
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcRandom& Random = Context.Random;
			bool bPlaced = false;
			for (int32 Index = Random.NextInt(Size + 1) - 1; Index >= 0; --Index)
			{
				const double Spread = FMath::Min(Index, 7);
				const auto Offset = [&Random, Spread]() { return FMath::RoundToInt32((Random.NextDouble() - Random.NextDouble()) * Spread); };
				const int32 X = Offset();
				const int32 Y = Offset();
				const int32 Z = Offset();
				bPlaced |= TryPlace(Context, Origin + FIntVector(X, Y, Z));
			}
			return bPlaced;
		}
	};

	TSharedPtr<const FMcFeature> ParseOre(const FJsonObject& Json, bool bScattered)
	{
		TSharedPtr<FOreBase> Result = bScattered ? StaticCastSharedRef<FOreBase>(MakeShared<FScatteredOre>()) : StaticCastSharedRef<FOreBase>(MakeShared<FOre>());
		Result->Size = Json.GetIntegerField(TEXT("size"));
		Json.TryGetNumberField(TEXT("discard_chance_on_air_exposure"), Result->DiscardOnAir);
		for (const TSharedPtr<FJsonValue>& Entry : Json.GetArrayField(TEXT("targets")))
		{
			const TSharedPtr<FJsonObject> Object = Entry->AsObject();
			FOreTarget Target;
			Target.Test = FMcRuleTest::Parse(Object->TryGetField(TEXT("target")));
			const TOptional<FMcStateId> State = McParseBlockState(Object->TryGetField(TEXT("state")));
			if (!Target.Test.IsValid() || !State.IsSet())
			{
				return nullptr;
			}
			Target.State = State.GetValue();
			Result->Targets.Add(Target);
		}
		return Result;
	}

	// ---- selectors -----------------------------------------------------------------------------------------

	class FRandomSelector final : public FMcFeature
	{
	public:
		TArray<TPair<float, TSharedPtr<const FMcPlacedFeature>>> Features;
		TSharedPtr<const FMcPlacedFeature> Default;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			for (const TPair<float, TSharedPtr<const FMcPlacedFeature>>& Entry : Features)
			{
				if (Context.Random.NextFloat() < Entry.Key)
				{
					return Entry.Value->Place(Context, Origin);
				}
			}
			return Default->Place(Context, Origin);
		}
	};

	class FWeightedSelector final : public FMcFeature
	{
	public:
		TArray<TSharedPtr<const FMcPlacedFeature>> Features;
		TArray<int32> Weights;  // all 1 for simple_random_selector
		int32 Total = 0;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			int32 Pick = Context.Random.NextInt(FMath::Max(1, Total));
			for (int32 Index = 0; Index < Features.Num(); ++Index)
			{
				Pick -= Weights[Index];
				if (Pick < 0)
				{
					return Features[Index]->Place(Context, Origin);
				}
			}
			return false;
		}
	};

	class FBooleanSelector final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcPlacedFeature> IfTrue;
		TSharedPtr<const FMcPlacedFeature> IfFalse;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			return (Context.Random.NextBool() ? IfTrue : IfFalse)->Place(Context, Origin);
		}
	};

	// sequence: each in turn, stopping at the first that places nothing. overlay: all of them.
	class FAll final : public FMcFeature
	{
	public:
		TArray<TSharedPtr<const FMcPlacedFeature>> Features;
		bool bStopOnFailure = false;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			bool bAny = false;
			for (const TSharedPtr<const FMcPlacedFeature>& Feature : Features)
			{
				const bool bPlaced = Feature->Place(Context, Origin);
				if (!bPlaced && bStopOnFailure)
				{
					return false;
				}
				bAny |= bPlaced;
			}
			return bAny;
		}
	};

	bool ReadPlacedList(const FJsonObject& Json, FMcFeatureLibrary& Library, TArray<TSharedPtr<const FMcPlacedFeature>>& Out)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Json.GetArrayField(TEXT("features")))
		{
			TSharedPtr<const FMcPlacedFeature> Feature = Library.ParsePlaced(Entry);
			if (!Feature.IsValid())
			{
				return false;
			}
			Out.Add(Feature);
		}
		return true;
	}

	// ---- block_column --------------------------------------------------------------------------------------

	class FBlockColumn final : public FMcFeature
	{
	public:
		TArray<FMcIntProvider> Heights;
		TArray<TSharedPtr<const FMcStateProvider>> Providers;
		int32 Step = 1;
		TSharedPtr<const FMcBlockPredicate> Allowed;
		bool bPrioritizeTip = false;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			TArray<int32, TInlineAllocator<4>> Counts;
			int32 Total = 0;
			for (const FMcIntProvider& Height : Heights)
			{
				Counts.Add(FMath::Max(0, Height.Sample(Context.Random)));
				Total += Counts.Last();
			}
			if (Total == 0)
			{
				return false;
			}
			// Shorten the column to the room there is, from the top (or the bottom, to keep the tip).
			int32 Room = 0;
			while (Room < Total && Allowed->Test(Context.Region, Origin + FIntVector(0, Room * Step, 0)))
			{
				++Room;
			}
			for (int32 Excess = Total - Room; Excess > 0;)
			{
				const int32 Layer = bPrioritizeTip ? Counts.IndexOfByPredicate([](int32 Count) { return Count > 0; }) : Counts.FindLastByPredicate([](int32 Count) { return Count > 0; });
				const int32 Cut = FMath::Min(Excess, Counts[Layer]);
				Counts[Layer] -= Cut;
				Excess -= Cut;
			}
			if (Room == 0)
			{
				return false;
			}
			int32 At = 0;
			for (int32 Layer = 0; Layer < Counts.Num(); ++Layer)
			{
				for (int32 Index = 0; Index < Counts[Layer]; ++Index, ++At)
				{
					const FIntVector Pos = Origin + FIntVector(0, At * Step, 0);
					if (const TOptional<FMcStateId> State = Providers[Layer]->Get(Context.Region, Context.Random, Pos))
					{
						Context.Region.SetBlock(Pos, State.GetValue());
					}
				}
			}
			return true;
		}
	};

	// ---- disk ----------------------------------------------------------------------------------------------

	class FDisk final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcStateProvider> Provider;
		TSharedPtr<const FMcBlockPredicate> Target;
		FMcIntProvider Radius;
		int32 HalfHeight = 0;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			const int32 R = Radius.Sample(Context.Random);
			bool bPlaced = false;
			for (int32 X = -R; X <= R; ++X)
			{
				for (int32 Z = -R; Z <= R; ++Z)
				{
					if (X * X + Z * Z > R * R)
					{
						continue;
					}
					for (int32 Y = HalfHeight; Y >= -HalfHeight; --Y)
					{
						const FIntVector Pos = Origin + FIntVector(X, Y, Z);
						if (!Target->Test(Context.Region, Pos))
						{
							continue;
						}
						if (const TOptional<FMcStateId> State = Provider->Get(Context.Region, Context.Random, Pos))
						{
							bPlaced |= Context.Region.SetBlock(Pos, State.GetValue());
						}
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- spring_feature ------------------------------------------------------------------------------------

	// A single fluid source in a wall: rock above (and below), exactly RockCount rock sides and HoleCount open.
	class FSpring final : public FMcFeature
	{
	public:
		FMcStateId State = 0;
		bool bRequiresBelow = true;
		int32 RockCount = 4;
		int32 HoleCount = 1;
		TSet<int32> Valid;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			const auto IsValid = [&](const FIntVector& Pos) { return Valid.Contains(Registry.GetState(Region.GetBlock(Pos)).Block); };
			if (!IsValid(Origin + Up) || (bRequiresBelow && !IsValid(Origin + Down)))
			{
				return false;
			}
			const FMcStateId Here = Region.GetBlock(Origin);
			if (!Region.IsAir(Here) && !Valid.Contains(Registry.GetState(Here).Block))
			{
				return false;
			}
			int32 Rocks = 0;
			int32 Holes = 0;
			for (const FIntVector& Side : { Horizontal[0], Horizontal[1], Horizontal[2], Horizontal[3], Down })
			{
				Rocks += IsValid(Origin + Side) ? 1 : 0;
				Holes += Region.IsAir(Region.GetBlock(Origin + Side)) ? 1 : 0;
			}
			return Rocks == RockCount && Holes == HoleCount && Region.SetBlock(Origin, State);
		}
	};

	// ---- lake ----------------------------------------------------------------------------------------------

	// A 16x8x16 hollow from a few overlapping ellipsoids: fluid in the bottom half, air above, and a barrier
	// shell under the fluid line. Gives up if it would spill (fluid next to its air, or open sides below).
	class FLake final : public FMcFeature
	{
	public:
		FMcStateId Fluid = 0;
		FMcStateId Barrier = 0;
		TSharedPtr<const FMcBlockPredicate> CanPlace;
		TSharedPtr<const FMcBlockPredicate> CanReplaceWithFluid;
		TSharedPtr<const FMcBlockPredicate> CanReplaceWithBarrier;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& InOrigin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
			if (InOrigin.Y <= Region.GetMinY() + 4 || !CanPlace->Test(Region, InOrigin))
			{
				return false;
			}
			const FIntVector Corner = InOrigin - FIntVector(8, 4, 8);
			bool Hollow[16][8][16] = {};
			for (int32 Blob = Random.NextInt(4) + 4; Blob > 0; --Blob)
			{
				const double SX = Random.NextDouble() * 6.0 + 3.0;
				const double SY = Random.NextDouble() * 4.0 + 2.0;
				const double SZ = Random.NextDouble() * 6.0 + 3.0;
				const double CX = Random.NextDouble() * (16.0 - SX - 2.0) + 1.0 + SX / 2.0;
				const double CY = Random.NextDouble() * (8.0 - SY - 4.0) + 2.0 + SY / 2.0;
				const double CZ = Random.NextDouble() * (16.0 - SZ - 2.0) + 1.0 + SZ / 2.0;
				for (int32 X = 1; X < 15; ++X)
				{
					for (int32 Z = 1; Z < 15; ++Z)
					{
						for (int32 Y = 1; Y < 7; ++Y)
						{
							const double DX = (X - CX) / (SX / 2.0);
							const double DY = (Y - CY) / (SY / 2.0);
							const double DZ = (Z - CZ) / (SZ / 2.0);
							Hollow[X][Y][Z] |= DX * DX + DY * DY + DZ * DZ < 1.0;
						}
					}
				}
			}
			const auto IsEdge = [&Hollow](int32 X, int32 Y, int32 Z)
			{
				if (Hollow[X][Y][Z])
				{
					return false;
				}
				return (X < 15 && Hollow[X + 1][Y][Z]) || (X > 0 && Hollow[X - 1][Y][Z]) || (Z < 15 && Hollow[X][Y][Z + 1])
					|| (Z > 0 && Hollow[X][Y][Z - 1]) || (Y < 7 && Hollow[X][Y + 1][Z]) || (Y > 0 && Hollow[X][Y - 1][Z]);
			};
			for (int32 X = 0; X < 16; ++X)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 Y = 0; Y < 8; ++Y)
					{
						if (!IsEdge(X, Y, Z))
						{
							continue;
						}
						const FMcStateId State = Region.GetBlock(Corner + FIntVector(X, Y, Z));
						const bool bLiquid = Shapes.GetFluid(State) != EMcFluid::None;
						if (Y >= 4 && bLiquid)
						{
							return false;
						}
						if (Y < 4 && !McIsSolid(State) && State != Fluid)
						{
							return false;
						}
					}
				}
			}
			const FMcStateId Air = DefaultState(TEXT("minecraft:air"));
			for (int32 X = 0; X < 16; ++X)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 Y = 0; Y < 8; ++Y)
					{
						const FIntVector Pos = Corner + FIntVector(X, Y, Z);
						if (Hollow[X][Y][Z] && CanReplaceWithFluid->Test(Region, Pos))
						{
							Region.SetBlock(Pos, Y >= 4 ? Air : Fluid);
						}
					}
				}
			}
			if (!Region.IsAir(Barrier))
			{
				for (int32 X = 0; X < 16; ++X)
				{
					for (int32 Z = 0; Z < 16; ++Z)
					{
						for (int32 Y = 0; Y < 8; ++Y)
						{
							const FIntVector Pos = Corner + FIntVector(X, Y, Z);
							if (IsEdge(X, Y, Z) && (Y < 4 || Random.NextBool()) && McIsSolid(Region.GetBlock(Pos)) && CanReplaceWithBarrier->Test(Region, Pos))
							{
								Region.SetBlock(Pos, Barrier);
							}
						}
					}
				}
			}
			return true;
		}
	};

	// ---- freeze_top_layer ----------------------------------------------------------------------------------

	// Ice on still water and a snow layer on the ground wherever the biome is cold enough to snow (below 0.15).
	class FFreezeTopLayer final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			static const FMcStateId Ice = DefaultState(TEXT("minecraft:ice"));
			static const FMcStateId Snow = DefaultState(TEXT("minecraft:snow"));
			const int32 BaseX = Origin.X & ~15;
			const int32 BaseZ = Origin.Z & ~15;
			bool bPlaced = false;
			for (int32 X = BaseX; X < BaseX + 16; ++X)
			{
				for (int32 Z = BaseZ; Z < BaseZ + 16; ++Z)
				{
					const FIntVector Top(X, Region.GetHeight(EMcHeightmap::MotionBlocking, X, Z), Z);
					const FIntVector Below = Top + Down;
					if (Context.Library.GetTemperature(Region.GetBiome(Top), Top.Y) >= 0.15f)
					{
						continue;
					}
					if (IsSourceOf(Region.GetBlock(Below), EMcFluid::Water))
					{
						bPlaced |= Region.SetBlock(Below, Ice);
						continue;
					}
					if (Region.IsAir(Region.GetBlock(Top)) && FMcBlockPredicate::CanSurvive(Region, Top, Snow))
					{
						bPlaced |= Region.SetBlock(Top, Snow);
						const FMcStateId Ground = Region.GetBlock(Below);
						const FMcStateId Snowy = McWithProperty(Ground, TEXT("snowy"), TEXT("true"));
						if (Snowy != Ground)
						{
							Region.SetBlock(Below, Snowy);
						}
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- block_pile ----------------------------------------------------------------------------------------

	// A low, ragged heap (hay, melons, snow, ice in villages and on the ground): two layers within 2 to 3
	// blocks of the middle, thinning out towards the edge.
	class FBlockPile final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcStateProvider> Provider;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			if (Origin.Y < Region.GetMinY() + 5)
			{
				return false;
			}
			const int32 RX = 2 + Random.NextInt(2);
			const int32 RZ = 2 + Random.NextInt(2);
			bool bPlaced = false;
			for (int32 Y = 0; Y <= 1; ++Y)
			{
				for (int32 X = -RX; X <= RX; ++X)
				{
					for (int32 Z = -RZ; Z <= RZ; ++Z)
					{
						const int32 Distance = X * X + Z * Z;
						const double Reach = Random.NextDouble() * 10.0 - Random.NextDouble() * 4.0;
						if (Distance > Reach * Reach && Random.NextDouble() >= 0.031)
						{
							continue;
						}
						const FIntVector Pos = Origin + FIntVector(X, Y, Z);
						const FMcStateId Here = Region.GetBlock(Pos);
						const FMcStateId Under = Region.GetBlock(Pos + Down);
						if (Region.IsAir(Here) && McHasSturdyFace(Under, Up))
						{
							if (const TOptional<FMcStateId> State = Provider->Get(Region, Random, Pos))
							{
								bPlaced |= Region.SetBlock(Pos, State.GetValue());
							}
						}
					}
				}
			}
			return bPlaced;
		}
	};

	// ---- vines ---------------------------------------------------------------------------------------------

	class FVines final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			static const FMcStateId Vine = DefaultState(TEXT("minecraft:vine"));
			static const FName Faces[4] = { TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east") };
			if (!Region.IsAir(Region.GetBlock(Origin)))
			{
				return false;
			}
			for (int32 Side = 0; Side < 4; ++Side)
			{
				if (McHasSturdyFace(Region.GetBlock(Origin + Horizontal[Side]), FIntVector::ZeroValue - Horizontal[Side]))
				{
					return Region.SetBlock(Origin, McWithProperty(Vine, Faces[Side], TEXT("true")));
				}
			}
			return false;
		}
	};

	// ---- block_blob ----------------------------------------------------------------------------------------

	// A few small overlapping balls dropped onto the ground (forest rocks, iceberg-free mossy boulders).
	class FBlockBlob final : public FMcFeature
	{
	public:
		FMcStateId State = 0;
		TSharedPtr<const FMcBlockPredicate> CanPlaceOn;
		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FMcGenRegion& Region = Context.Region;
			FMcRandom& Random = Context.Random;
			FIntVector At = Origin;
			for (; At.Y > Region.GetMinY() + 3; --At.Y)
			{
				if (!Region.IsAir(Region.GetBlock(At + Down)) && CanPlaceOn->Test(Region, At + Down))
				{
					break;
				}
			}
			if (At.Y <= Region.GetMinY() + 3)
			{
				return false;
			}
			for (int32 Ball = 0; Ball < 3; ++Ball)
			{
				const int32 RX = Random.NextInt(2);
				const int32 RY = Random.NextInt(2);
				const int32 RZ = Random.NextInt(2);
				const double Radius = (RX + RY + RZ) * 0.333 + 0.5;
				for (int32 X = -RX; X <= RX; ++X)
				{
					for (int32 Y = -RY; Y <= RY; ++Y)
					{
						for (int32 Z = -RZ; Z <= RZ; ++Z)
						{
							const FIntVector Pos = At + FIntVector(X, Y, Z);
							if (X * X + Y * Y + Z * Z <= Radius * Radius && McFeature::CanReplace(Region.GetBlock(Pos)))
							{
								Region.SetBlock(Pos, State);
							}
						}
					}
				}
				At += FIntVector(Random.NextInt(3) - 1, -Random.NextInt(2), Random.NextInt(3) - 1);
			}
			return true;
		}
	};

	TSharedPtr<const FMcBlockPredicate> ReadPredicate(const FJsonObject& Json, const TCHAR* Field, const FMcFeatureLibrary& Library)
	{
		return FMcBlockPredicate::Parse(Json.TryGetField(Field), Library.GetSeaLevel());
	}
}

void McRegisterBasicFeatures()
{
	using FLibrary = FMcFeatureLibrary;
	FLibrary::RegisterType(TEXT("simple_block"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FSimpleBlock> Result = MakeShared<FSimpleBlock>();
		Result->ToPlace = FMcStateProvider::Parse(Json.TryGetField(TEXT("to_place")));
		return Result->ToPlace.IsValid() ? Result : TSharedPtr<FSimpleBlock>();
	});
	FLibrary::RegisterType(TEXT("ore"), [](const FJsonObject& Json, FLibrary&) { return ParseOre(Json, false); });
	FLibrary::RegisterType(TEXT("scattered_ore"), [](const FJsonObject& Json, FLibrary&) { return ParseOre(Json, true); });

	FLibrary::RegisterType(TEXT("random_selector"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FRandomSelector> Result = MakeShared<FRandomSelector>();
		for (const TSharedPtr<FJsonValue>& Entry : Json.GetArrayField(TEXT("features")))
		{
			const TSharedPtr<FJsonObject> Object = Entry->AsObject();
			TSharedPtr<const FMcPlacedFeature> Feature = Library.ParsePlaced(Object->TryGetField(TEXT("feature")));
			if (!Feature.IsValid())
			{
				return nullptr;
			}
			Result->Features.Emplace(static_cast<float>(Object->GetNumberField(TEXT("chance"))), Feature);
		}
		Result->Default = Library.ParsePlaced(Json.TryGetField(TEXT("default")));
		return Result->Default.IsValid() ? Result : TSharedPtr<FRandomSelector>();
	});
	FLibrary::RegisterType(TEXT("weighted_random_selector"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FWeightedSelector> Result = MakeShared<FWeightedSelector>();
		for (const TSharedPtr<FJsonValue>& Entry : Json.GetArrayField(TEXT("features")))
		{
			const TSharedPtr<FJsonObject> Object = Entry->AsObject();
			TSharedPtr<const FMcPlacedFeature> Feature = Library.ParsePlaced(Object->TryGetField(TEXT("data")));
			if (!Feature.IsValid())
			{
				return nullptr;
			}
			Result->Features.Add(Feature);
			Result->Weights.Add(Object->GetIntegerField(TEXT("weight")));
			Result->Total += Result->Weights.Last();
		}
		return Result;
	});
	FLibrary::RegisterType(TEXT("simple_random_selector"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FWeightedSelector> Result = MakeShared<FWeightedSelector>();
		if (!ReadPlacedList(Json, Library, Result->Features))
		{
			return nullptr;
		}
		Result->Weights.Init(1, Result->Features.Num());
		Result->Total = Result->Features.Num();
		return Result;
	});
	FLibrary::RegisterType(TEXT("random_boolean_selector"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FBooleanSelector> Result = MakeShared<FBooleanSelector>();
		Result->IfTrue = Library.ParsePlaced(Json.TryGetField(TEXT("feature_true")));
		Result->IfFalse = Library.ParsePlaced(Json.TryGetField(TEXT("feature_false")));
		return Result->IfTrue.IsValid() && Result->IfFalse.IsValid() ? Result : TSharedPtr<FBooleanSelector>();
	});
	for (const TCHAR* Type : { TEXT("sequence"), TEXT("overlay") })
	{
		const bool bSequence = FCString::Strcmp(Type, TEXT("sequence")) == 0;
		FLibrary::RegisterType(Type, [bSequence](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
		{
			TSharedRef<FAll> Result = MakeShared<FAll>();
			Result->bStopOnFailure = bSequence;
			return ReadPlacedList(Json, Library, Result->Features) ? Result : TSharedPtr<FAll>();
		});
	}

	FLibrary::RegisterType(TEXT("block_column"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FBlockColumn> Result = MakeShared<FBlockColumn>();
		for (const TSharedPtr<FJsonValue>& Entry : Json.GetArrayField(TEXT("layers")))
		{
			const TSharedPtr<FJsonObject> Object = Entry->AsObject();
			FMcIntProvider& Height = Result->Heights.AddDefaulted_GetRef();
			Result->Providers.Add(FMcStateProvider::Parse(Object->TryGetField(TEXT("provider"))));
			if (!Height.Parse(Object->TryGetField(TEXT("height"))) || !Result->Providers.Last().IsValid())
			{
				return nullptr;
			}
		}
		Result->Step = Json.GetStringField(TEXT("direction")) == TEXT("down") ? -1 : 1;
		Json.TryGetBoolField(TEXT("prioritize_tip"), Result->bPrioritizeTip);
		Result->Allowed = ReadPredicate(Json, TEXT("allowed_placement"), Library);
		return Result->Allowed.IsValid() ? Result : TSharedPtr<FBlockColumn>();
	});
	FLibrary::RegisterType(TEXT("disk"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FDisk> Result = MakeShared<FDisk>();
		Result->Provider = FMcStateProvider::Parse(Json.TryGetField(TEXT("state_provider")));
		Result->Target = ReadPredicate(Json, TEXT("target"), Library);
		Json.TryGetNumberField(TEXT("half_height"), Result->HalfHeight);
		const bool bValid = Result->Provider.IsValid() && Result->Target.IsValid() && Result->Radius.Parse(Json.TryGetField(TEXT("radius")));
		return bValid ? Result : TSharedPtr<FDisk>();
	});
	FLibrary::RegisterType(TEXT("spring_feature"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FSpring> Result = MakeShared<FSpring>();
		// A fluid state ({ "id": "minecraft:water", "properties": { "falling": "true" } }): the block's source.
		const TSharedPtr<FJsonObject>* FluidObject;
		FString FluidName;
		if (Json.TryGetObjectField(TEXT("state"), FluidObject))
		{
			if (!(*FluidObject)->TryGetStringField(TEXT("id"), FluidName))
			{
				(*FluidObject)->TryGetStringField(TEXT("Name"), FluidName);
			}
		}
		else
		{
			Json.TryGetStringField(TEXT("state"), FluidName);
		}
		const TOptional<FMcStateId> State = FMcBlockRegistry::Get().FindDefaultState(FName(*McCache::Namespaced(FluidName)));
		if (!State.IsSet())
		{
			return nullptr;
		}
		Result->State = State.GetValue();
		Json.TryGetBoolField(TEXT("requires_block_below"), Result->bRequiresBelow);
		Json.TryGetNumberField(TEXT("rock_count"), Result->RockCount);
		Json.TryGetNumberField(TEXT("hole_count"), Result->HoleCount);
		const TSharedPtr<FJsonValue> Blocks = Json.TryGetField(TEXT("valid_blocks"));
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcBlockTags& Tags = FMcBlockTags::Get();
		TArray<FString> Names;
		if (Blocks.IsValid() && Blocks->Type == EJson::String)
		{
			Names.Add(Blocks->AsString());
		}
		else if (Blocks.IsValid() && Blocks->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Name : Blocks->AsArray())
			{
				Names.Add(Name->AsString());
			}
		}
		for (const FString& Name : Names)
		{
			if (Name.StartsWith(TEXT("#")))
			{
				const int32 Tag = Tags.Find(Name.Mid(1));
				for (int32 Block = 0; Block < Registry.NumBlocks(); ++Block)
				{
					if (Tags.HasBlock(Tag, Block))
					{
						Result->Valid.Add(Block);
					}
				}
			}
			else if (const FMcBlock* Block = Registry.FindBlock(FName(*McCache::Namespaced(Name))))
			{
				Result->Valid.Add(Block->Index);
			}
		}
		return Result;
	});
	FLibrary::RegisterType(TEXT("lake"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FLake> Result = MakeShared<FLake>();
		const TOptional<FMcStateId> Fluid = McParseBlockState(Json.TryGetField(TEXT("fluid")));
		const TOptional<FMcStateId> Barrier = McParseBlockState(Json.TryGetField(TEXT("barrier")));
		Result->CanPlace = ReadPredicate(Json, TEXT("can_place_feature"), Library);
		Result->CanReplaceWithFluid = ReadPredicate(Json, TEXT("can_replace_with_air_or_fluid"), Library);
		Result->CanReplaceWithBarrier = ReadPredicate(Json, TEXT("can_replace_with_barrier"), Library);
		if (!Fluid.IsSet() || !Barrier.IsSet() || !Result->CanPlace.IsValid() || !Result->CanReplaceWithFluid.IsValid() || !Result->CanReplaceWithBarrier.IsValid())
		{
			return nullptr;
		}
		Result->Fluid = Fluid.GetValue();
		Result->Barrier = Barrier.GetValue();
		return Result;
	});
	FLibrary::RegisterType(TEXT("no_op"), [](const FJsonObject&, FLibrary&) { return MakeShared<FNoOp>(); });
	FLibrary::RegisterType(TEXT("freeze_top_layer"), [](const FJsonObject&, FLibrary&) { return MakeShared<FFreezeTopLayer>(); });
	FLibrary::RegisterType(TEXT("block_pile"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FBlockPile> Result = MakeShared<FBlockPile>();
		Result->Provider = FMcStateProvider::Parse(Json.TryGetField(TEXT("state_provider")));
		return Result->Provider.IsValid() ? Result : TSharedPtr<FBlockPile>();
	});
	FLibrary::RegisterType(TEXT("vines"), [](const FJsonObject&, FLibrary&) { return MakeShared<FVines>(); });
	FLibrary::RegisterType(TEXT("block_blob"), [](const FJsonObject& Json, FLibrary& Library) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FBlockBlob> Result = MakeShared<FBlockBlob>();
		const TOptional<FMcStateId> State = McParseBlockState(Json.TryGetField(TEXT("state")));
		Result->CanPlaceOn = ReadPredicate(Json, TEXT("can_place_on"), Library);
		if (!State.IsSet() || !Result->CanPlaceOn.IsValid())
		{
			return nullptr;
		}
		Result->State = State.GetValue();
		return Result;
	});
}
