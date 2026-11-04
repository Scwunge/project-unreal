#include "McJavaExport.h"
#include "McJavaImport.h"
#include "McChunkSave.h"
#include "McRegionFile.h"
#include "McWorldSave.h"
#include "McChunk.h"
#include "McNbt.h"
#include "McBlockRegistry.h"
#include "McBiomeRegistry.h"
#include "McDimensionType.h"
#include "McGenRegion.h"
#include "McBlockTags.h"
#include "McBlockShapes.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace
{
	using EType = FMcNbt::EType;

	FMcNbt MakeTag(EType Type)
	{
		FMcNbt Tag;
		Tag.Type = Type;
		return Tag;
	}

	FMcNbt Int(int64 Value, EType Type = EType::Int)
	{
		FMcNbt Tag = MakeTag(Type);
		Tag.Integer = Value;
		return Tag;
	}

	FMcNbt Real(double Value, EType Type)
	{
		FMcNbt Tag = MakeTag(Type);
		Tag.Real = Value;
		return Tag;
	}

	FMcNbt Str(const FString& Value)
	{
		FMcNbt Tag = MakeTag(EType::String);
		Tag.String = Value;
		return Tag;
	}

	void Add(FMcNbt& Compound, const TCHAR* Key, FMcNbt Value)
	{
		Compound.Keys.Add(Key);
		Compound.Values.Add(MoveTemp(Value));
	}

	// "minecraft:oak_stairs[facing=north,half=top]" as the game's palette entry: Name, and Properties when there are any.
	FMcNbt MakeBlockEntry(const FString& Described)
	{
		FMcNbt Entry = MakeTag(EType::Compound);
		FString Name = Described, Props;
		int32 Bracket;
		if (Described.FindChar(TEXT('['), Bracket))
		{
			Name = Described.Left(Bracket);
			Props = Described.Mid(Bracket + 1);
			Props.RemoveFromEnd(TEXT("]"));
		}
		Add(Entry, TEXT("Name"), Str(Name));
		if (!Props.IsEmpty())
		{
			FMcNbt Properties = MakeTag(EType::Compound);
			TArray<FString> Pairs;
			Props.ParseIntoArray(Pairs, TEXT(","), true);
			for (const FString& Pair : Pairs)
			{
				FString Key, Value;
				if (Pair.Split(TEXT("="), &Key, &Value))
				{
					Properties.Keys.Add(Key);
					Properties.Values.Add(Str(Value));
				}
			}
			Add(Entry, TEXT("Properties"), MoveTemp(Properties));
		}
		return Entry;
	}

	FMcNbt LongArray(const TArray<int64>& Longs)
	{
		FMcNbt Tag = MakeTag(EType::LongArray);
		for (int64 Value : Longs)
		{
			Tag.Values.Add(Int(Value, EType::Long));
		}
		return Tag;
	}

	void WriteBig32(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>(Value >> 24));
		Out.Add(static_cast<uint8>(Value >> 16));
		Out.Add(static_cast<uint8>(Value >> 8));
		Out.Add(static_cast<uint8>(Value));
	}

	bool Compress(FName Format, TConstArrayView<uint8> In, TArray<uint8>& Out)
	{
		int32 Size = FCompression::CompressMemoryBound(Format, In.Num()) + 64;
		Out.SetNumUninitialized(Size);
		if (!FCompression::CompressMemory(Format, Out.GetData(), Size, In.GetData(), In.Num()))
		{
			return false;
		}
		Out.SetNum(Size);
		return true;
	}
}

void McJavaExport::PackIndices(TConstArrayView<int32> Indexes, int32 Bits, TArray<int64>& OutLongs)
{
	const int32 PerLong = 64 / Bits;
	OutLongs.Init(0, FMath::DivideAndRoundUp(Indexes.Num(), PerLong));
	for (int32 Index = 0; Index < Indexes.Num(); ++Index)
	{
		OutLongs[Index / PerLong] |= static_cast<int64>(static_cast<uint64>(Indexes[Index]) << ((Index % PerLong) * Bits));
	}
}

