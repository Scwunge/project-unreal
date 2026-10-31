#include "McDensityFunction.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "McCache.h"
#include "McGen.h"
#include "McRandom.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr uint8 DependsX = 1;
	constexpr uint8 DependsY = 2;
	constexpr uint8 DependsZ = 4;
	constexpr uint8 DependsAll = DependsX | DependsY | DependsZ;

	const TMap<FString, EMcDensityOp>& OpsByName()
	{
		static const TMap<FString, EMcDensityOp> Ops = {
			{ TEXT("add"), EMcDensityOp::Add },
			{ TEXT("mul"), EMcDensityOp::Mul },
			{ TEXT("sub"), EMcDensityOp::Sub },
			{ TEXT("div"), EMcDensityOp::Div },
			{ TEXT("min"), EMcDensityOp::Min },
			{ TEXT("max"), EMcDensityOp::Max },
			{ TEXT("abs"), EMcDensityOp::Abs },
			{ TEXT("square"), EMcDensityOp::Square },
			{ TEXT("cube"), EMcDensityOp::Cube },
			{ TEXT("half_negative"), EMcDensityOp::HalfNegative },
			{ TEXT("quarter_negative"), EMcDensityOp::QuarterNegative },
			{ TEXT("squeeze"), EMcDensityOp::Squeeze },
			{ TEXT("negate"), EMcDensityOp::Negate },
			{ TEXT("clamp"), EMcDensityOp::Clamp },
			{ TEXT("gradient"), EMcDensityOp::Gradient },
			{ TEXT("lerp"), EMcDensityOp::Lerp },
			{ TEXT("range_choice"), EMcDensityOp::RangeChoice },
			{ TEXT("interval_select"), EMcDensityOp::IntervalSelect },
			{ TEXT("noise"), EMcDensityOp::Noise },
			{ TEXT("shift_a"), EMcDensityOp::ShiftA },
			{ TEXT("shift_b"), EMcDensityOp::ShiftB },
			{ TEXT("shift"), EMcDensityOp::Shift },
			{ TEXT("old_blended_noise"), EMcDensityOp::BlendedNoise },
			{ TEXT("spline"), EMcDensityOp::Spline },
			{ TEXT("cache"), EMcDensityOp::Cache },
			{ TEXT("interpolated"), EMcDensityOp::Interpolated },
			{ TEXT("find_top_surface"), EMcDensityOp::FindTopSurface },
			{ TEXT("slice"), EMcDensityOp::Slice },
			{ TEXT("distance_to_point"), EMcDensityOp::DistanceToPoint },
			{ TEXT("end_outer_islands"), EMcDensityOp::EndIslands },
		};
		return Ops;
	}

	uint8 AxisIndex(const FString& Axis)
	{
		return Axis == TEXT("x") ? 0 : Axis == TEXT("z") ? 2 : 1;
	}

	uint8 AxisBit(uint8 Axis)
	{
		return static_cast<uint8>(1 << Axis);
	}

	// The outer islands of the End as a density, in the units of 8 blocks (minecraft.wiki, "The End"): beyond
	// 1024 blocks from the origin, wherever a simplex noise sampled once per 16x16 blocks dips under -0.9
	// there's an island, round and with a size from the cell's position (a bigger size is a smaller island).
	// Each island contributes 100 minus its distance times its size, clamped to -100..80, and the density is
	// the highest of them, less 8, over 128.
	double EndIslandDensity(const FMcSimplexNoise& Noise, int32 BlockX, int32 BlockZ)
	{
		const int32 X = FMath::FloorToInt32(BlockX / 8.0);
		const int32 Z = FMath::FloorToInt32(BlockZ / 8.0);
		const int32 CellX = X >> 1;
		const int32 CellZ = Z >> 1;
		const int32 LimitX = FMath::Abs(CellX) + 12;
		const int32 LimitZ = FMath::Abs(CellZ) + 12;
		double Height = -100.0;
		// The centre of the map holds no islands within reach.
		if (LimitX * LimitX + LimitZ * LimitZ > 4096)
		{
			for (int32 J = -12; J <= 12; ++J)
			{
				for (int32 I = -12; I <= 12; ++I)
				{
					const int32 IslandX = CellX + I;
					const int32 IslandZ = CellZ + J;
					if (IslandX * IslandX + IslandZ * IslandZ <= 4096 || Noise.Sample(IslandX, IslandZ) >= -0.9)
					{
						continue;
					}
					const int32 Size = (FMath::Abs(IslandX) * 3439 + FMath::Abs(IslandZ) * 147) % 13 + 9;
					const double DX = (X & 1) - I * 2;
					const double DZ = (Z & 1) - J * 2;
					Height = FMath::Max(Height, FMath::Clamp(100.0 - FMath::Sqrt(DX * DX + DZ * DZ) * Size, -100.0, 80.0));
				}
			}
		}
		return (Height - 8.0) / 128.0;
	}

	int32 FloorDiv(int32 Value, int32 Divisor)
	{
		return FMath::FloorToInt32(static_cast<double>(Value) / Divisor);
	}
}

