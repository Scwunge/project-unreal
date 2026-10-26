// The density function graph from the jar's worldgen/density_function data: how solid each point of the
// world is (positive is solid), and the climate values biomes are picked by.
//
// The JSON is compiled once per world seed into a flat array of nodes; named functions referenced from
// several places become one node. Evaluating is read-only on the graph, with all scratch state in an
// FMcDensityContext, so each worker thread can generate chunks with its own context.
//
// Semantics follow minecraft.wiki's Density function page for 26.3. Old-world blending and structure
// "beards" have nothing to blend with yet: blend_alpha is 1, blend_offset 0, blend_density passes its
// input through and beardifier is 0. end_outer_islands is a constant "no islands" until the End (M8).

#pragma once

#include "CoreMinimal.h"
#include "McNoise.h"

class FJsonValue;
class FJsonObject;
class FMcDensityContext;

enum class EMcDensityOp : uint8
{
	Constant,
	Add, Mul, Sub, Div, Min, Max,
	Abs, Square, Cube, HalfNegative, QuarterNegative, Squeeze, Negate,
	Clamp, Gradient, Lerp, RangeChoice, IntervalSelect,
	Noise, ShiftA, ShiftB, Shift, BlendedNoise, Spline,
	Cache, Interpolated, FindTopSurface, Slice, DistanceToPoint, EndIslands,
};

struct FMcDensityNode
{
	EMcDensityOp Op = EMcDensityOp::Constant;
	// Inputs by role: A is input/left/alpha, B right/first, C second; D and E more (noise shifts, choices).
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	int32 C = INDEX_NONE;
	int32 D = INDEX_NONE;
	int32 E = INDEX_NONE;
	// interval_select's functions.
	TArray<int32> Choices;
	// Numbers by role: constant value, ranges, scales, gradient ends, cell sizes, points.
	double P[6] = {};
	TArray<double> Thresholds;
	// Into the graph's noises, blended noises or splines.
	int32 Resource = INDEX_NONE;
	// Memo or interpolation slot in the context.
	int32 Slot = INDEX_NONE;
	// Which coordinates the value depends on: 1 x, 2 y, 4 z.
	uint8 Depends = 0;
	// For gradient and slice: 0 x, 1 y, 2 z. For gradient also the tiling: 0 clamp, 1 repeat, 2 mirrored.
	uint8 Axis = 0;
	uint8 Tiling = 0;
};

class MCGEN_API FMcDensityGraph
{
public:
	explicit FMcDensityGraph(uint64 InSeed);

	// A named function from density_function/, e.g. "minecraft:overworld/continents", loading it and
	// everything it references. INDEX_NONE (and an error logged) if any part fails to load.
	int32 LoadFunction(const FString& Id);
	// An inline function: a number, a name, or an object.
	int32 Parse(const TSharedPtr<FJsonValue>& Json);

	double Evaluate(int32 Node, FMcDensityContext& Context) const;

	const FMcDensityNode& GetNode(int32 Index) const { return Nodes[Index]; }
	int32 NumNodes() const { return Nodes.Num(); }
	int32 NumSlots() const { return SlotCount; }
	bool HasErrors() const { return bErrors; }
	uint64 GetSeed() const { return Seed; }

	// Whether a node's value changes with y; columns can share values that don't.
	bool DependsOnY(int32 Node) const { return (Nodes[Node].Depends & 2) != 0; }

private:
	friend class FMcDensityContext;

	struct FSplinePoint
	{
		double Location = 0.0;
		double Value = 0.0;
		int32 SubSpline = INDEX_NONE;
		double Derivative = 0.0;
	};
	struct FSpline
	{
		int32 Coordinate = INDEX_NONE;
		TArray<FSplinePoint> Points;
		uint8 Depends = 0;
	};

	int32 ParseObject(const FJsonObject& Json);
	int32 ParseSpline(const TSharedPtr<FJsonValue>& Json);
	int32 GetNoise(const FString& Id);
	int32 AddNode(FMcDensityNode&& Node);
	double EvaluateSpline(int32 Index, FMcDensityContext& Context) const;
	double EvaluateUncached(const FMcDensityNode& Node, FMcDensityContext& Context) const;
	double Interpolate(int32 NodeIndex, const FMcDensityNode& Node, FMcDensityContext& Context) const;

	uint64 Seed;
	TArray<FMcDensityNode> Nodes;
	TArray<FMcNoise> Noises;
	TMap<FString, int32> NoiseIds;
	TArray<FMcBlendedNoise> BlendedNoises;
	TArray<FSpline> Splines;
	TMap<FString, int32> Named;
	TSet<FString> Loading;
	int32 SlotCount = 0;
	bool bErrors = false;
};

// Scratch state for evaluating a graph: the position, memos, and (when generating a chunk) the values of
// interpolated functions at the chunk's cell corners.
class MCGEN_API FMcDensityContext
{
public:
	explicit FMcDensityContext(const FMcDensityGraph& Graph);

	void SetPos(int32 InX, int32 InY, int32 InZ) { X = InX; Y = InY; Z = InZ; }
	// Interpolated functions use corner grids covering this column of blocks (16x16, MinY up Height
	// blocks), worked out once each. Without a chunk, they work their corners out per point.
	void BeginChunk(int32 BlockX, int32 BlockZ, int32 MinY, int32 Height);
	void EndChunk();

	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;

private:
	friend class FMcDensityGraph;

	struct FMemo
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;
		double Value = 0.0;
		bool bValid = false;
	};
	struct FGrid
	{
		TArray<double> Corners;
		int32 SizeX = 0;
		int32 SizeY = 0;
		bool bReady = false;
	};

	TArray<FMemo> Memos;
	TArray<FGrid> Grids;
	bool bInChunk = false;
	int32 ChunkX = 0;
	int32 ChunkZ = 0;
	int32 ChunkMinY = 0;
	int32 ChunkHeight = 0;
};