void McJavaExport::MakeChunk(const FMcChunk& Chunk, FMcNbt& Out, int64 LastUpdate)
{
	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	Out = MakeTag(EType::Compound);
	Add(Out, TEXT("DataVersion"), Int(DataVersion));
	Add(Out, TEXT("xPos"), Int(Chunk.GetPos().X));
	Add(Out, TEXT("zPos"), Int(Chunk.GetPos().Y));
	Add(Out, TEXT("yPos"), Int(Chunk.GetMinY() / McCoords::SectionSize));
	Add(Out, TEXT("Status"), Str(TEXT("minecraft:full")));
	Add(Out, TEXT("LastUpdate"), Int(LastUpdate, EType::Long));
	Add(Out, TEXT("InhabitedTime"), Int(0, EType::Long));
	// The game relights a chunk that says its light isn't on.
	Add(Out, TEXT("isLightOn"), Int(0, EType::Byte));
	FMcNbt Sections = MakeTag(EType::List);
	for (int32 Section = 0; Section < Chunk.NumSections(); ++Section)
	{
		FMcNbt Entry = MakeTag(EType::Compound);
		Add(Entry, TEXT("Y"), Int(Chunk.GetMinY() / McCoords::SectionSize + Section, EType::Byte));
		// Blocks: a palette of the states used, and an index for each of the 4096 (y, then z, then x).
		TMap<FMcStateId, int32> Used;
		TArray<FMcStateId> Order;
		TArray<int32> Indexes;
		Indexes.SetNumUninitialized(4096);
		const FMcChunkSection& Source = Chunk.GetSection(Section);
		for (int32 Block = 0; Block < 4096; ++Block)
		{
			const FMcStateId State = Source.Blocks.Get(Block);
			int32* Found = Used.Find(State);
			if (!Found)
			{
				Found = &Used.Add(State, Order.Num());
				Order.Add(State);
			}
			Indexes[Block] = *Found;
		}
		FMcNbt BlockStates = MakeTag(EType::Compound);
		FMcNbt Palette = MakeTag(EType::List);
		for (FMcStateId State : Order)
		{
			Palette.Values.Add(MakeBlockEntry(Blocks.Describe(State)));
		}
		Add(BlockStates, TEXT("palette"), MoveTemp(Palette));
		if (Order.Num() > 1)
		{
			TArray<int64> Longs;
			PackIndices(Indexes, McJavaImport::GetBitsPerEntry(Order.Num(), 4), Longs);
			Add(BlockStates, TEXT("data"), LongArray(Longs));
		}
		Add(Entry, TEXT("block_states"), MoveTemp(BlockStates));
		// Biomes: the same, for 4 x 4 x 4 cells (64 per section, y then z then x).
		TMap<FMcBiomeId, int32> BiomeUsed;
		TArray<FMcBiomeId> BiomeOrder;
		TArray<int32> Cells;
		Cells.SetNumUninitialized(64);
		for (int32 Cell = 0; Cell < 64; ++Cell)
		{
			const FMcBiomeId Biome = Chunk.GetBiomes()[Section * 64 + Cell];
			int32* Found = BiomeUsed.Find(Biome);
			if (!Found)
			{
				Found = &BiomeUsed.Add(Biome, BiomeOrder.Num());
				BiomeOrder.Add(Biome);
			}
			Cells[Cell] = *Found;
		}
		FMcNbt BiomeTag = MakeTag(EType::Compound);
		FMcNbt BiomePalette = MakeTag(EType::List);
		for (FMcBiomeId Biome : BiomeOrder)
		{
			BiomePalette.Values.Add(Str(Biomes.GetName(Biome).ToString()));
		}
		Add(BiomeTag, TEXT("palette"), MoveTemp(BiomePalette));
		if (BiomeOrder.Num() > 1)
		{
			TArray<int64> Longs;
			PackIndices(Cells, McJavaImport::GetBitsPerEntry(BiomeOrder.Num(), 1), Longs);
			Add(BiomeTag, TEXT("data"), LongArray(Longs));
		}
		Add(Entry, TEXT("biomes"), MoveTemp(BiomeTag));
		Sections.Values.Add(MoveTemp(Entry));
	}
	Add(Out, TEXT("sections"), MoveTemp(Sections));
	Add(Out, TEXT("block_entities"), MakeTag(EType::List));
	// Heightmaps (observed in a real 26.3 save): four long arrays, each column's height above the world's bottom in
	// 9 bits, 7 to a long, columns in z then x order.
	FMcNbt Heightmaps = MakeTag(EType::Compound);
	FMcBlockTags::Get().EnsureLoaded();
	FMcBlockShapes::Get().EnsureLoaded();
	FMcChunk* Middle = const_cast<FMcChunk*>(&Chunk);
	FMcChunk* Nine[9] = {};
	Nine[4] = Middle;
	FMcGenRegion Region(Chunk.GetPos(), Nine);
	const struct { const TCHAR* Name; EMcHeightmap Map; } Maps[] = {
		{ TEXT("OCEAN_FLOOR"), EMcHeightmap::OceanFloor },
		{ TEXT("MOTION_BLOCKING_NO_LEAVES"), EMcHeightmap::MotionBlockingNoLeaves },
		{ TEXT("MOTION_BLOCKING"), EMcHeightmap::MotionBlocking },
		{ TEXT("WORLD_SURFACE"), EMcHeightmap::WorldSurface },
	};
	for (const auto& Entry : Maps)
	{
		TArray<int32> Columns;
		Columns.SetNumUninitialized(256);
		for (int32 Z = 0; Z < 16; ++Z)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				Columns[Z * 16 + X] = Region.GetHeight(Entry.Map, Chunk.GetPos().X * 16 + X, Chunk.GetPos().Y * 16 + Z) - Chunk.GetMinY();
			}
		}
		TArray<int64> Longs;
		PackIndices(Columns, 9, Longs);
		Add(Heightmaps, Entry.Name, LongArray(Longs));
	}
	Add(Out, TEXT("Heightmaps"), MoveTemp(Heightmaps));
	// Structures: none are kept; the game wants both keys. Pending ticks: none are kept. PostProcessing: one list per
	// section, empty.
	FMcNbt Structures = MakeTag(EType::Compound);
	Add(Structures, TEXT("References"), MakeTag(EType::Compound));
	Add(Structures, TEXT("starts"), MakeTag(EType::Compound));
	Add(Out, TEXT("structures"), MoveTemp(Structures));
	Add(Out, TEXT("block_ticks"), MakeTag(EType::List));
	Add(Out, TEXT("fluid_ticks"), MakeTag(EType::List));
	FMcNbt PostProcessing = MakeTag(EType::List);
	for (int32 Section = 0; Section < Chunk.NumSections(); ++Section)
	{
		PostProcessing.Values.Add(MakeTag(EType::List));
	}
	Add(Out, TEXT("PostProcessing"), MoveTemp(PostProcessing));
}

