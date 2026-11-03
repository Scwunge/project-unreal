#include "McChunkSave.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McChunk.h"
#include "McServer.h"
#include "Misc/Compression.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	constexpr uint8 ChunkFormatVersion = 1;
	constexpr int32 SectionVolume = McCoords::SectionSize * McCoords::SectionSize * McCoords::SectionSize;
	constexpr int32 CellsPerSection = 64;
	// Anything bigger than this was not written by us, whatever the compressed header claims.
	constexpr int32 MaxRawSize = 64 * 1024 * 1024;
	constexpr int32 MaxPaletteEntries = 70000;

	enum class ESection : uint8
	{
		Uniform,  // one palette index for the whole section
		Full,     // an index for every block, in SectionIndex order
	};
}

TArray<uint8> McChunkSave::Encode(const FMcChunk& Chunk, const FMcChunkSaveExtras& Extras)
{
	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	if (!Blocks.IsLoaded() || !Biomes.IsLoaded())
	{
		return {};
	}

	// The states and biomes this chunk uses, numbered in the order they're first met.
	TArray<FMcStateId> StatePalette;
	TMap<FMcStateId, uint16> StateIndex;
	auto IndexOfState = [&](FMcStateId State) -> uint16
	{
		if (const uint16* Found = StateIndex.Find(State))
		{
			return *Found;
		}
		const uint16 Index = static_cast<uint16>(StatePalette.Num());
		StatePalette.Add(State);
		StateIndex.Add(State, Index);
		return Index;
	};

	// Sections are indexed before the palette is written, so go through them once into index arrays.
	TArray<uint8> SectionKinds;
	TArray<uint16> SectionData;  // a uniform section's one index, or its 4096
	TArray<FMcStateId> States;
	States.SetNumUninitialized(SectionVolume);
	for (int32 Index = 0; Index < Chunk.NumSections(); ++Index)
	{
		const FMcPalettedContainer& Container = Chunk.GetSection(Index).Blocks;
		if (Container.IsUniform())
		{
			SectionKinds.Add(static_cast<uint8>(ESection::Uniform));
			SectionData.Add(IndexOfState(Container.Get(0)));
			continue;
		}
		SectionKinds.Add(static_cast<uint8>(ESection::Full));
		Container.CopyTo(States);
		for (FMcStateId State : States)
		{
			SectionData.Add(IndexOfState(State));
		}
	}

	TArray<FMcBiomeId> BiomePalette;
	TMap<FMcBiomeId, uint8> BiomeIndex;
	TArray<uint8> BiomeData;
	for (FMcBiomeId Cell : Chunk.GetBiomes())
	{
		uint8 Index = 0;
		if (const uint8* Found = BiomeIndex.Find(Cell))
		{
			Index = *Found;
		}
		else
		{
			Index = static_cast<uint8>(BiomePalette.Num());
			BiomePalette.Add(Cell);
			BiomeIndex.Add(Cell, Index);
		}
		BiomeData.Add(Index);
	}

	TArray<uint8> Raw;
	FMemoryWriter Writer(Raw);
	int32 MinY = Chunk.GetMinY();
	uint8 NumSections = static_cast<uint8>(Chunk.NumSections());
	Writer << MinY << NumSections;

	int32 NumStates = StatePalette.Num();
	Writer << NumStates;
	for (FMcStateId State : StatePalette)
	{
		FString Text = Blocks.Describe(State);
		Writer << Text;
	}
	int32 NumBiomes = BiomePalette.Num();
	Writer << NumBiomes;
	for (FMcBiomeId Biome : BiomePalette)
	{
		FString Text = Biomes.GetName(Biome).ToString();
		Writer << Text;
	}

	int32 DataAt = 0;
	for (uint8 Kind : SectionKinds)
	{
		Writer << Kind;
		const int32 Count = Kind == static_cast<uint8>(ESection::Uniform) ? 1 : SectionVolume;
		Writer.Serialize(const_cast<uint16*>(&SectionData[DataAt]), Count * sizeof(uint16));
		DataAt += Count;
	}
	Writer.Serialize(BiomeData.GetData(), BiomeData.Num());

	uint8 bPopulated = Extras.bPopulated ? 1 : 0;
	Writer << bPopulated;
	TArray<uint8> BlockEntities = Chunk.EncodeBlockEntities();
	Writer << BlockEntities;
	TArray<uint8> Entities = Extras.Entities;
	Writer << Entities;
	TArray<uint8> Structures = Extras.Structures;
	Writer << Structures;

	const int32 RawSize = Raw.Num();
	int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, RawSize);
	TArray<uint8> Out;
	Out.SetNumUninitialized(1 + sizeof(int32) + CompressedSize);
	Out[0] = ChunkFormatVersion;
	FMemory::Memcpy(&Out[1], &RawSize, sizeof(int32));
	if (!FCompression::CompressMemory(NAME_Zlib, &Out[1 + sizeof(int32)], CompressedSize, Raw.GetData(), RawSize))
	{
		return {};
	}
	Out.SetNum(1 + sizeof(int32) + CompressedSize);
	return Out;
}