FMcDensityGraph::FMcDensityGraph(uint64 InSeed)
	: Seed(InSeed)
{
}

int32 FMcDensityGraph::AddNode(FMcDensityNode&& Node)
{
	return Nodes.Add(MoveTemp(Node));
}

int32 FMcDensityGraph::GetNoise(const FString& Id)
{
	const FString Key = McCache::Namespaced(Id);
	if (const int32* Found = NoiseIds.Find(Key))
	{
		return *Found;
	}
	FMcNoiseParams Params;
	if (!FMcNoiseParams::Load(Key, Params))
	{
		bErrors = true;
		return INDEX_NONE;
	}
	FMcRandom Random = FMcRandom(Seed).Fork(Key);
	const int32 Index = Noises.Emplace(Random, Params);
	NoiseIds.Add(Key, Index);
	return Index;
}

int32 FMcDensityGraph::LoadFunction(const FString& Id)
{
	const FString Key = McCache::Namespaced(Id);
	if (const int32* Found = Named.Find(Key))
	{
		return *Found;
	}
	if (Loading.Contains(Key))
	{
		UE_LOG(LogMcGen, Error, TEXT("Density function %s refers to itself"), *Key);
		bErrors = true;
		return INDEX_NONE;
	}
	const FString Path = McCache::Data(TEXT("worldgen/density_function/") + McCache::PathOf(Key) + TEXT(".json"));
	FString Text;
	TSharedPtr<FJsonValue> Json;
	const bool bLoaded = FFileHelper::LoadFileToString(Text, *Path);
	Text.TrimStartAndEndInline();
	// A file can be just a number, which the JSON reader won't take on its own.
	if (bLoaded && !Text.IsEmpty() && (FChar::IsDigit(Text[0]) || Text[0] == TEXT('-')))
	{
		Json = MakeShared<FJsonValueNumber>(FCString::Atod(*Text));
	}
	else if (bLoaded && Text.StartsWith(TEXT("\"")))
	{
		Json = MakeShared<FJsonValueString>(Text.Mid(1, Text.Len() - 2));
	}
	else if (!bLoaded || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		UE_LOG(LogMcGen, Error, TEXT("Density function %s not found"), *Key);
		bErrors = true;
		return INDEX_NONE;
	}

	Loading.Add(Key);
	const int32 Inner = Parse(Json);
	Loading.Remove(Key);
	if (Inner == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	// Named functions are memoised: they are usually referenced from several places.
	int32 Index = Inner;
	if (Nodes[Inner].Op != EMcDensityOp::Constant && Nodes[Inner].Op != EMcDensityOp::Cache)
	{
		FMcDensityNode Node;
		Node.Op = EMcDensityOp::Cache;
		Node.A = Inner;
		Node.Depends = Nodes[Inner].Depends;
		Node.Slot = SlotCount++;
		Index = AddNode(MoveTemp(Node));
	}
	Named.Add(Key, Index);
	return Index;
}

int32 FMcDensityGraph::Parse(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		bErrors = true;
		return INDEX_NONE;
	}
	if (Json->Type == EJson::Number)
	{
		FMcDensityNode Node;
		Node.Op = EMcDensityOp::Constant;
		Node.P[0] = Json->AsNumber();
		return AddNode(MoveTemp(Node));
	}
	if (Json->Type == EJson::String)
	{
		return LoadFunction(Json->AsString());
	}
	if (Json->Type == EJson::Object)
	{
		return ParseObject(*Json->AsObject());
	}
	UE_LOG(LogMcGen, Error, TEXT("A density function is neither a number, a name nor an object"));
	bErrors = true;
	return INDEX_NONE;
}

