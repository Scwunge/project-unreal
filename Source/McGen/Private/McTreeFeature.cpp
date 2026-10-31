// The tree feature type: oaks, birches, spruces, jungle, acacia, dark oak, cherry, mangrove and the big variants
// (minecraft.wiki, "Tree", "Configured feature"). A tree is a trunk placer that decides where the logs go and
// where the leaves attach, a foliage placer that builds the leaves around each attachment, and decorators for the
// extras (beehives, vines, podzol). The pieces come from the jar's feature JSON; the shapes are our own, made to look
// like the wiki's pictures of each tree.
//
// Trunk placers written: straight, forking, giant, mega_jungle, dark_oak, fancy, cherry. The others (bending,
// upwards_branching, poplar) place a straight trunk of their height, and foliage placers we don't know (poplar)
// a blob, so every tree type places something. Decorators written: beehive, trunk_vine, leave_vine, alter_ground, place_on_ground.

#include "McFeatureTypes.h"
#include "Dom/JsonObject.h"
#include "McBlockPredicate.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McProviders.h"
#include "McRandom.h"
#include "McStateProvider.h"

namespace
{
	const FIntVector Up(0, 1, 0);
	// North is -z, as in the game.
	const FIntVector Sides[4] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };
	const TCHAR* const SideNames[4] = { TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east") };

	enum class ETrunk : uint8 { Straight, Forking, Giant, MegaJungle, DarkOak, Fancy, Cherry };
	enum class EFoliage : uint8 { Blob, Spruce, Pine, Acacia, Bush, RandomSpread, DarkOak, Fancy, MegaPine, Cherry, Jungle };
	enum class EDecorator : uint8 { Beehive, TrunkVine, LeaveVine, AlterGround, PlaceOnGround };
	// Which corner cells of a square layer of leaves are left out.
	enum class ECorners : uint8 { Square, Cut, Random, Round };

	struct FDecorator
	{
		EDecorator Type = EDecorator::Beehive;
		float Probability = 1.0f;
		TSharedPtr<const FMcStateProvider> Provider;
		// place_on_ground: attempts, and how far each may stray from the foot (blocks sideways, blocks up or down).
		int32 Tries = 128;
		int32 Radius = 2;
		int32 Height = 1;
	};

	// Where leaves attach: the cell above a branch's last log. A double one is the north-west cell of a 2x2 top.
	struct FAttach
	{
		FIntVector Pos = FIntVector::ZeroValue;
		int32 Extra = 0;
		bool bDouble = false;
	};

	struct FLog
	{
		FIntVector Pos = FIntVector::ZeroValue;
		int32 Axis = 1;  // 0 x, 1 y, 2 z
	};

	struct FPlan
	{
		TArray<FLog> Logs;
		TArray<FAttach> Attach;
		// The cells of the trunk's foot: one, or the 2x2 of the giant trees.
		TArray<FIntVector> Foot;
	};

	int32 ReadInt(const FJsonObject& Json, const TCHAR* Field, int32 Default = 0)
	{
		double Value = 0.0;
		return Json.TryGetNumberField(Field, Value) ? static_cast<int32>(Value) : Default;
	}

	// An int provider the placer may leave out (it keeps its default then).
	bool ReadProvider(const FJsonObject& Json, const TCHAR* Field, FMcIntProvider& Out)
	{
		const TSharedPtr<FJsonValue> Value = Json.TryGetField(Field);
		return !Value.IsValid() || Out.Parse(Value);
	}

	// A range written as a number or { "min_inclusive": a, "max_inclusive": b } (the type may be left out).
	void ReadRange(const FJsonObject& Json, const TCHAR* Field, int32& OutMin, int32& OutMax)
	{
		const TSharedPtr<FJsonObject>* Object;
		if (Json.TryGetObjectField(Field, Object))
		{
			OutMin = ReadInt(**Object, TEXT("min_inclusive"), OutMin);
			OutMax = ReadInt(**Object, TEXT("max_inclusive"), OutMax);
		}
		else
		{
			OutMin = OutMax = ReadInt(Json, Field, OutMin);
		}
	}

	FString ShortType(const FJsonObject& Json)
	{
		FString Type;
		Json.TryGetStringField(TEXT("type"), Type);
		int32 Colon;
		return Type.FindChar(TEXT(':'), Colon) ? Type.Mid(Colon + 1) : Type;
	}

	bool IsFree(FMcGenRegion& Region, FMcStateId State)
	{
		static const int32 Replaceable = FMcBlockTags::Get().Find(TEXT("minecraft:replaceable_by_trees"));
		return Region.IsAir(State) || FMcBlockTags::Get().Has(Replaceable, State);
	}

	// What a tree is being built from, where, and what it has put down so far.
	struct FBuild
	{
		FMcFeatureContext& Context;
		FMcGenRegion& Region;
		FMcRandom& Random;
		FIntVector Origin;
		TArray<FLog> Logs;
		TArray<FIntVector> Leaves;
	};

	class FTree final : public FMcFeature
	{
	public:
		TSharedPtr<const FMcStateProvider> TrunkProvider;
		TSharedPtr<const FMcStateProvider> FoliageProvider;
		TSharedPtr<const FMcStateProvider> BelowProvider;
		ETrunk Trunk = ETrunk::Straight;
		EFoliage Foliage = EFoliage::Blob;
		int32 BaseHeight = 4;
		int32 RandA = 0;
		int32 RandB = 0;
		// The foliage's numbers: Radius, Offset, FoliageHeight (also foliage_height and crown_height), TrunkHeight (spruce).
		FMcIntProvider Radius;
		FMcIntProvider Offset;
		FMcIntProvider FoliageHeight;
		FMcIntProvider TrunkHeight;
		int32 LeafAttempts = 50;
		// Cherry's branches: how many, how far out, and where they start and end below the top.
		FMcIntProvider BranchCount;
		int32 BranchLengthMin = 2, BranchLengthMax = 4;
		int32 BranchStartMin = -4, BranchStartMax = -3;
		int32 BranchEndMin = -1, BranchEndMax = 0;
		TArray<FDecorator> Decorators;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			FBuild B{ Context, Context.Region, Context.Random, Origin, {}, {} };
			FPlan Plan;
			PlanTrunk(B, Plan);

			// The ground under the foot has to hold plants, and every log's place has to be free.
			static const int32 Ground = FMcBlockTags::Get().Find(TEXT("minecraft:supports_vegetation"));
			for (const FIntVector& Foot : Plan.Foot)
			{
				if (!FMcBlockTags::Get().Has(Ground, B.Region.GetBlock(Foot - Up)))
				{
					return false;
				}
			}
			for (const FLog& Log : Plan.Logs)
			{
				if (!IsFree(B.Region, B.Region.GetBlock(Log.Pos)))
				{
					return false;
				}
			}
			// A tree keeps off other trunks: the ring of columns around each foot has to be free up to the trunk's top.
			// Fitted to the reference client's trunk density per forest column (about 29 per 1000, against twice that
			// without the ring); the radius is the one-block ring, not read from any data file.
			int32 TopY = MIN_int32;
			for (const FLog& Log : Plan.Logs)
			{
				TopY = FMath::Max(TopY, Log.Pos.Y);
			}
			// Trees with a wider trunk (several feet) skip the ring: dark forest measured 61..112 per 1000 columns, and
			// the ring held it to 35..73.
			for (const FIntVector& Foot : Plan.Foot)
			{
				if (Plan.Foot.Num() > 1)
				{
					break;
				}
				for (int32 Y = Foot.Y; Y <= TopY; ++Y)
				{
					for (int32 DZ = -1; DZ <= 1; ++DZ)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							if (!IsFree(B.Region, B.Region.GetBlock(FIntVector(Foot.X + DX, Y, Foot.Z + DZ))))
							{
								return false;
							}
						}
					}
				}
			}

			if (BelowProvider.IsValid())
			{
				for (const FIntVector& Foot : Plan.Foot)
				{
					const TOptional<FMcStateId> Below = BelowProvider->Get(B.Region, B.Random, Foot - Up);
					if (Below.IsSet())
					{
						B.Region.SetBlock(Foot - Up, Below.GetValue());
					}
				}
			}
			for (const FLog& Log : Plan.Logs)
			{
				PlaceLog(B, Log);
			}
			for (const FAttach& Attach : Plan.Attach)
			{
				PlaceFoliage(B, Attach);
			}
			for (const FDecorator& Decorator : Decorators)
			{
				Decorate(B, Decorator);
			}
			return true;
		}

	private:
		// ---- trunks ----------------------------------------------------------------------------------------

		int32 RollHeight(FMcRandom& Random) const
		{
			return BaseHeight + Random.NextInt(RandA + 1) + Random.NextInt(RandB + 1);
		}

		static void AddLog(FPlan& Plan, const FIntVector& Pos, int32 Axis = 1)
		{
			Plan.Logs.Add({ Pos, Axis });
		}

		static int32 AxisOf(const FIntVector& Step)
		{
			return Step.X != 0 ? 0 : Step.Z != 0 ? 2 : 1;
		}

		// A line of logs from one cell to another, the way a branch runs; the start cell is not included.
		static void AddLine(FPlan& Plan, const FIntVector& From, const FIntVector& To)
		{
			const FIntVector Delta = To - From;
			const int32 Steps = FMath::Max3(FMath::Abs(Delta.X), FMath::Abs(Delta.Y), FMath::Abs(Delta.Z));
			const int32 Axis = (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y) && FMath::Abs(Delta.X) >= FMath::Abs(Delta.Z)) ? 0
				: (FMath::Abs(Delta.Z) >= FMath::Abs(Delta.Y) ? 2 : 1);
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				const float T = static_cast<float>(Step) / Steps;
				AddLog(Plan, FIntVector(From.X + FMath::RoundToInt(Delta.X * T), From.Y + FMath::RoundToInt(Delta.Y * T), From.Z + FMath::RoundToInt(Delta.Z * T)), Axis);
			}
		}

		void PlanTrunk(FBuild& B, FPlan& Plan) const
		{
			const FIntVector O = B.Origin;
			FMcRandom& Random = B.Random;
			Plan.Foot.Add(O);
			switch (Trunk)
			{
			case ETrunk::Straight:
			{
				const int32 Height = RollHeight(Random);
				for (int32 Y = 0; Y < Height; ++Y)
				{
					AddLog(Plan, O + FIntVector(0, Y, 0));
				}
				Plan.Attach.Add({ O + FIntVector(0, Height, 0), 0, false });
				break;
			}
			case ETrunk::Forking:
			{
				// Up, then leaning over a step or two near the top, with a shorter second fork off to another side.
				const int32 Height = RollHeight(Random);
				const FIntVector Lean = Sides[Random.NextInt(4)];
				const int32 LeanFrom = Height - 1 - Random.NextInt(4);
				int32 LeanLeft = 2 - Random.NextInt(3);
				FIntVector Pos = O;
				FIntVector ForkStart = O;
				const int32 ForkAt = Height - 2 - Random.NextInt(3);
				for (int32 Y = 0; Y < Height; ++Y)
				{
					if (Y >= LeanFrom && LeanLeft > 0)
					{
						Pos += Lean;
						--LeanLeft;
					}
					Pos.Y = O.Y + Y;
					if (Y == ForkAt)
					{
						ForkStart = Pos;
					}
					AddLog(Plan, Pos);
				}
				Plan.Attach.Add({ Pos + Up, 0, false });

				FIntVector Other = Sides[Random.NextInt(4)];
				if (Other == Lean)
				{
					Other = FIntVector::ZeroValue - Lean;
				}
				const int32 Length = 2 + Random.NextInt(2);
				FIntVector Fork = ForkStart;
				for (int32 Step = 1; Step <= Length; ++Step)
				{
					Fork += Other;
					if (Step >= 2)
					{
						Fork.Y += 1;
					}
					AddLog(Plan, Fork, AxisOf(Other));
				}
				Plan.Attach.Add({ Fork + Up, 0, false });
				break;
			}
			case ETrunk::Giant:
			case ETrunk::MegaJungle:
			case ETrunk::DarkOak:
				PlanBigTrunk(B, Plan);
				break;
			case ETrunk::Fancy:
				PlanFancy(B, Plan);
				break;
			case ETrunk::Cherry:
				PlanCherry(B, Plan);
				break;
			}
		}

		// The 2x2 trunks. O is the north-west column.
		void PlanBigTrunk(FBuild& B, FPlan& Plan) const
		{
			const FIntVector O = B.Origin;
			FMcRandom& Random = B.Random;
			Plan.Foot = { O, O + FIntVector(1, 0, 0), O + FIntVector(0, 0, 1), O + FIntVector(1, 0, 1) };
			const int32 Height = RollHeight(Random);
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (const FIntVector& Foot : Plan.Foot)
				{
					AddLog(Plan, Foot + FIntVector(0, Y, 0));
				}
			}
			FAttach Top;
			Top.Pos = O + FIntVector(0, Height, 0);
			Top.bDouble = true;
			if (Trunk == ETrunk::MegaJungle)
			{
				Top.Extra = 2;
			}
			Plan.Attach.Add(Top);

			if (Trunk == ETrunk::MegaJungle)
			{
				// Branches off the upper trunk, each ending in a ball of leaves.
				for (int32 Y = Height - 2 - Random.NextInt(4); Y > Height / 2; Y -= 2 + Random.NextInt(4))
				{
					const int32 Dir = Random.NextInt(4);
					const FIntVector Step = Sides[Dir];
					FIntVector Start = O + FIntVector(Step.X > 0 ? 1 : 0, Y, Step.Z > 0 ? 1 : 0);
					const int32 Length = 2 + Random.NextInt(2);
					FIntVector End = Start;
					for (int32 Index = 1; Index <= Length; ++Index)
					{
						End = Start + Step * Index + FIntVector(0, Index / 2, 0);
						AddLog(Plan, End, AxisOf(Step));
					}
					Plan.Attach.Add({ End + Up, 0, false });
				}
			}
			else if (Trunk == ETrunk::DarkOak)
			{
				// A few stubs of branch near the top, sticking out of the trunk's sides.
				for (const FIntVector& Foot : Plan.Foot)
				{
					for (int32 Side = 0; Side < 4; ++Side)
					{
						const FIntVector Out = Foot + Sides[Side];
						if (!Plan.Foot.Contains(Out) && Random.NextInt(4) == 0)
						{
							AddLog(Plan, FIntVector(Out.X, O.Y + Height - 1 - Random.NextInt(2), Out.Z), AxisOf(Sides[Side]));
						}
					}
				}
			}
		}

		// The big oak: a trunk with branches reaching out and up, each ending in a ball of leaves.
		void PlanFancy(FBuild& B, FPlan& Plan) const
		{
			const FIntVector O = B.Origin;
			FMcRandom& Random = B.Random;
			const int32 Height = FMath::Max(5, BaseHeight + Random.NextInt(RandA + 1));
			for (int32 Y = 0; Y < Height; ++Y)
			{
				AddLog(Plan, O + FIntVector(0, Y, 0));
			}
			FAttach Top;
			Top.Pos = O + FIntVector(0, Height, 0);
			Top.Extra = 1;
			Plan.Attach.Add(Top);

			const int32 Branches = FMath::Max(2, Height * 2 / 5);
			for (int32 Index = 0; Index < Branches; ++Index)
			{
				const int32 StartY = FMath::Clamp(Height * 3 / 10 + Random.NextInt(FMath::Max(1, Height * 6 / 10)), 2, Height - 3);
				const double Angle = Random.NextDouble() * UE_DOUBLE_PI * 2.0;
				const int32 Length = 2 + Random.NextInt(1 + Height / 4);
				const FIntVector Start = O + FIntVector(0, StartY, 0);
				const FIntVector End = Start + FIntVector(FMath::RoundToInt(FMath::Cos(Angle) * Length), 1 + Random.NextInt(2), FMath::RoundToInt(FMath::Sin(Angle) * Length));
				if (End.X == Start.X && End.Z == Start.Z)
				{
					continue;
				}
				// A branch that runs into something is left out.
				FPlan Branch;
				AddLine(Branch, Start, End);
				bool bClear = true;
				for (const FLog& Log : Branch.Logs)
				{
					bClear &= IsFree(B.Region, B.Region.GetBlock(Log.Pos));
				}
				if (bClear)
				{
					Plan.Logs.Append(Branch.Logs);
					Plan.Attach.Add({ End + Up, 0, false });
				}
			}
		}

		// A cherry: a short straight trunk with branches that go out sideways, then up, each carrying a crown.
		void PlanCherry(FBuild& B, FPlan& Plan) const
		{
			const FIntVector O = B.Origin;
			FMcRandom& Random = B.Random;
			const int32 Height = RollHeight(Random);
			for (int32 Y = 0; Y < Height; ++Y)
			{
				AddLog(Plan, O + FIntVector(0, Y, 0));
			}
			Plan.Attach.Add({ O + FIntVector(0, Height, 0), 0, false });

			const int32 Branches = BranchCount.Sample(Random);
			int32 FirstSide = Random.NextInt(4);
			for (int32 Index = 0; Index < Branches; ++Index)
			{
				const FIntVector Step = Sides[(FirstSide + Index) % 4];
				const int32 StartY = Height - 1 + BranchStartMin + Random.NextInt(BranchStartMax - BranchStartMin + 1);
				const int32 EndY = Height - 1 + BranchEndMin + Random.NextInt(BranchEndMax - BranchEndMin + 1);
				const int32 Length = BranchLengthMin + Random.NextInt(BranchLengthMax - BranchLengthMin + 1);
				const FIntVector Start = O + FIntVector(0, StartY, 0);
				FIntVector End = Start;
				for (int32 Out = 1; Out <= Length; ++Out)
				{
					End = Start + Step * Out;
					AddLog(Plan, End, AxisOf(Step));
				}
				for (int32 Y = StartY + 1; Y <= EndY; ++Y)
				{
					End.Y = O.Y + Y;
					AddLog(Plan, End, 1);
				}
				Plan.Attach.Add({ End + Up, 0, false });
			}
		}

		// ---- placing blocks --------------------------------------------------------------------------------

		void PlaceLog(FBuild& B, const FLog& Log) const
		{
			const TOptional<FMcStateId> State = TrunkProvider->Get(B.Region, B.Random, Log.Pos);
			if (!State.IsSet() || !IsFree(B.Region, B.Region.GetBlock(Log.Pos)))
			{
				return;
			}
			static const TCHAR* const Axes[3] = { TEXT("x"), TEXT("y"), TEXT("z") };
			B.Region.SetBlock(Log.Pos, McWithProperty(State.GetValue(), TEXT("axis"), Axes[Log.Axis]));
			B.Logs.Add(Log);
		}

		void PlaceLeaf(FBuild& B, const FIntVector& Pos) const
		{
			const FMcStateId Here = B.Region.GetBlock(Pos);
			if (!IsFree(B.Region, Here))
			{
				return;
			}
			const TOptional<FMcStateId> State = FoliageProvider->Get(B.Region, B.Random, Pos);
			if (!State.IsSet())
			{
				return;
			}
			// Placed leaves are the freshest: next to a log, so not decaying (the decay checks are not run here).
			FMcStateId Leaf = McWithProperty(State.GetValue(), TEXT("distance"), TEXT("1"));
			const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
			if (Shapes.GetFluid(Here) == EMcFluid::Water)
			{
				Leaf = McWithProperty(Leaf, TEXT("waterlogged"), TEXT("true"));
			}
			if (B.Region.SetBlock(Pos, Leaf))
			{
				B.Leaves.Add(Pos);
			}
		}

		// ---- foliage ---------------------------------------------------------------------------------------

		static bool Keeps(FBuild& B, int32 AX, int32 AZ, int32 R, ECorners Corners)
		{
			switch (Corners)
			{
			case ECorners::Square:
				return true;
			case ECorners::Cut:
				return !(R > 0 && AX == R && AZ == R);
			case ECorners::Random:
				return !(R > 0 && AX == R && AZ == R) || B.Random.NextInt(2) == 1;
			default:
				return AX * AX + AZ * AZ <= R * R + R / 2;
			}
		}

		// One flat layer of leaves of radius R around an attachment (counted from the edge of a 2x2 top).
		void Layer(FBuild& B, const FAttach& Attach, int32 Y, int32 R, ECorners Corners) const
		{
			if (R < 0)
			{
				return;
			}
			const int32 Extra = Attach.bDouble ? 1 : 0;
			for (int32 DX = -R; DX <= R + Extra; ++DX)
			{
				for (int32 DZ = -R; DZ <= R + Extra; ++DZ)
				{
					const int32 AX = DX < 0 ? -DX : FMath::Max(0, DX - Extra);
					const int32 AZ = DZ < 0 ? -DZ : FMath::Max(0, DZ - Extra);
					if (Keeps(B, AX, AZ, R, Corners))
					{
						PlaceLeaf(B, FIntVector(Attach.Pos.X + DX, Y, Attach.Pos.Z + DZ));
					}
				}
			}
		}

		void PlaceFoliage(FBuild& B, const FAttach& A) const
		{
			FMcRandom& Random = B.Random;
			const int32 R = Radius.Sample(Random) + A.Extra;
			const int32 Off = Offset.Sample(Random);
			const int32 H = FoliageHeight.Sample(Random);
			const int32 Y = A.Pos.Y;
			switch (Foliage)
			{
			case EFoliage::Blob:
				// Two narrower layers on top and two wide ones under, as in an oak: a cross, a 3x3, two 5x5s.
				for (int32 Row = 0; Row <= H; ++Row)
				{
					Layer(B, A, Y + Off - Row, Row < 2 ? R - 1 : R, Row == 0 ? ECorners::Cut : ECorners::Random);
				}
				break;
			case EFoliage::Bush:
				for (int32 Row = 0; Row < H; ++Row)
				{
					Layer(B, A, Y + Off - Row, Row == 0 ? R - 1 : R, Row == 0 ? ECorners::Cut : ECorners::Random);
				}
				break;
			case EFoliage::Spruce:
			{
				// A cone from the tip down, the layers widening to the radius and starting over from a narrow one.
				const int32 Bare = TrunkHeight.Sample(Random);
				const int32 Rows = FMath::Max(3, Y - (B.Origin.Y + Bare + Off) + 1);
				int32 Current = 0;
				for (int32 Row = 0; Row < Rows; ++Row)
				{
					Layer(B, A, Y - Row, Current, ECorners::Cut);
					Current = Current >= R ? 1 : Current + 1;
				}
				break;
			}
			case EFoliage::Pine:
				for (int32 Row = 0; Row <= H; ++Row)
				{
					Layer(B, A, Y - Row, FMath::Min(Row, R + 1), ECorners::Cut);
				}
				break;
			case EFoliage::Acacia:
				Layer(B, A, Y, R, ECorners::Round);
				Layer(B, A, Y - 1, R + 1, ECorners::Round);
				break;
			case EFoliage::RandomSpread:
				PlaceLeaf(B, A.Pos);
				for (int32 Attempt = 0; Attempt < LeafAttempts; ++Attempt)
				{
					PlaceLeaf(B, A.Pos + FIntVector(Random.NextInt(2 * R + 1) - R, Random.NextInt(H + 1) - H, Random.NextInt(2 * R + 1) - R));
				}
				break;
			case EFoliage::DarkOak:
				Layer(B, A, Y - 2, 2, ECorners::Round);
				Layer(B, A, Y - 1, 3, ECorners::Round);
				Layer(B, A, Y, 3, ECorners::Round);
				Layer(B, A, Y + 1, 2, ECorners::Round);
				break;
			case EFoliage::Fancy:
				Layer(B, A, Y + 1, R - 1, ECorners::Round);
				Layer(B, A, Y, R, ECorners::Round);
				Layer(B, A, Y - 1, R, ECorners::Round);
				Layer(B, A, Y - 2, R - 1, ECorners::Round);
				break;
			case EFoliage::MegaPine:
			{
				// A cone of rings that step out every other layer, from a single tip.
				const int32 MaxR = 2 + H / 4;
				for (int32 Row = 0; Row <= H; ++Row)
				{
					int32 Ring = Row == 0 ? 0 : 1 + Row * (MaxR - 1) / FMath::Max(H, 1);
					if (Row % 2 == 1 && Ring > 1)
					{
						--Ring;
					}
					Layer(B, A, Y + Off - Row + 1, Ring, ECorners::Round);
				}
				break;
			}
			case EFoliage::Cherry:
			{
				// A wide crown with holes in it: narrow on top, widest two thirds down.
				const int32 Radii[5] = { R - 2, R - 1, R, R, R - 1 };
				for (int32 Row = 0; Row < 5; ++Row)
				{
					FBuild& Build = B;
					const int32 Ring = Radii[Row];
					const int32 Extra = A.bDouble ? 1 : 0;
					for (int32 DX = -Ring; DX <= Ring + Extra; ++DX)
					{
						for (int32 DZ = -Ring; DZ <= Ring + Extra; ++DZ)
						{
							const int32 AX = DX < 0 ? -DX : FMath::Max(0, DX - Extra);
							const int32 AZ = DZ < 0 ? -DZ : FMath::Max(0, DZ - Extra);
							const bool bCorner = Ring > 0 && AX == Ring && AZ == Ring;
							if ((bCorner && Random.NextFloat() < 0.25f) || (Row >= 2 && AX + AZ == Ring + Ring - 1 && Random.NextFloat() < 0.25f))
							{
								continue;
							}
							if (AX * AX + AZ * AZ <= Ring * Ring + Ring)
							{
								PlaceLeaf(Build, FIntVector(A.Pos.X + DX, Y + 1 - Row, A.Pos.Z + DZ));
							}
						}
					}
				}
				break;
			}
			case EFoliage::Jungle:
				// A fat ball: a point or a small disc on top, the widest layer in the middle, a narrower one under.
				Layer(B, A, Y + 1, R - 2, ECorners::Round);
				Layer(B, A, Y, R, ECorners::Round);
				Layer(B, A, Y - 1, R - 1, ECorners::Round);
				break;
			}
		}

		// ---- decorators ------------------------------------------------------------------------------------

		static FMcStateId DefaultState(const TCHAR* Block)
		{
			return FMcBlockRegistry::Get().FindDefaultState(Block).Get(0);
		}

		// The vine at Pos, on the side that faces Support (a neighbour).
		static void PlaceVine(FBuild& B, const FIntVector& Pos, const FIntVector& Support)
		{
			static const FMcStateId Vine = DefaultState(TEXT("minecraft:vine"));
			if (!B.Region.IsAir(B.Region.GetBlock(Pos)))
			{
				return;
			}
			for (int32 Side = 0; Side < 4; ++Side)
			{
				if (Pos + Sides[Side] == Support)
				{
					B.Region.SetBlock(Pos, McWithProperty(Vine, SideNames[Side], TEXT("true")));
				}
			}
		}

		void Decorate(FBuild& B, const FDecorator& Decorator) const
		{
			FMcRandom& Random = B.Random;
			switch (Decorator.Type)
			{
			case EDecorator::TrunkVine:
				for (const FLog& Log : B.Logs)
				{
					for (const FIntVector& Side : Sides)
					{
						if (Random.NextInt(3) > 0)
						{
							PlaceVine(B, Log.Pos + Side, Log.Pos);
						}
					}
				}
				break;
			case EDecorator::LeaveVine:
				for (const FIntVector& Leaf : B.Leaves)
				{
					for (const FIntVector& Side : Sides)
					{
						if (Random.NextFloat() < Decorator.Probability && B.Region.IsAir(B.Region.GetBlock(Leaf + Side)))
						{
							// It hangs down a few blocks.
							for (int32 Drop = 0; Drop < 5; ++Drop)
							{
								const FIntVector At = Leaf + Side - FIntVector(0, Drop, 0);
								if (!B.Region.IsAir(B.Region.GetBlock(At)))
								{
									break;
								}
								PlaceVine(B, At, At - Side);
							}
						}
					}
				}
				break;
			case EDecorator::Beehive:
			{
				if (Random.NextFloat() >= Decorator.Probability || B.Logs.IsEmpty() || B.Leaves.IsEmpty())
				{
					return;
				}
				// On the trunk's side at the height where the leaves begin.
				int32 NestY = MAX_int32;
				for (const FIntVector& Leaf : B.Leaves)
				{
					NestY = FMath::Min(NestY, Leaf.Y);
				}
				TArray<FIntVector> Candidates;
				for (const FLog& Log : B.Logs)
				{
					if (Log.Pos.Y == NestY)
					{
						Candidates.Add(Log.Pos);
					}
				}
				if (Candidates.IsEmpty())
				{
					return;
				}
				static const FMcStateId Nest = DefaultState(TEXT("minecraft:bee_nest"));
				const FIntVector Log = Candidates[Random.NextInt(Candidates.Num())];
				const int32 First = Random.NextInt(4);
				for (int32 Index = 0; Index < 4; ++Index)
				{
					const int32 Side = (First + Index) % 4;
					if (B.Region.IsAir(B.Region.GetBlock(Log + Sides[Side])))
					{
						B.Region.SetBlock(Log + Sides[Side], McWithProperty(Nest, TEXT("facing"), SideNames[Side]));
						break;
					}
				}
				break;
			}
			case EDecorator::PlaceOnGround:
			{
				// Litter scattered round the foot: each try picks a cell within the radius and height and sets the block
				// there if it is air with something to rest on.
				if (!Decorator.Provider.IsValid())
				{
					return;
				}
				for (int32 Try = 0; Try < Decorator.Tries; ++Try)
				{
					const FIntVector At = B.Origin + FIntVector(
						Random.NextInt(Decorator.Radius * 2 + 1) - Decorator.Radius,
						Random.NextInt(Decorator.Height * 2 + 1) - Decorator.Height,
						Random.NextInt(Decorator.Radius * 2 + 1) - Decorator.Radius);
					const TOptional<FMcStateId> State = Decorator.Provider->Get(B.Region, Random, At);
					if (State.IsSet() && B.Region.IsAir(B.Region.GetBlock(At)) && FMcBlockPredicate::CanSurvive(B.Region, At, State.GetValue()))
					{
						B.Region.SetBlock(At, State.GetValue());
					}
				}
				break;
			}
			case EDecorator::AlterGround:
			{
				// Podzol (or whatever the provider gives) scattered over the ground around the trunk's foot.
				if (!Decorator.Provider.IsValid())
				{
					return;
				}
				static const int32 Replaceable = FMcBlockTags::Get().Find(TEXT("minecraft:beneath_tree_podzol_replaceable"));
				const FIntVector O = B.Origin;
				for (int32 DX = -2; DX <= 3; ++DX)
				{
					for (int32 DZ = -2; DZ <= 3; ++DZ)
					{
						if (Random.NextInt(10) >= 7)
						{
							continue;
						}
						for (int32 DY = 1; DY >= -3; --DY)
						{
							const FIntVector At = O + FIntVector(DX, DY - 1, DZ);
							if (FMcBlockTags::Get().Has(Replaceable, B.Region.GetBlock(At)))
							{
								const TOptional<FMcStateId> State = Decorator.Provider->Get(B.Region, Random, At);
								if (State.IsSet())
								{
									B.Region.SetBlock(At, State.GetValue());
								}
								break;
							}
							if (!B.Region.IsAir(B.Region.GetBlock(At)))
							{
								break;
							}
						}
					}
				}
				break;
			}
			}
		}
	};

	ETrunk ParseTrunk(const FString& Type)
	{
		if (Type == TEXT("forking_trunk_placer")) return ETrunk::Forking;
		if (Type == TEXT("giant_trunk_placer")) return ETrunk::Giant;
		if (Type == TEXT("mega_jungle_trunk_placer")) return ETrunk::MegaJungle;
		if (Type == TEXT("dark_oak_trunk_placer")) return ETrunk::DarkOak;
		if (Type == TEXT("fancy_trunk_placer")) return ETrunk::Fancy;
		if (Type == TEXT("cherry_trunk_placer")) return ETrunk::Cherry;
		// straight, and the ones not written: bending, upwards_branching, poplar.
		return ETrunk::Straight;
	}

	TOptional<EFoliage> ParseFoliage(const FString& Type)
	{
		if (Type == TEXT("blob_foliage_placer")) return EFoliage::Blob;
		if (Type == TEXT("spruce_foliage_placer")) return EFoliage::Spruce;
		if (Type == TEXT("pine_foliage_placer")) return EFoliage::Pine;
		if (Type == TEXT("acacia_foliage_placer")) return EFoliage::Acacia;
		if (Type == TEXT("bush_foliage_placer")) return EFoliage::Bush;
		if (Type == TEXT("random_spread_foliage_placer")) return EFoliage::RandomSpread;
		if (Type == TEXT("dark_oak_foliage_placer")) return EFoliage::DarkOak;
		if (Type == TEXT("fancy_foliage_placer")) return EFoliage::Fancy;
		if (Type == TEXT("mega_pine_foliage_placer")) return EFoliage::MegaPine;
		if (Type == TEXT("cherry_foliage_placer")) return EFoliage::Cherry;
		if (Type == TEXT("jungle_foliage_placer")) return EFoliage::Jungle;
		return TOptional<EFoliage>();
	}

	TSharedPtr<const FMcFeature> ParseTree(const FJsonObject& Json)
	{
		TSharedRef<FTree> Tree = MakeShared<FTree>();
		Tree->TrunkProvider = FMcStateProvider::Parse(Json.TryGetField(TEXT("trunk_provider")));
		Tree->FoliageProvider = FMcStateProvider::Parse(Json.TryGetField(TEXT("foliage_provider")));
		if (!Tree->TrunkProvider.IsValid() || !Tree->FoliageProvider.IsValid())
		{
			return nullptr;
		}
		if (Json.HasField(TEXT("below_trunk_provider")))
		{
			Tree->BelowProvider = FMcStateProvider::Parse(Json.TryGetField(TEXT("below_trunk_provider")));
		}

		const TSharedPtr<FJsonObject>& TrunkJson = Json.GetObjectField(TEXT("trunk_placer"));
		Tree->Trunk = ParseTrunk(ShortType(*TrunkJson));
		Tree->BaseHeight = ReadInt(*TrunkJson, TEXT("base_height"), 4);
		Tree->RandA = ReadInt(*TrunkJson, TEXT("height_rand_a"));
		Tree->RandB = ReadInt(*TrunkJson, TEXT("height_rand_b"));
		if (Tree->Trunk == ETrunk::Cherry)
		{
			ReadProvider(*TrunkJson, TEXT("branch_count"), Tree->BranchCount);
			ReadRange(*TrunkJson, TEXT("branch_horizontal_length"), Tree->BranchLengthMin, Tree->BranchLengthMax);
			ReadRange(*TrunkJson, TEXT("branch_start_offset_from_top"), Tree->BranchStartMin, Tree->BranchStartMax);
			ReadRange(*TrunkJson, TEXT("branch_end_offset_from_top"), Tree->BranchEndMin, Tree->BranchEndMax);
		}

		const TSharedPtr<FJsonObject>& FoliageJson = Json.GetObjectField(TEXT("foliage_placer"));
		const TOptional<EFoliage> Foliage = ParseFoliage(ShortType(*FoliageJson));
		if (Foliage.IsSet())
		{
			Tree->Foliage = Foliage.GetValue();
			bool bValid = ReadProvider(*FoliageJson, TEXT("radius"), Tree->Radius);
			bValid &= ReadProvider(*FoliageJson, TEXT("offset"), Tree->Offset);
			bValid &= ReadProvider(*FoliageJson, TEXT("height"), Tree->FoliageHeight);
			bValid &= ReadProvider(*FoliageJson, TEXT("foliage_height"), Tree->FoliageHeight);
			bValid &= ReadProvider(*FoliageJson, TEXT("crown_height"), Tree->FoliageHeight);
			bValid &= ReadProvider(*FoliageJson, TEXT("trunk_height"), Tree->TrunkHeight);
			if (!bValid)
			{
				return nullptr;
			}
			Tree->LeafAttempts = ReadInt(*FoliageJson, TEXT("leaf_placement_attempts"), 50);
		}
		else
		{
			// A foliage placer we haven't written: a plain blob.
			Tree->Foliage = EFoliage::Blob;
			Tree->Radius.Min = Tree->Radius.Max = 3;
			Tree->FoliageHeight.Min = Tree->FoliageHeight.Max = 3;
		}

		const TArray<TSharedPtr<FJsonValue>>* Decorators;
		if (Json.TryGetArrayField(TEXT("decorators"), Decorators))
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Decorators)
			{
				const TSharedPtr<FJsonObject> Object = Entry->AsObject();
				const FString Type = Object.IsValid() ? ShortType(*Object) : FString();
				FDecorator Decorator;
				double Probability = 1.0;
				Object->TryGetNumberField(TEXT("probability"), Probability);
				Decorator.Probability = static_cast<float>(Probability);
				if (Type == TEXT("beehive")) Decorator.Type = EDecorator::Beehive;
				else if (Type == TEXT("trunk_vine")) Decorator.Type = EDecorator::TrunkVine;
				else if (Type == TEXT("leave_vine")) Decorator.Type = EDecorator::LeaveVine;
				else if (Type == TEXT("alter_ground"))
				{
					Decorator.Type = EDecorator::AlterGround;
					Decorator.Provider = FMcStateProvider::Parse(Object->TryGetField(TEXT("provider")));
				}
				else if (Type == TEXT("place_on_ground"))
				{
					Decorator.Type = EDecorator::PlaceOnGround;
					Decorator.Provider = FMcStateProvider::Parse(Object->TryGetField(TEXT("block_state_provider")));
					int32 Value = 0;
					if (Object->TryGetNumberField(TEXT("tries"), Value)) Decorator.Tries = Value;
					if (Object->TryGetNumberField(TEXT("radius"), Value)) Decorator.Radius = Value;
					if (Object->TryGetNumberField(TEXT("height"), Value)) Decorator.Height = Value;
				}
				else
				{
					// cocoa, attached_to_leaves, attached_to_logs and the rest: not written.
					continue;
				}
				Tree->Decorators.Add(Decorator);
			}
		}
		return Tree;
	}
}

void McRegisterTreeFeatures()
{
	FMcFeatureLibrary::RegisterType(TEXT("tree"), [](const FJsonObject& Json, FMcFeatureLibrary&) { return ParseTree(Json); });
}
