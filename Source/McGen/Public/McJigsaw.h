// The jigsaw assembler (minecraft.wiki, "Jigsaw structure", "Template pool"): villages, pillager outposts, ancient
// cities, trail ruins, abandoned camps and bastions are grown from a start piece. Every piece is a template with
// jigsaw blocks; each jigsaw names a pool, and a piece from that pool whose own jigsaw matches is attached at it,
// as long as it stays inside the structure's box and clear of the pieces already there, up to a depth.
//
// The layout depends on the seed and the start chunk (and the terrain height there, from a height source), so
// every chunk can work out the same layout and write only its own part of it.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "McStructureTemplate.h"

class FJsonObject;
class FMcFeatureLibrary;
class FMcGenRegion;
class FMcPlacedFeature;
class FMcRandom;

class MCGEN_API FMcJigsawLibrary
{
public:
	// Directions as 0 north, 1 east, 2 south, 3 west, 4 up, 5 down.
	struct FJigsaw
	{
		FIntVector Pos;  // inside the template
		int32 Front = 0;
		int32 Top = 4;
		FName Name;
		FName Target;
		FName Pool;
		bool bRollable = true;
		int32 Selection = 0;
		int32 Placement = 0;
	};

	struct FElement
	{
		enum class EKind : uint8 { Single, Feature, List, Empty };
		EKind Kind = EKind::Empty;
		TSharedPtr<const FMcStructureTemplate> Template;
		FString Location;  // the template's id
		TSharedPtr<const FMcProcessorList> Processors;  // with gravity added for terrain matching
		bool bTerrainMatching = false;
		TArray<TSharedPtr<const FElement>> Parts;       // a list element's elements, put down together
		TSharedPtr<const FMcPlacedFeature> Feature;
		TArray<FJigsaw> Jigsaws;
		FIntVector Size = FIntVector::ZeroValue;
	};

	struct FPool
	{
		TArray<TPair<TSharedPtr<const FElement>, int32>> Elements;
		FName Fallback;
	};

	struct FPiece
	{
		TSharedPtr<const FElement> Element;
		FIntVector Origin = FIntVector::ZeroValue;  // where the template's corner goes, before turning
		EMcRotation Rotation = EMcRotation::None;
		FIntVector Min = FIntVector::ZeroValue;     // the box it covers, inclusive
		FIntVector Max = FIntVector::ZeroValue;
		int32 Depth = 0;
		int32 Parent = -1;  // the piece it grows from, an index into the layout
	};

	struct FLayout
	{
		TArray<FPiece> Pieces;
	};

	struct FStart
	{
		FName Pool;
		int32 MaxDepth = 6;
		int32 MaxDistance = 80;
		FName StartJigsaw;
		// The block column the start piece is centred on, and the y its ground (or start jigsaw) is put at.
		int32 CentreX = 0;
		int32 CentreZ = 0;
		int32 Y = 0;
		int32 MinY = -64;
		int32 MaxY = 320;
		// Pools that stand in for others, picked when the structure starts (trial chambers' spawner pools).
		TMap<FName, FName> Aliases;
	};

	// Reads the pool and, one by one, every pool its jigsaws point at (game thread). Features may be null.
	bool LoadPool(FName Id, FMcFeatureLibrary* Features);
	const FPool* GetPool(FName Id) const;
	int32 NumPools() const { return Pools.Num(); }
	// Template ids that pools name but don't load.
	const TSet<FString>& GetMissing() const { return Missing; }

	// Grows a structure. Height gives the ground height (one above the top block) at a block column, with no
	// regard to features; it is how terrain matching pieces and the pieces they carry find the ground.
	TSharedPtr<FLayout> Assemble(const FStart& Start, FMcRandom& Random, TFunctionRef<int32(int32, int32)> Height) const;

	// Puts the parts of a layout inside the clip rectangle (inclusive columns) into the region. Random is
	// seeded per piece from the caller's stream.
	void Place(FMcGenRegion& Region, const FLayout& Layout, FIntPoint ClipMin, FIntPoint ClipMax, const FMcRandom& Base, bool bBeard, const FMcFeatureLibrary* Features, int32 SeaLevel) const;

	static FIntVector Direction(int32 Index);

private:
	TSharedPtr<const FElement> ParseElement(const FJsonObject& Json, FMcFeatureLibrary* Features, TArray<FName>& Referenced);
	static void CollectJigsaws(const FMcStructureTemplate& Template, TArray<FJigsaw>& Out);

	TMap<FName, TSharedPtr<FPool>> Pools;
	TSet<FString> Missing;
};