int32 FMcDensityGraph::ParseObject(const FJsonObject& Json)
{
	FString Type = Json.GetStringField(TEXT("type"));
	Type.RemoveFromStart(TEXT("minecraft:"));

	auto Field = [&](const TCHAR* Name) { return Parse(Json.TryGetField(Name)); };
	auto OptionalField = [&](const TCHAR* Name) { return Json.HasField(Name) ? Parse(Json.TryGetField(Name)) : INDEX_NONE; };
	auto Number = [&](const TCHAR* Name, double Default = 0.0) { double Value = Default; Json.TryGetNumberField(Name, Value); return Value; };
	auto Constant = [&](double Value) { FMcDensityNode Node; Node.P[0] = Value; return AddNode(MoveTemp(Node)); };
	auto DependsOf = [&](int32 Index) -> uint8 { return Index == INDEX_NONE ? 0 : Nodes[Index].Depends; };

	// Blending with old worlds and structure beards: nothing to blend with yet.
	if (Type == TEXT("blend_alpha"))
	{
		return Constant(1.0);
	}
	if (Type == TEXT("blend_offset") || Type == TEXT("beardifier"))
	{
		return Constant(0.0);
	}
	if (Type == TEXT("blend_density"))
	{
		return Field(TEXT("input"));
	}

	const EMcDensityOp* Op = OpsByName().Find(Type);
	if (!Op)
	{
		UE_LOG(LogMcGen, Error, TEXT("Unknown density function type %s"), *Type);
		bErrors = true;
		return INDEX_NONE;
	}

	FMcDensityNode Node;
	Node.Op = *Op;
	switch (Node.Op)
	{
	case EMcDensityOp::Add:
	case EMcDensityOp::Mul:
	case EMcDensityOp::Sub:
	case EMcDensityOp::Div:
	case EMcDensityOp::Min:
	case EMcDensityOp::Max:
		Node.A = Field(TEXT("left"));
		Node.B = Field(TEXT("right"));
		Node.Depends = DependsOf(Node.A) | DependsOf(Node.B);
		break;

	case EMcDensityOp::Abs:
	case EMcDensityOp::Square:
	case EMcDensityOp::Cube:
	case EMcDensityOp::HalfNegative:
	case EMcDensityOp::QuarterNegative:
	case EMcDensityOp::Squeeze:
	case EMcDensityOp::Negate:
		Node.A = Field(TEXT("input"));
		Node.Depends = DependsOf(Node.A);
		break;

	case EMcDensityOp::Clamp:
		Node.A = Field(TEXT("input"));
		Node.P[0] = Number(TEXT("min"));
		Node.P[1] = Number(TEXT("max"));
		Node.Depends = DependsOf(Node.A);
		break;

	case EMcDensityOp::Gradient:
	{
		Node.Axis = AxisIndex(Json.GetStringField(TEXT("axis")));
		Node.P[0] = Number(TEXT("from_coordinate"));
		Node.P[1] = Number(TEXT("to_coordinate"));
		Node.P[2] = Number(TEXT("from_value"));
		Node.P[3] = Number(TEXT("to_value"));
		FString Tiling;
		Json.TryGetStringField(TEXT("tiling"), Tiling);
		Node.Tiling = Tiling == TEXT("repeat") ? 1 : Tiling == TEXT("mirrored_repeat") ? 2 : 0;
		Node.Depends = AxisBit(Node.Axis);
		break;
	}

	case EMcDensityOp::Lerp:
		Node.A = Field(TEXT("alpha"));
		Node.B = Field(TEXT("first"));
		Node.C = Field(TEXT("second"));
		Node.Depends = DependsOf(Node.A) | DependsOf(Node.B) | DependsOf(Node.C);
		break;

	case EMcDensityOp::RangeChoice:
		Node.A = Field(TEXT("input"));
		Node.B = Field(TEXT("when_in_range"));
		Node.C = Field(TEXT("when_out_of_range"));
		Node.P[0] = Number(TEXT("min_inclusive"));
		Node.P[1] = Number(TEXT("max_exclusive"));
		Node.Depends = DependsOf(Node.A) | DependsOf(Node.B) | DependsOf(Node.C);
		break;

	case EMcDensityOp::IntervalSelect:
	{
		Node.A = Field(TEXT("input"));
		Node.Depends = DependsOf(Node.A);
		for (const TSharedPtr<FJsonValue>& Value : Json.GetArrayField(TEXT("thresholds")))
		{
			Node.Thresholds.Add(Value->AsNumber());
		}
		for (const TSharedPtr<FJsonValue>& Value : Json.GetArrayField(TEXT("functions")))
		{
			const int32 Choice = Parse(Value);
			Node.Choices.Add(Choice);
			Node.Depends |= DependsOf(Choice);
		}
		if (Node.Choices.Num() != Node.Thresholds.Num() + 1)
		{
			UE_LOG(LogMcGen, Error, TEXT("interval_select needs one more function than thresholds"));
			bErrors = true;
			return INDEX_NONE;
		}
		break;
	}

	case EMcDensityOp::Noise:
		Node.Resource = GetNoise(Json.GetStringField(TEXT("noise")));
		Node.P[0] = Number(TEXT("xz_scale"), 1.0);
		Node.P[1] = Number(TEXT("y_scale"), 1.0);
		Node.A = OptionalField(TEXT("shift_x"));
		Node.B = OptionalField(TEXT("shift_y"));
		Node.C = OptionalField(TEXT("shift_z"));
		Node.Depends = (Node.P[0] != 0.0 ? (DependsX | DependsZ) : 0) | (Node.P[1] != 0.0 ? DependsY : 0)
			| DependsOf(Node.A) | DependsOf(Node.B) | DependsOf(Node.C);
		break;

	case EMcDensityOp::ShiftA:
	case EMcDensityOp::ShiftB:
		Node.Resource = GetNoise(Json.GetStringField(TEXT("noise")));
		Node.Depends = DependsX | DependsZ;
		break;

	case EMcDensityOp::Shift:
		Node.Resource = GetNoise(Json.GetStringField(TEXT("noise")));
		Node.Depends = DependsAll;
		break;

	case EMcDensityOp::BlendedNoise:
	{
		FMcRandom Random = FMcRandom(Seed).Fork(TEXT("minecraft:terrain"));
		Node.Resource = BlendedNoises.Emplace(Random, Number(TEXT("xz_scale")), Number(TEXT("y_scale")), Number(TEXT("xz_factor")),
			Number(TEXT("y_factor")), Number(TEXT("smear_scale_multiplier")));
		Node.Depends = DependsAll;
		break;
	}

	case EMcDensityOp::Spline:
	{
		const TSharedPtr<FJsonValue> Spline = Json.TryGetField(TEXT("spline"));
		if (Spline.IsValid() && Spline->Type == EJson::Number)
		{
			return Constant(Spline->AsNumber());
		}
		Node.Resource = ParseSpline(Spline);
		if (Node.Resource == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		Node.Depends = Splines[Node.Resource].Depends;
		break;
	}

	case EMcDensityOp::Cache:
		Node.A = Field(TEXT("input"));
		Node.Depends = DependsOf(Node.A);
		Node.Slot = SlotCount++;
		break;

	case EMcDensityOp::Interpolated:
		Node.A = Field(TEXT("input"));
		Node.P[0] = FMath::Max(1.0, Number(TEXT("cell_size_xz"), 4.0));
		Node.P[1] = FMath::Max(1.0, Number(TEXT("cell_size_y"), 8.0));
		Node.Depends = DependsOf(Node.A);
		Node.Slot = SlotCount++;
		break;

	case EMcDensityOp::FindTopSurface:
		Node.A = Field(TEXT("density"));
		Node.B = Field(TEXT("upper_bound"));
		Node.P[0] = Number(TEXT("lower_bound"));
		Node.P[1] = FMath::Max(1.0, Number(TEXT("cell_height"), 1.0));
		// The search runs over y, so the result only depends on the column.
		Node.Depends = ((DependsOf(Node.A) | DependsOf(Node.B)) & ~DependsY);
		Node.Slot = SlotCount++;
		break;

	case EMcDensityOp::Slice:
		Node.Axis = AxisIndex(Json.GetStringField(TEXT("axis")));
		Node.P[0] = Number(TEXT("coordinate"));
		Node.A = Field(TEXT("input"));
		Node.Depends = DependsOf(Node.A) & ~AxisBit(Node.Axis);
		break;

	case EMcDensityOp::DistanceToPoint:
	{
		const TArray<TSharedPtr<FJsonValue>>& Point = Json.GetArrayField(TEXT("point"));
		for (int32 Index = 0; Index < 3 && Index < Point.Num(); ++Index)
		{
			Node.P[Index] = Point[Index]->AsNumber();
		}
		const FString Metric = Json.GetStringField(TEXT("metric"));
		Node.Tiling = Metric == TEXT("euclidean_squared") ? 1 : Metric == TEXT("manhattan") ? 2 : Metric == TEXT("chebyshev") ? 3 : 0;
		Node.Depends = DependsAll;
		break;
	}

	case EMcDensityOp::EndIslands:
	{
		FMcRandom Random = FMcRandom(Seed).Fork(TEXT("minecraft:end_islands"));
		Node.Resource = IslandNoises.Emplace(Random);
		Node.Depends = DependsX | DependsZ;
		break;
	}

	default:
		break;
	}

	return AddNode(MoveTemp(Node));
}

int32 FMcDensityGraph::ParseSpline(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid() || Json->Type != EJson::Object)
	{
		bErrors = true;
		return INDEX_NONE;
	}
	const TSharedPtr<FJsonObject> Object = Json->AsObject();
	FSpline Spline;
	Spline.Coordinate = Parse(Object->TryGetField(TEXT("coordinate")));
	if (Spline.Coordinate == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	Spline.Depends = Nodes[Spline.Coordinate].Depends;
	for (const TSharedPtr<FJsonValue>& PointValue : Object->GetArrayField(TEXT("points")))
	{
		const TSharedPtr<FJsonObject> PointObject = PointValue->AsObject();
		FSplinePoint Point;
		Point.Location = PointObject->GetNumberField(TEXT("location"));
		Point.Derivative = PointObject->GetNumberField(TEXT("derivative"));
		const TSharedPtr<FJsonValue> Value = PointObject->TryGetField(TEXT("value"));
		if (Value.IsValid() && Value->Type == EJson::Number)
		{
			Point.Value = Value->AsNumber();
		}
		else
		{
			Point.SubSpline = ParseSpline(Value);
			if (Point.SubSpline == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			Spline.Depends |= Splines[Point.SubSpline].Depends;
		}
		Spline.Points.Add(Point);
	}
	if (Spline.Points.IsEmpty())
	{
		bErrors = true;
		return INDEX_NONE;
	}
	return Splines.Add(MoveTemp(Spline));
}

double FMcDensityGraph::EvaluateSpline(int32 Index, FMcDensityContext& Context) const
{
	const FSpline& Spline = Splines[Index];
	const double Coordinate = Evaluate(Spline.Coordinate, Context);
	auto ValueAt = [&](const FSplinePoint& Point) { return Point.SubSpline == INDEX_NONE ? Point.Value : EvaluateSpline(Point.SubSpline, Context); };

	const TArray<FSplinePoint>& Points = Spline.Points;
	// Outside the points the curve carries on in a straight line.
	if (Coordinate <= Points[0].Location)
	{
		return ValueAt(Points[0]) + Points[0].Derivative * (Coordinate - Points[0].Location);
	}
	const FSplinePoint& Last = Points.Last();
	if (Coordinate >= Last.Location)
	{
		return ValueAt(Last) + Last.Derivative * (Coordinate - Last.Location);
	}
	int32 Upper = 1;
	while (Points[Upper].Location <= Coordinate)
	{
		++Upper;
	}
	const FSplinePoint& P0 = Points[Upper - 1];
	const FSplinePoint& P1 = Points[Upper];
	const double Span = P1.Location - P0.Location;
	const double T = (Coordinate - P0.Location) / Span;
	const double V0 = ValueAt(P0);
	const double V1 = ValueAt(P1);
	// Cubic Hermite through both points with their slopes.
	const double T2 = T * T;
	const double T3 = T2 * T;
	return (2 * T3 - 3 * T2 + 1) * V0 + (T3 - 2 * T2 + T) * Span * P0.Derivative + (-2 * T3 + 3 * T2) * V1 + (T3 - T2) * Span * P1.Derivative;
}

double FMcDensityGraph::Evaluate(int32 Index, FMcDensityContext& Context) const
{
	const FMcDensityNode& Node = Nodes[Index];
	if (Node.Op == EMcDensityOp::Constant)
	{
		return Node.P[0];
	}
	if (Node.Op == EMcDensityOp::Interpolated)
	{
		return Interpolate(Index, Node, Context);
	}
	if (Node.Slot == INDEX_NONE)
	{
		return EvaluateUncached(Node, Context);
	}
	// Memoised on the coordinates the value depends on.
	const int32 KeyX = (Node.Depends & DependsX) ? Context.X : 0;
	const int32 KeyY = (Node.Depends & DependsY) ? Context.Y : 0;
	const int32 KeyZ = (Node.Depends & DependsZ) ? Context.Z : 0;
	FMcDensityContext::FMemo& Memo = Context.Memos[Node.Slot];
	if (Memo.bValid && Memo.X == KeyX && Memo.Y == KeyY && Memo.Z == KeyZ)
	{
		return Memo.Value;
	}
	const double Value = EvaluateUncached(Node, Context);
	FMcDensityContext::FMemo& Stored = Context.Memos[Node.Slot];
	Stored = { KeyX, KeyY, KeyZ, Value, true };
	return Value;
}

double FMcDensityGraph::EvaluateUncached(const FMcDensityNode& Node, FMcDensityContext& Context) const
{
	switch (Node.Op)
	{
	case EMcDensityOp::Constant:
		return Node.P[0];
	case EMcDensityOp::Add:
		return Evaluate(Node.A, Context) + Evaluate(Node.B, Context);
	case EMcDensityOp::Mul:
	{
		const double Left = Evaluate(Node.A, Context);
		return Left == 0.0 ? 0.0 : Left * Evaluate(Node.B, Context);
	}
	case EMcDensityOp::Sub:
		return Evaluate(Node.A, Context) - Evaluate(Node.B, Context);
	case EMcDensityOp::Div:
	{
		const double Right = Evaluate(Node.B, Context);
		return Right == 0.0 ? 0.0 : Evaluate(Node.A, Context) / Right;
	}
	case EMcDensityOp::Min:
		return FMath::Min(Evaluate(Node.A, Context), Evaluate(Node.B, Context));
	case EMcDensityOp::Max:
		return FMath::Max(Evaluate(Node.A, Context), Evaluate(Node.B, Context));
	case EMcDensityOp::Abs:
		return FMath::Abs(Evaluate(Node.A, Context));
	case EMcDensityOp::Square:
		return FMath::Square(Evaluate(Node.A, Context));
	case EMcDensityOp::Cube:
	{
		const double Value = Evaluate(Node.A, Context);
		return Value * Value * Value;
	}
	case EMcDensityOp::HalfNegative:
	{
		const double Value = Evaluate(Node.A, Context);
		return Value < 0.0 ? Value * 0.5 : Value;
	}
	case EMcDensityOp::QuarterNegative:
	{
		const double Value = Evaluate(Node.A, Context);
		return Value < 0.0 ? Value * 0.25 : Value;
	}
	case EMcDensityOp::Squeeze:
	{
		const double Value = FMath::Clamp(Evaluate(Node.A, Context), -1.0, 1.0);
		return Value / 2.0 - Value * Value * Value / 24.0;
	}
	case EMcDensityOp::Negate:
		return -Evaluate(Node.A, Context);
	case EMcDensityOp::Clamp:
		return FMath::Clamp(Evaluate(Node.A, Context), Node.P[0], Node.P[1]);
	case EMcDensityOp::Gradient:
	{
		const double Coordinate = Node.Axis == 0 ? Context.X : Node.Axis == 1 ? Context.Y : Context.Z;
		const double Span = Node.P[1] - Node.P[0];
		if (Node.Tiling == 0)
		{
			// Multiply before dividing, so whole-number gradients like y give whole numbers.
			const double Clamped = FMath::Clamp(Coordinate, FMath::Min(Node.P[0], Node.P[1]), FMath::Max(Node.P[0], Node.P[1]));
			return Node.P[2] + (Clamped - Node.P[0]) * (Node.P[3] - Node.P[2]) / Span;
		}
		double T = (Coordinate - Node.P[0]) / Span;
		if (Node.Tiling == 1)
		{
			T -= FMath::FloorToDouble(T);
		}
		else
		{
			const double Period = T - 2.0 * FMath::FloorToDouble(T / 2.0);
			T = Period <= 1.0 ? Period : 2.0 - Period;
		}
		return FMath::Lerp(Node.P[2], Node.P[3], T);
	}
	case EMcDensityOp::Lerp:
	{
		const double Alpha = Evaluate(Node.A, Context);
		// Skip the side that has no weight; terrain lerps are often 0 or 1 outside a band of heights.
		if (Alpha == 0.0)
		{
			return Evaluate(Node.B, Context);
		}
		if (Alpha == 1.0)
		{
			return Evaluate(Node.C, Context);
		}
		return FMath::Lerp(Evaluate(Node.B, Context), Evaluate(Node.C, Context), Alpha);
	}
	case EMcDensityOp::RangeChoice:
	{
		const double Value = Evaluate(Node.A, Context);
		return Value >= Node.P[0] && Value < Node.P[1] ? Evaluate(Node.B, Context) : Evaluate(Node.C, Context);
	}
	case EMcDensityOp::IntervalSelect:
	{
		const double Value = Evaluate(Node.A, Context);
		int32 Choice = 0;
		while (Choice < Node.Thresholds.Num() && Value >= Node.Thresholds[Choice])
		{
			++Choice;
		}
		return Evaluate(Node.Choices[Choice], Context);
	}
	case EMcDensityOp::Noise:
	{
		if (Node.Resource == INDEX_NONE)
		{
			return 0.0;
		}
		const double ShiftX = Node.A != INDEX_NONE ? Evaluate(Node.A, Context) : 0.0;
		const double ShiftY = Node.B != INDEX_NONE ? Evaluate(Node.B, Context) : 0.0;
		const double ShiftZ = Node.C != INDEX_NONE ? Evaluate(Node.C, Context) : 0.0;
		return Noises[Node.Resource].Sample(Context.X * Node.P[0] + ShiftX, Context.Y * Node.P[1] + ShiftY, Context.Z * Node.P[0] + ShiftZ);
	}
	case EMcDensityOp::ShiftA:
		return Node.Resource == INDEX_NONE ? 0.0 : Noises[Node.Resource].Sample(Context.X * 0.25, 0.0, Context.Z * 0.25) * 4.0;
	case EMcDensityOp::ShiftB:
		return Node.Resource == INDEX_NONE ? 0.0 : Noises[Node.Resource].Sample(Context.Z * 0.25, Context.X * 0.25, 0.0) * 4.0;
	case EMcDensityOp::Shift:
		return Node.Resource == INDEX_NONE ? 0.0 : Noises[Node.Resource].Sample(Context.X * 0.25, Context.Y * 0.25, Context.Z * 0.25) * 4.0;
	case EMcDensityOp::BlendedNoise:
		return BlendedNoises[Node.Resource].Sample(Context.X, Context.Y, Context.Z);
	case EMcDensityOp::EndIslands:
		return EndIslandDensity(IslandNoises[Node.Resource], Context.X, Context.Z);
	case EMcDensityOp::Spline:
		return EvaluateSpline(Node.Resource, Context);
	case EMcDensityOp::Cache:
		return Evaluate(Node.A, Context);
	case EMcDensityOp::FindTopSurface:
	{
		const int32 SavedY = Context.Y;
		const int32 Step = static_cast<int32>(Node.P[1]);
		const int32 Lower = static_cast<int32>(Node.P[0]);
		const int32 Upper = FloorDiv(FMath::FloorToInt32(Evaluate(Node.B, Context)), Step) * Step;
		double Result = Lower;
		for (int32 Y = Upper; Y >= Lower; Y -= Step)
		{
			Context.Y = Y;
			if (Evaluate(Node.A, Context) > 0.0)
			{
				Result = Y;
				break;
			}
		}
		Context.Y = SavedY;
		return Result;
	}
	case EMcDensityOp::Slice:
	{
		int32* Coordinate = Node.Axis == 0 ? &Context.X : Node.Axis == 1 ? &Context.Y : &Context.Z;
		const int32 Saved = *Coordinate;
		*Coordinate = static_cast<int32>(Node.P[0]);
		const double Value = Evaluate(Node.A, Context);
		*Coordinate = Saved;
		return Value;
	}
	case EMcDensityOp::DistanceToPoint:
	{
		const double DX = FMath::Abs(Context.X - Node.P[0]);
		const double DY = FMath::Abs(Context.Y - Node.P[1]);
		const double DZ = FMath::Abs(Context.Z - Node.P[2]);
		switch (Node.Tiling)
		{
		case 1: return DX * DX + DY * DY + DZ * DZ;
		case 2: return DX + DY + DZ;
		case 3: return FMath::Max3(DX, DY, DZ);
		default: return FMath::Sqrt(DX * DX + DY * DY + DZ * DZ);
		}
	}
	default:
		return 0.0;
	}
}

double FMcDensityGraph::Interpolate(int32 NodeIndex, const FMcDensityNode& Node, FMcDensityContext& Context) const
{
	const int32 CellXZ = static_cast<int32>(Node.P[0]);
	const int32 CellY = (Node.Depends & DependsY) ? static_cast<int32>(Node.P[1]) : 0;
	const int32 X = Context.X;
	const int32 Y = Context.Y;
	const int32 Z = Context.Z;
	const int32 X0 = FloorDiv(X, CellXZ) * CellXZ;
	const int32 Z0 = FloorDiv(Z, CellXZ) * CellXZ;
	const int32 Y0 = CellY > 0 ? FloorDiv(Y, CellY) * CellY : Y;
	const double TX = static_cast<double>(X - X0) / CellXZ;
	const double TZ = static_cast<double>(Z - Z0) / CellXZ;
	const double TY = CellY > 0 ? static_cast<double>(Y - Y0) / CellY : 0.0;

	// Inside the chunk being generated, corners come from a grid worked out once.
	const bool bGrid = Context.bInChunk && X >= Context.ChunkX && X < Context.ChunkX + 16 && Z >= Context.ChunkZ && Z < Context.ChunkZ + 16
		&& Y >= Context.ChunkMinY && Y < Context.ChunkMinY + Context.ChunkHeight && 16 % CellXZ == 0 && (CellY == 0 || Context.ChunkHeight % CellY == 0);
	if (bGrid)
	{
		FMcDensityContext::FGrid& Grid = Context.Grids[Node.Slot];
		if (!Grid.bReady)
		{
			Grid.SizeX = 16 / CellXZ + 1;
			Grid.SizeY = CellY > 0 ? Context.ChunkHeight / CellY + 1 : 1;
			Grid.Corners.SetNumUninitialized(Grid.SizeX * Grid.SizeX * Grid.SizeY);
			for (int32 CZ = 0; CZ < Grid.SizeX; ++CZ)
			{
				for (int32 CX = 0; CX < Grid.SizeX; ++CX)
				{
					for (int32 CY = 0; CY < Grid.SizeY; ++CY)
					{
						Context.SetPos(Context.ChunkX + CX * CellXZ, CellY > 0 ? Context.ChunkMinY + CY * CellY : Y, Context.ChunkZ + CZ * CellXZ);
						Grid.Corners[(CZ * Grid.SizeX + CX) * Grid.SizeY + CY] = Evaluate(Node.A, Context);
					}
				}
			}
			Context.SetPos(X, Y, Z);
			Grid.bReady = true;
		}
		const int32 GX = (X0 - Context.ChunkX) / CellXZ;
		const int32 GZ = (Z0 - Context.ChunkZ) / CellXZ;
		const int32 GY = CellY > 0 ? (Y0 - Context.ChunkMinY) / CellY : 0;
		const int32 NextY = CellY > 0 ? 1 : 0;
		auto Corner = [&](int32 DX, int32 DY, int32 DZ) { return Grid.Corners[((GZ + DZ) * Grid.SizeX + GX + DX) * Grid.SizeY + GY + DY * NextY]; };
		const double Bottom = FMath::Lerp(FMath::Lerp(Corner(0, 0, 0), Corner(1, 0, 0), TX), FMath::Lerp(Corner(0, 0, 1), Corner(1, 0, 1), TX), TZ);
		if (NextY == 0)
		{
			return Bottom;
		}
		const double Top = FMath::Lerp(FMath::Lerp(Corner(0, 1, 0), Corner(1, 1, 0), TX), FMath::Lerp(Corner(0, 1, 1), Corner(1, 1, 1), TX), TZ);
		return FMath::Lerp(Bottom, Top, TY);
	}

	// Anywhere else, the eight corners around the point.
	auto CornerAt = [&](int32 CX, int32 CY, int32 CZ)
	{
		Context.SetPos(CX, CY, CZ);
		return Evaluate(Node.A, Context);
	};
	const int32 X1 = X0 + CellXZ;
	const int32 Z1 = Z0 + CellXZ;
	double Value = FMath::Lerp(FMath::Lerp(CornerAt(X0, Y0, Z0), CornerAt(X1, Y0, Z0), TX), FMath::Lerp(CornerAt(X0, Y0, Z1), CornerAt(X1, Y0, Z1), TX), TZ);
	if (CellY > 0 && TY > 0.0)
	{
		const int32 Y1 = Y0 + CellY;
		const double Top = FMath::Lerp(FMath::Lerp(CornerAt(X0, Y1, Z0), CornerAt(X1, Y1, Z0), TX), FMath::Lerp(CornerAt(X0, Y1, Z1), CornerAt(X1, Y1, Z1), TX), TZ);
		Value = FMath::Lerp(Value, Top, TY);
	}
	Context.SetPos(X, Y, Z);
	return Value;
}

FMcDensityContext::FMcDensityContext(const FMcDensityGraph& Graph)
{
	Memos.SetNum(Graph.NumSlots());
	Grids.SetNum(Graph.NumSlots());
}

void FMcDensityContext::BeginChunk(int32 BlockX, int32 BlockZ, int32 MinY, int32 Height)
{
	bInChunk = true;
	ChunkX = BlockX;
	ChunkZ = BlockZ;
	ChunkMinY = MinY;
	ChunkHeight = Height;
	for (FGrid& Grid : Grids)
	{
		Grid.bReady = false;
	}
}

void FMcDensityContext::EndChunk()
{
	bInChunk = false;
}