void McJavaExport::MakeRegion(const TMap<FIntPoint, TArray<uint8>>& Chunks, TArray<uint8>& OutFile)
{
	// Two sectors of header (where each chunk is, and when it was saved), then each chunk on a sector boundary: its
	// length, the compression (2: zlib) and the bytes.
	OutFile.Init(0, 8192);
	const int32 Now = static_cast<int32>(FDateTime::UtcNow().ToUnixTimestamp());
	for (const TPair<FIntPoint, TArray<uint8>>& Pair : Chunks)
	{
		TArray<uint8> Packed;
		if (!Compress(NAME_Zlib, Pair.Value, Packed))
		{
			continue;
		}
		const int32 Sector = OutFile.Num() / 4096;
		TArray<uint8> Record;
		WriteBig32(Record, Packed.Num() + 1);
		Record.Add(2);
		Record.Append(Packed);
		const int32 Sectors = FMath::DivideAndRoundUp(Record.Num(), 4096);
		Record.AddZeroed(Sectors * 4096 - Record.Num());
		OutFile.Append(Record);
		const int32 Slot = ((Pair.Key.Y & 31) * 32 + (Pair.Key.X & 31)) * 4;
		const uint32 Location = (static_cast<uint32>(Sector) << 8) | static_cast<uint32>(Sectors);
		for (int32 Byte = 0; Byte < 4; ++Byte)
		{
			OutFile[Slot + Byte] = static_cast<uint8>(Location >> (24 - 8 * Byte));
			OutFile[4096 + Slot + Byte] = static_cast<uint8>(static_cast<uint32>(Now) >> (24 - 8 * Byte));
		}
	}
}

