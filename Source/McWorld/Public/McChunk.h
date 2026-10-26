// Chunk columns: 16x16 blocks across, split into 16-block-tall sections over the dimension's height.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "McPalettedContainer.h"

namespace McCoords
{
	constexpr int32 SectionSize = 16;

	// Index of a block inside a section, from its local coordinates (each 0 to 15), in y, z, x order.
	FORCEINLINE int32 SectionIndex(int32 X, int32 Y, int32 Z) { return (Y << 8) | (Z << 4) | X; }

	// The chunk (or section) holding a block coordinate, rounding towards negative infinity.
	FORCEINLINE int32 BlockToChunk(int32 Block) { return Block >> 4; }
	FORCEINLINE int32 BlockToLocal(int32 Block) { return Block & 15; }

	// One block is one metre: 100 Unreal units.
	constexpr double UnitsPerBlock = 100.0;

	// Minecraft axes (x east, y up, z south) to Unreal's (X forward, Y right, Z up): x -> X, z -> Y, y -> Z.
	// Swapping y and z turns Minecraft's right-handed axes into Unreal's left-handed ones without mirroring.
	FORCEINLINE FVector BlockToUnreal(const FVector& Block) { return FVector(Block.X, Block.Z, Block.Y) * UnitsPerBlock; }
	FORCEINLINE FVector UnrealToBlock(const FVector& Location) { return FVector(Location.X, Location.Z, Location.Y) / UnitsPerBlock; }
}

struct MCWORLD_API FMcChunkSection
{
	FMcPalettedContainer Blocks;
	int32 NonAirCount = 0;

	bool IsEmpty() const { return NonAirCount == 0; }
};

class MCWORLD_API FMcChunk
{
public:
	FMcChunk(FIntPoint InPos, int32 InMinY, int32 InNumSections);

	FIntPoint GetPos() const { return Pos; }
	int32 GetMinY() const { return MinY; }
	int32 GetMaxY() const { return MinY + Sections.Num() * McCoords::SectionSize - 1; }
	int32 NumSections() const { return Sections.Num(); }
	bool IsInHeight(int32 Y) const { return Y >= MinY && Y <= GetMaxY(); }

	// The section holding world height Y, as an index from the bottom of the world.
	int32 SectionIndexOfY(int32 Y) const { return (Y - MinY) >> 4; }
	// World height of the bottom block of a section.
	int32 SectionMinY(int32 SectionIndex) const { return MinY + SectionIndex * McCoords::SectionSize; }

	const FMcChunkSection& GetSection(int32 Index) const { return Sections[Index]; }

	// X and Z are local (0 to 15), Y is the world height. Outside the height range the world is air (state 0).
	FMcStateId GetBlock(int32 X, int32 Y, int32 Z) const;
	// Returns the previous state. Heights outside the range are ignored.
	FMcStateId SetBlock(int32 X, int32 Y, int32 Z, FMcStateId State);

	// Sets a whole horizontal layer, e.g. for flat worlds.
	void FillLayer(int32 Y, FMcStateId State);

	// Goes up by one on every change; the renderer compares it to know when to mesh again.
	uint32 GetRevision() const { return Revision; }

private:
	FIntPoint Pos;
	int32 MinY;
	TArray<FMcChunkSection> Sections;
	uint32 Revision = 0;
};
