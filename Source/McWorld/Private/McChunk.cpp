#include "McChunk.h"
#include "McBlockRegistry.h"

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
	++Revision;
	return Previous;
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