bool McJavaExport::ExportWorld(const FString& WorldDir, const FString& OutFolder, FStats& OutStats)
{
	FMcLevelData Level;
	if (!McWorldSave::LoadLevel(WorldDir, Level) || !FMcBlockRegistry::Get().EnsureLoaded())
	{
		return false;
	}
	IFileManager& Files = IFileManager::Get();
	for (const TCHAR* Dimension : { TEXT("minecraft:overworld"), TEXT("minecraft:the_nether"), TEXT("minecraft:the_end") })
	{
		const FString Source = WorldDir / McWorldSave::GetDimensionFolder(Dimension);
		FMcDimensionType Type;
		if (!Files.DirectoryExists(*Source) || !FMcDimensionType::Load(Dimension, Type))
		{
			continue;
		}
		FString Namespace, Path;
		FString(Dimension).Split(TEXT(":"), &Namespace, &Path);
		const FString Target = OutFolder / TEXT("dimensions") / Namespace / Path / TEXT("region");
		TArray<FString> Names;
		Files.FindFiles(Names, *(Source / TEXT("*.mcr")), true, false);
		for (const FString& Name : Names)
		{
			TArray<FString> Parts;
			Name.ParseIntoArray(Parts, TEXT("."), true);
			if (Parts.Num() != 4 || Parts[0] != TEXT("r") || !Parts[1].IsNumeric() || !Parts[2].IsNumeric())
			{
				continue;
			}
			const int32 RegionX = FCString::Atoi(*Parts[1]);
			const int32 RegionZ = FCString::Atoi(*Parts[2]);
			FMcRegionFile Region(Source / Name);
			TBitArray<> Present;
			Region.GetPresent(Present);
			TMap<FIntPoint, TArray<uint8>> Chunks;
			for (int32 Index = 0; Index < Present.Num(); ++Index)
			{
				if (!Present[Index])
				{
					continue;
				}
				const FIntPoint Pos(RegionX * 32 + Index % 32, RegionZ * 32 + Index / 32);
				TArray<uint8> Payload;
				FMcChunk Chunk(Pos, Type.MinY, Type.Height / McCoords::SectionSize);
				FMcChunkSaveExtras Extras;
				if (!Region.Read(Pos, Payload) || !McChunkSave::Decode(Payload, Chunk, Extras))
				{
					++OutStats.Failed;
					continue;
				}
				FMcNbt Root;
				MakeChunk(Chunk, Root, static_cast<int64>(Level.DayTime));
				TArray<uint8> Bytes;
				Root.WriteRoot(Bytes);
				Chunks.Add(Pos, MoveTemp(Bytes));
				++OutStats.Chunks;
			}
			if (Chunks.IsEmpty())
			{
				continue;
			}
			TArray<uint8> File;
			MakeRegion(Chunks, File);
			Files.MakeDirectory(*Target, true);
			if (!FFileHelper::SaveArrayToFile(File, *(Target / FString::Printf(TEXT("r.%d.%d.mca"), RegionX, RegionZ))))
			{
				return false;
			}
			++OutStats.Regions;
		}
	}
	// level.dat, gzip-compressed, in the current layout.
	FMcNbt Data = MakeTag(EType::Compound);
	Add(Data, TEXT("LevelName"), Str(Level.Name));
	Add(Data, TEXT("DataVersion"), Int(DataVersion));
	FMcNbt Version = MakeTag(EType::Compound);
	Add(Version, TEXT("Id"), Int(DataVersion));
	Add(Version, TEXT("Name"), Str(TEXT("26.3")));
	Add(Version, TEXT("Series"), Str(TEXT("main")));
	Add(Version, TEXT("Snapshot"), Int(0, EType::Byte));
	Add(Data, TEXT("Version"), MoveTemp(Version));
	Add(Data, TEXT("GameType"), Int(Level.GameMode == TEXT("creative") ? 1 : 0));
	Add(Data, TEXT("Time"), Int(static_cast<int64>(Level.DayTime), EType::Long));
	// Observed in a real 26.3 level.dat (ref save): no DayTime here, and a storage "version" of 19133.
	Add(Data, TEXT("version"), Int(19133));
	Add(Data, TEXT("LastPlayed"), Int(FDateTime::UtcNow().ToUnixTimestamp() * 1000, EType::Long));
	Add(Data, TEXT("initialized"), Int(1, EType::Byte));
	Add(Data, TEXT("allowCommands"), Int(1, EType::Byte));
	FMcNbt Spawn = MakeTag(EType::Compound);
	FMcNbt Pos = MakeTag(EType::IntArray);
	Pos.Values = { Int(Level.Spawn.X), Int(Level.Spawn.Y), Int(Level.Spawn.Z) };
	Add(Spawn, TEXT("pos"), MoveTemp(Pos));
	Add(Spawn, TEXT("dimension"), Str(Level.Dimension));
	Add(Spawn, TEXT("yaw"), Real(0.0, EType::Float));
	Add(Spawn, TEXT("pitch"), Real(0.0, EType::Float));
	Add(Data, TEXT("spawn"), MoveTemp(Spawn));
	FMcNbt Difficulty = MakeTag(EType::Compound);
	static const TCHAR* const DifficultyNames[] = { TEXT("peaceful"), TEXT("easy"), TEXT("normal"), TEXT("hard") };
	Add(Difficulty, TEXT("difficulty"), Str(DifficultyNames[FMath::Clamp(Level.Difficulty, 0, 3)]));
	Add(Difficulty, TEXT("hardcore"), Int(0, EType::Byte));
	Add(Difficulty, TEXT("locked"), Int(0, EType::Byte));
	Add(Data, TEXT("difficulty_settings"), MoveTemp(Difficulty));
	// Packs, as in a real 26.3 level.dat: the vanilla one enabled, the experimental ones disabled.
	FMcNbt Packs = MakeTag(EType::Compound);
	FMcNbt Enabled = MakeTag(EType::List);
	Enabled.Values.Add(Str(TEXT("vanilla")));
	FMcNbt Disabled = MakeTag(EType::List);
	for (const TCHAR* Name : { TEXT("minecart_improvements"), TEXT("redstone_experiments"), TEXT("trade_rebalance") })
	{
		Disabled.Values.Add(Str(Name));
	}
	Add(Packs, TEXT("Enabled"), MoveTemp(Enabled));
	Add(Packs, TEXT("Disabled"), MoveTemp(Disabled));
	Add(Data, TEXT("DataPacks"), MoveTemp(Packs));
	FMcNbt Root = MakeTag(EType::Compound);
	Add(Root, TEXT("Data"), MoveTemp(Data));
	TArray<uint8> Raw, Zipped;
	Root.WriteRoot(Raw);
	if (!Compress(NAME_Gzip, Raw, Zipped))
	{
		return false;
	}
	Files.MakeDirectory(*OutFolder, true);
	if (!FFileHelper::SaveArrayToFile(Zipped, *(OutFolder / TEXT("level.dat"))))
	{
		return false;
	}
	// data/minecraft/world_gen_settings.dat: the seed and each dimension's generator. Without it the game stops with
	// "Overworld settings missing" while loading the level (observed). Shape as in real 26.3 saves: every dimension has a
	// type and a generator; ours are the noise generators with the standard presets.
	auto Dimension = [](const TCHAR* Type, const TCHAR* Settings, const TCHAR* Preset)
	{
		FMcNbt Generator = MakeTag(EType::Compound);
		Add(Generator, TEXT("settings"), Str(Settings));
		FMcNbt Biomes = MakeTag(EType::Compound);
		if (Preset)
		{
			Add(Biomes, TEXT("preset"), Str(Preset));
		}
		Add(Biomes, TEXT("type"), Str(Preset ? TEXT("minecraft:multi_noise") : TEXT("minecraft:the_end")));
		Add(Generator, TEXT("biome_source"), MoveTemp(Biomes));
		Add(Generator, TEXT("type"), Str(TEXT("minecraft:noise")));
		FMcNbt Entry = MakeTag(EType::Compound);
		Add(Entry, TEXT("generator"), MoveTemp(Generator));
		Add(Entry, TEXT("type"), Str(Type));
		return Entry;
	};
	FMcNbt Dimensions = MakeTag(EType::Compound);
	Add(Dimensions, TEXT("minecraft:overworld"), Dimension(TEXT("minecraft:overworld"), TEXT("minecraft:overworld"), TEXT("minecraft:overworld")));
	Add(Dimensions, TEXT("minecraft:the_nether"), Dimension(TEXT("minecraft:the_nether"), TEXT("minecraft:nether"), TEXT("minecraft:nether")));
	Add(Dimensions, TEXT("minecraft:the_end"), Dimension(TEXT("minecraft:the_end"), TEXT("minecraft:end"), nullptr));
	FMcNbt Settings = MakeTag(EType::Compound);
	Add(Settings, TEXT("bonus_chest"), Int(0, EType::Byte));
	Add(Settings, TEXT("seed"), Int(Level.Seed, EType::Long));
	Add(Settings, TEXT("generate_structures"), Int(1, EType::Byte));
	Add(Settings, TEXT("dimensions"), MoveTemp(Dimensions));
	FMcNbt GenRoot = MakeTag(EType::Compound);
	Add(GenRoot, TEXT("data"), MoveTemp(Settings));
	Add(GenRoot, TEXT("DataVersion"), Int(DataVersion));
	TArray<uint8> GenRaw, GenZipped;
	GenRoot.WriteRoot(GenRaw);
	if (!Compress(NAME_Gzip, GenRaw, GenZipped))
	{
		return false;
	}
	const FString DataFolder = OutFolder / TEXT("data") / TEXT("minecraft");
	Files.MakeDirectory(*DataFolder, true);
	return FFileHelper::SaveArrayToFile(GenZipped, *(DataFolder / TEXT("world_gen_settings.dat")));
}