bool McChunkSave::Decode(TConstArrayView<uint8> Bytes, FMcChunk& Chunk, FMcChunkSaveExtras& Extras)
{
	if (Bytes.Num() < 1 + static_cast<int32>(sizeof(int32)) || Bytes[0] != ChunkFormatVersion)
	{
		return false;
	}
	int32 RawSize = 0;
	FMemory::Memcpy(&RawSize, &Bytes[1], sizeof(int32));
	if (RawSize <= 0 || RawSize > MaxRawSize)
	{
		return false;
	}
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(RawSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawSize, &Bytes[1 + sizeof(int32)], Bytes.Num() - 1 - sizeof(int32)))
	{
		return false;
	}

	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	if (!Blocks.IsLoaded() || !Biomes.IsLoaded())
	{
		return false;
	}

	FMemoryReader Reader(Raw);
	int32 MinY = 0;
	uint8 NumSections = 0;
	Reader << MinY << NumSections;
	if (Reader.IsError() || MinY != Chunk.GetMinY() || NumSections != Chunk.NumSections())
	{
		return false;
	}

	int32 NumStates = 0;
	Reader << NumStates;
	if (Reader.IsError() || NumStates < 0 || NumStates > MaxPaletteEntries)
	{
		return false;
	}
	TArray<FMcStateId> StatePalette;
	StatePalette.Reserve(NumStates);
	for (int32 Index = 0; Index < NumStates; ++Index)
	{
		FString Text;
		Reader << Text;
		if (Reader.IsError())
		{
			return false;
		}
		const TOptional<FMcStateId> State = Blocks.ParseState(Text);
		if (!State)
		{
			UE_LOG(LogMcServer, Warning, TEXT("Saved chunk %d,%d has the block '%s', which this version doesn't know; it loads as air."),
				Chunk.GetPos().X, Chunk.GetPos().Y, *Text);
		}
		StatePalette.Add(State.Get(0));
	}
	int32 NumBiomes = 0;
	Reader << NumBiomes;
	if (Reader.IsError() || NumBiomes < 0 || NumBiomes > 256)
	{
		return false;
	}
	TArray<FMcBiomeId> BiomePalette;
	for (int32 Index = 0; Index < NumBiomes; ++Index)
	{
		FString Text;
		Reader << Text;
		if (Reader.IsError())
		{
			return false;
		}
		BiomePalette.Add(Biomes.Find(FName(*Text)).Get(Biomes.GetPlains()));
	}

	// Into temporaries, so a chunk that turns out to be damaged halfway doesn't leave Chunk half loaded.
	TArray<TArray<FMcStateId>> Loaded;
	Loaded.SetNum(NumSections);
	for (int32 Section = 0; Section < NumSections; ++Section)
	{
		uint8 Kind = 0;
		Reader << Kind;
		TArray<uint16> Indexes;
		if (Kind == static_cast<uint8>(ESection::Uniform))
		{
			Indexes.SetNumUninitialized(1);
		}
		else if (Kind == static_cast<uint8>(ESection::Full))
		{
			Indexes.SetNumUninitialized(SectionVolume);
		}
		else
		{
			return false;
		}
		Reader.Serialize(Indexes.GetData(), Indexes.Num() * sizeof(uint16));
		if (Reader.IsError())
		{
			return false;
		}
		TArray<FMcStateId>& States = Loaded[Section];
		States.SetNumUninitialized(SectionVolume);
		for (int32 Block = 0; Block < SectionVolume; ++Block)
		{
			const uint16 Palette = Indexes[Kind == static_cast<uint8>(ESection::Uniform) ? 0 : Block];
			if (Palette >= StatePalette.Num())
			{
				return false;
			}
			States[Block] = StatePalette[Palette];
		}
	}

	TArray<uint8> BiomeData;
	BiomeData.SetNumUninitialized(NumSections * CellsPerSection);
	Reader.Serialize(BiomeData.GetData(), BiomeData.Num());
	if (Reader.IsError())
	{
		return false;
	}
	TArray<FMcBiomeId> LoadedBiomes;
	LoadedBiomes.SetNumUninitialized(BiomeData.Num());
	for (int32 Cell = 0; Cell < BiomeData.Num(); ++Cell)
	{
		if (BiomeData[Cell] >= BiomePalette.Num())
		{
			return false;
		}
		LoadedBiomes[Cell] = BiomePalette[BiomeData[Cell]];
	}

	uint8 bPopulated = 0;
	TArray<uint8> BlockEntities;
	TArray<uint8> Entities;
	TArray<uint8> Structures;
	Reader << bPopulated << BlockEntities << Entities;
	if (!Reader.IsError() && !Reader.AtEnd())
	{
		Reader << Structures;
	}
	if (Reader.IsError())
	{
		return false;
	}

	for (int32 Section = 0; Section < NumSections; ++Section)
	{
		Chunk.LoadSection(Section, Loaded[Section]);
	}
	Chunk.LoadBiomes(LoadedBiomes);
	if (!Chunk.DecodeBlockEntities(BlockEntities))
	{
		UE_LOG(LogMcServer, Warning, TEXT("Saved chunk %d,%d has damaged block entities; they're dropped."), Chunk.GetPos().X, Chunk.GetPos().Y);
	}
	Extras.bPopulated = bPopulated != 0;
	Extras.Entities = MoveTemp(Entities);
	Extras.Structures = MoveTemp(Structures);
	return true;
}
