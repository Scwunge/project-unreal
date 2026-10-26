// Section meshing. M1 draws every non-air block as a flat-coloured cube; M2 replaces the colours with the
// baked block models and textures from McAssets.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "McCore.h"

class FMcBlockRegistry;
class FMcBlockWorld;

// A copy of one section plus a one-block border from its neighbours, so a worker thread can mesh it while
// the world goes on changing.
struct MCRENDER_API FMcSectionSnapshot
{
	static constexpr int32 Padded = 18;

	FIntPoint ChunkPos;
	int32 SectionIndex = 0;
	int32 MinY = 0;  // world height of the section's bottom block
	TArray<FMcStateId> Blocks;  // Padded^3, see Index()

	// X, Y and Z are local to the section and run from -1 to 16.
	static FORCEINLINE int32 Index(int32 X, int32 Y, int32 Z) { return ((Y + 1) * Padded + (Z + 1)) * Padded + (X + 1); }
	FORCEINLINE FMcStateId Get(int32 X, int32 Y, int32 Z) const { return Blocks[Index(X, Y, Z)]; }

	// Game thread. Unloaded neighbours and heights outside the world read as air.
	static FMcSectionSnapshot Capture(const FMcBlockWorld& World, FIntPoint ChunkPos, int32 SectionIndex);
};

struct MCRENDER_API FMcSectionMesh
{
	int32 SectionIndex = 0;
	TArray<FVector> Positions;  // Unreal units, relative to the chunk column's origin at height 0
	TArray<FVector> Normals;
	TArray<FColor> Colors;
	TArray<int32> Triangles;

	bool IsEmpty() const { return Triangles.Num() == 0; }
};

// What the greybox mesher knows about each state, built once on the game thread and shared read-only.
struct MCRENDER_API FMcGreyboxPalette
{
	TArray<FColor> StateColors;  // indexed by state id
	TBitArray<> Solid;           // indexed by state id; M1 treats every non-air block as a full opaque cube
	bool bFlipWinding = false;

	static TSharedRef<const FMcGreyboxPalette> Build(const FMcBlockRegistry& Registry, bool bFlipWinding);
};

namespace McGreyboxMesher
{
	// Any thread. One quad per block face that touches a non-solid neighbour.
	MCRENDER_API FMcSectionMesh Build(const FMcSectionSnapshot& Snapshot, const FMcGreyboxPalette& Palette);
}
