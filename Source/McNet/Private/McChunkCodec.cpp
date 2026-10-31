#include "McChunkCodec.h"
#include "McChunk.h"
#include "Misc/Compression.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	constexpr uint8 Version = 3;
	constexpr int32 SectionVolume = McCoords::SectionSize * McCoords::SectionSize * McCoords::SectionSize;

	enum class ESection : uint8
	{
		Uniform,  // one state for the whole section
		Full,     // every state, in SectionIndex order
	};
}

TArray<uint8> McChunkCodec::Encode(const FMcChunk& Chunk)
{
	TArray<uint8> Raw;
	FMemoryWriter Writer(Raw);
	uint8 NumSections = static_cast<uint8>(Chunk.NumSections());
	Writer << NumSections;
	TArray<FMcStateId> States;
	States.SetNumUninitialized(SectionVolume);
	for (int32 Index = 0; Index < Chunk.NumSections(); ++Index)
	{
		const FMcPalettedContainer& Blocks = Chunk.GetSection(Index).Blocks;
		if (Blocks.IsUniform())
		{
			uint8 Kind = static_cast<uint8>(ESection::Uniform);
			FMcStateId State = Blocks.Get(0);
			Writer << Kind << State;
			continue;
		}
		uint8 Kind = static_cast<uint8>(ESection::Full);
		Writer << Kind;
		Blocks.CopyTo(States);
		Writer.Serialize(States.GetData(), States.Num() * sizeof(FMcStateId));
	}
	// Then the biome cells, 64 per section.
	TArray<FMcBiomeId> Biomes(Chunk.GetBiomes());
	Writer.Serialize(Biomes.GetData(), Biomes.Num());
	// Then the block entities a player sees (signs, for their text, and pots, for their sherds); what containers
	// hold stays on the server.
	TArray<TPair<FIntVector, FMcBlockEntity>> Shown;
	for (const TPair<FIntVector, FMcBlockEntity>& Pair : Chunk.GetBlockEntities())
	{
		if (Pair.Value.Kind == EMcBlockEntityKind::Sign || Pair.Value.Kind == EMcBlockEntityKind::Pot)
		{
			Shown.Add(Pair);
		}
	}
	int32 NumShown = Shown.Num();
	Writer << NumShown;
	for (TPair<FIntVector, FMcBlockEntity>& Pair : Shown)
	{
		Writer << Pair.Key;
		Pair.Value.Serialize(Writer);
	}

	// Zlib keeps runs of the same state small; the header says how big the bytes were.
	const int32 RawSize = Raw.Num();
	int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, RawSize);
	TArray<uint8> Out;
	Out.SetNumUninitialized(1 + sizeof(int32) + CompressedSize);
	Out[0] = Version;
	FMemory::Memcpy(&Out[1], &RawSize, sizeof(int32));
	if (!FCompression::CompressMemory(NAME_Zlib, &Out[1 + sizeof(int32)], CompressedSize, Raw.GetData(), RawSize))
	{
		return {};
	}
	Out.SetNum(1 + sizeof(int32) + CompressedSize);
	return Out;
}

bool McChunkCodec::Decode(TConstArrayView<uint8> Bytes, FMcChunk& Chunk)
{
	if (Bytes.Num() < 1 + static_cast<int32>(sizeof(int32)) || Bytes[0] != Version)
	{
		return false;
	}
	int32 RawSize = 0;
	FMemory::Memcpy(&RawSize, &Bytes[1], sizeof(int32));
	// A chunk can't be bigger than every section in full, and its signs.
	if (RawSize <= 0 || RawSize > 1 + Chunk.NumSections() * (1 + SectionVolume * static_cast<int32>(sizeof(FMcStateId)) + 64) + 4 + 4 * 1024 * 1024)
	{
		return false;
	}
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(RawSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawSize, &Bytes[1 + sizeof(int32)], Bytes.Num() - 1 - sizeof(int32)))
	{
		return false;
	}

	FMemoryReader Reader(Raw);
	uint8 NumSections = 0;
	Reader << NumSections;
	if (NumSections != Chunk.NumSections())
	{
		return false;
	}
	TArray<FMcStateId> States;
	States.SetNumUninitialized(SectionVolume);
	for (int32 Index = 0; Index < NumSections && !Reader.IsError(); ++Index)
	{
		uint8 Kind = 0;
		Reader << Kind;
		if (Kind == static_cast<uint8>(ESection::Uniform))
		{
			FMcStateId State = 0;
			Reader << State;
			for (FMcStateId& Each : States)
			{
				Each = State;
			}
		}
		else if (Kind == static_cast<uint8>(ESection::Full))
		{
			Reader.Serialize(States.GetData(), States.Num() * sizeof(FMcStateId));
		}
		else
		{
			return false;
		}
		if (Reader.IsError())
		{
			return false;
		}
		Chunk.LoadSection(Index, States);
	}
	TArray<FMcBiomeId> Biomes;
	Biomes.SetNumUninitialized(Chunk.NumSections() * 64);
	Reader.Serialize(Biomes.GetData(), Biomes.Num());
	if (Reader.IsError())
	{
		return false;
	}
	Chunk.LoadBiomes(Biomes);
	int32 NumShown = 0;
	Reader << NumShown;
	if (Reader.IsError() || NumShown < 0 || NumShown > 16 * 16 * Chunk.NumSections() * 16)
	{
		return false;
	}
	for (int32 Index = 0; Index < NumShown; ++Index)
	{
		FIntVector Key;
		FMcBlockEntity Entity;
		Reader << Key;
		if (Reader.IsError() || !Entity.Serialize(Reader))
		{
			return false;
		}
		Chunk.SetBlockEntity(Key.X, Key.Y, Key.Z, MoveTemp(Entity));
	}
	return true;
}
