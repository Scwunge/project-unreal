#include "McGenRegion.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"

FMcGenRegion::FMcGenRegion(FIntPoint InCentre, TArrayView<FMcChunk* const> InChunks)
	: Centre(InCentre)
{
	check(InChunks.Num() == 9);
	for (int32 Index = 0; Index < 9; ++Index)
	{
		Chunks[Index] = InChunks[Index];
	}
	const FMcChunk* Middle = Chunks[4];
	check(Middle);
	MinY = Middle->GetMinY();
	MaxY = Middle->GetMaxY();
	for (TArray<int32>& Map : Heights)
	{
		Map.Init(MIN_int32, 48 * 48);
	}
	const FMcBlockTags& Tags = FMcBlockTags::Get();
	MotionTag = Tags.Find(TEXT("minecraft:blocks_motion_in_heightmap"));
	MotionNoLeavesTag = Tags.Find(TEXT("minecraft:blocks_motion_in_heightmap_no_leaves"));
}

TOptional<EMcHeightmap> FMcGenRegion::ParseHeightmap(const FString& Name)
{
	if (Name == TEXT("WORLD_SURFACE_WG")) return EMcHeightmap::WorldSurfaceWG;
	if (Name == TEXT("WORLD_SURFACE")) return EMcHeightmap::WorldSurface;
	if (Name == TEXT("OCEAN_FLOOR_WG")) return EMcHeightmap::OceanFloorWG;
	if (Name == TEXT("OCEAN_FLOOR")) return EMcHeightmap::OceanFloor;
	if (Name == TEXT("MOTION_BLOCKING")) return EMcHeightmap::MotionBlocking;
	if (Name == TEXT("MOTION_BLOCKING_NO_LEAVES")) return EMcHeightmap::MotionBlockingNoLeaves;
	return {};
}

FMcChunk* FMcGenRegion::ChunkAt(int32 X, int32 Z) const
{
	const int32 DX = (X >> 4) - Centre.X + 1;
	const int32 DZ = (Z >> 4) - Centre.Y + 1;
	if (DX < 0 || DX > 2 || DZ < 0 || DZ > 2)
	{
		return nullptr;
	}
	return Chunks[DZ * 3 + DX];
}

int32 FMcGenRegion::ColumnIndex(int32 X, int32 Z) const
{
	return (Z - (Centre.Y - 1) * 16) * 48 + (X - (Centre.X - 1) * 16);
}

bool FMcGenRegion::Contains(int32 X, int32 Z) const
{
	return ChunkAt(X, Z) != nullptr;
}

FMcStateId FMcGenRegion::GetBlock(const FIntVector& Pos) const
{
	const FMcChunk* Chunk = ChunkAt(Pos.X, Pos.Z);
	return Chunk && Pos.Y >= MinY && Pos.Y <= MaxY ? Chunk->GetBlock(Pos.X & 15, Pos.Y, Pos.Z & 15) : 0;
}

bool FMcGenRegion::IsAir(FMcStateId State) const
{
	return FMcBlockRegistry::Get().IsAir(State);
}

bool FMcGenRegion::Counts(EMcHeightmap Heightmap, FMcStateId State) const
{
	const FMcBlockTags& Tags = FMcBlockTags::Get();
	switch (Heightmap)
	{
	case EMcHeightmap::WorldSurfaceWG:
	case EMcHeightmap::WorldSurface:
		return !IsAir(State);
	case EMcHeightmap::OceanFloorWG:
	case EMcHeightmap::OceanFloor:
		return Tags.Has(MotionTag, State);
	case EMcHeightmap::MotionBlocking:
		return Tags.Has(MotionTag, State) || FMcBlockShapes::Get().GetFluid(State) != EMcFluid::None;
	case EMcHeightmap::MotionBlockingNoLeaves:
		return Tags.Has(MotionNoLeavesTag, State) || FMcBlockShapes::Get().GetFluid(State) != EMcFluid::None;
	default:
		return false;
	}
}

bool FMcGenRegion::SetBlock(const FIntVector& Pos, FMcStateId State)
{
	FMcChunk* Chunk = ChunkAt(Pos.X, Pos.Z);
	if (!Chunk || Pos.Y < MinY || Pos.Y > MaxY || ReadOnly[((Pos.Z >> 4) - Centre.Y + 1) * 3 + (Pos.X >> 4) - Centre.X + 1])
	{
		return false;
	}
	Chunk->SetBlock(Pos.X & 15, Pos.Y, Pos.Z & 15, State);
	const int32 Column = ColumnIndex(Pos.X, Pos.Z);
	for (int32 Map = 0; Map < static_cast<int32>(EMcHeightmap::Count); ++Map)
	{
		int32& Height = Heights[Map][Column];
		if (Height == MIN_int32)
		{
			continue;
		}
		if (Counts(static_cast<EMcHeightmap>(Map), State))
		{
			Height = FMath::Max(Height, Pos.Y + 1);
		}
		else if (Pos.Y == Height - 1)
		{
			// The top block went: find the next one down when it's next asked for.
			Height = MIN_int32;
		}
	}
	return true;
}

bool FMcGenRegion::SetBlockEntity(const FIntVector& Pos, const FMcBlockEntity& Entity)
{
	FMcChunk* Chunk = ChunkAt(Pos.X, Pos.Z);
	if (!Chunk || Pos.Y < MinY || Pos.Y > MaxY || ReadOnly[((Pos.Z >> 4) - Centre.Y + 1) * 3 + (Pos.X >> 4) - Centre.X + 1])
	{
		return false;
	}
	Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, Entity);
	return true;
}

const FMcBlockEntity* FMcGenRegion::GetBlockEntity(const FIntVector& Pos) const
{
	const FMcChunk* Chunk = ChunkAt(Pos.X, Pos.Z);
	return Chunk && Pos.Y >= MinY && Pos.Y <= MaxY ? Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15) : nullptr;
}

int32 FMcGenRegion::GetHeight(EMcHeightmap Heightmap, int32 X, int32 Z)
{
	const FMcChunk* Chunk = ChunkAt(X, Z);
	if (!Chunk)
	{
		return MinY;
	}
	int32& Height = Heights[static_cast<int32>(Heightmap)][ColumnIndex(X, Z)];
	if (Height == MIN_int32)
	{
		Height = MinY;
		for (int32 Y = MaxY; Y >= MinY; --Y)
		{
			// A section with nothing in it has nothing to count: jump to its bottom.
			if (Chunk->IsInHeight(Y) && Chunk->GetSection(Chunk->SectionIndexOfY(Y)).IsEmpty())
			{
				Y = Chunk->SectionMinY(Chunk->SectionIndexOfY(Y));
				continue;
			}
			if (Counts(Heightmap, Chunk->GetBlock(X & 15, Y, Z & 15)))
			{
				Height = Y + 1;
				break;
			}
		}
	}
	return Height;
}

FMcBiomeId FMcGenRegion::GetBiome(const FIntVector& Pos) const
{
	const FMcChunk* Chunk = ChunkAt(Pos.X, Pos.Z);
	return Chunk ? Chunk->GetBiome(Pos.X & 15, FMath::Clamp(Pos.Y, MinY, MaxY), Pos.Z & 15) : FMcBiomeRegistry::Get().GetPlains();
}
