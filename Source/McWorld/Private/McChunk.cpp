#include "McChunk.h"
#include "McBlockRegistry.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

FMcChunk::FMcChunk(FIntPoint InPos, int32 InMinY, int32 InNumSections)
	: Pos(InPos)
	, MinY(InMinY)
{
	Sections.SetNum(InNumSections);
	Biomes.SetNumZeroed(InNumSections * 64);
}

FMcBiomeId FMcChunk::GetBiome(int32 X, int32 Y, int32 Z) const
{
	const int32 CellY = FMath::Clamp((Y - MinY) >> 2, 0, Sections.Num() * 4 - 1);
	return Biomes[(CellY * 4 + (Z >> 2)) * 4 + (X >> 2)];
}

void FMcChunk::SetBiomeCell(int32 CellX, int32 CellY, int32 CellZ, FMcBiomeId Biome)
{
	Biomes[(CellY * 4 + CellZ) * 4 + CellX] = Biome;
}

void FMcChunk::FillBiome(FMcBiomeId Biome)
{
	for (FMcBiomeId& Cell : Biomes)
	{
		Cell = Biome;
	}
	++Revision;
}

void FMcChunk::LoadBiomes(TConstArrayView<FMcBiomeId> InBiomes)
{
	if (InBiomes.Num() == Biomes.Num())
	{
		FMemory::Memcpy(Biomes.GetData(), InBiomes.GetData(), Biomes.Num());
		++Revision;
	}
}

FMcStateId FMcChunk::GetBlock(int32 X, int32 Y, int32 Z) const
{
	if (!IsInHeight(Y))
	{
		return 0;
	}
	return Sections[SectionIndexOfY(Y)].Blocks.Get(McCoords::SectionIndex(X, McCoords::BlockToLocal(Y - MinY), Z));
}

FMcStateId FMcChunk::SetBlock(int32 X, int32 Y, int32 Z, FMcStateId State)
{
	if (!IsInHeight(Y))
	{
		return 0;
	}

	FMcChunkSection& Section = Sections[SectionIndexOfY(Y)];
	const int32 Index = McCoords::SectionIndex(X, McCoords::BlockToLocal(Y - MinY), Z);
	const FMcStateId Previous = Section.Blocks.Get(Index);
	if (Previous == State)
	{
		return Previous;
	}

	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	Section.NonAirCount += (Registry.IsAir(Previous) ? 0 : -1) + (Registry.IsAir(State) ? 0 : 1);
	Section.Blocks.Set(Index, State);
	if (!BlockEntities.IsEmpty() && Registry.GetBlockOf(Previous).Name != Registry.GetBlockOf(State).Name)
	{
		BlockEntities.Remove(FIntVector(X, Y, Z));
	}
	++Revision;
	return Previous;
}

const FMcBlockEntity* FMcChunk::FindBlockEntity(int32 X, int32 Y, int32 Z) const
{
	return BlockEntities.Find(FIntVector(X, Y, Z));
}

FMcBlockEntity* FMcChunk::FindBlockEntity(int32 X, int32 Y, int32 Z)
{
	return BlockEntities.Find(FIntVector(X, Y, Z));
}

void FMcChunk::SetBlockEntity(int32 X, int32 Y, int32 Z, FMcBlockEntity Entity)
{
	if (IsInHeight(Y))
	{
		BlockEntities.Add(FIntVector(X, Y, Z), MoveTemp(Entity));
	}
}

bool FMcChunk::RemoveBlockEntity(int32 X, int32 Y, int32 Z)
{
	return BlockEntities.Remove(FIntVector(X, Y, Z)) > 0;
}

TArray<uint8> FMcChunk::EncodeBlockEntities() const
{
	// In position order, so the same entities always make the same bytes.
	TArray<FIntVector> Positions;
	BlockEntities.GetKeys(Positions);
	Positions.Sort([](const FIntVector& A, const FIntVector& B)
	{
		return A.Y != B.Y ? A.Y < B.Y : A.Z != B.Z ? A.Z < B.Z : A.X < B.X;
	});
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	int32 Count = Positions.Num();
	Writer << Count;
	for (const FIntVector& At : Positions)
	{
		int32 X = At.X;
		int32 Y = At.Y;
		int32 Z = At.Z;
		Writer << X << Y << Z;
		FMcBlockEntity Copy = BlockEntities[At];
		Copy.Serialize(Writer);
	}
	return Bytes;
}

bool FMcChunk::DecodeBlockEntities(TConstArrayView<uint8> Bytes)
{
	BlockEntities.Reset();
	FMemoryReaderView Reader(Bytes);
	int32 Count = 0;
	Reader << Count;
	if (Reader.IsError() || Count < 0 || Count > 65536)
	{
		return false;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;
		Reader << X << Y << Z;
		FMcBlockEntity Entity;
		if (Reader.IsError() || X < 0 || X > 15 || Z < 0 || Z > 15 || !IsInHeight(Y) || !Entity.Serialize(Reader))
		{
			BlockEntities.Reset();
			return false;
		}
		BlockEntities.Add(FIntVector(X, Y, Z), MoveTemp(Entity));
	}
	return true;
}

void FMcChunk::LoadSection(int32 Index, TConstArrayView<FMcStateId> States)
{
	check(States.Num() == McCoords::SectionSize * McCoords::SectionSize * McCoords::SectionSize);
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcChunkSection& Section = Sections[Index];
	Section.Blocks.Fill(States[0]);
	Section.NonAirCount = 0;
	for (int32 I = 0; I < States.Num(); ++I)
	{
		if (States[I] != States[0])
		{
			Section.Blocks.Set(I, States[I]);
		}
		Section.NonAirCount += Registry.IsAir(States[I]) ? 0 : 1;
	}
	++Revision;
}

void FMcChunk::FillLayer(int32 Y, FMcStateId State)
{
	for (int32 Z = 0; Z < McCoords::SectionSize; ++Z)
	{
		for (int32 X = 0; X < McCoords::SectionSize; ++X)
		{
			SetBlock(X, Y, Z, State);
		}
	}
}
