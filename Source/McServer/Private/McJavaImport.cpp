#include "McJavaImport.h"
#include "HAL/FileManager.h"
#include "McBiomeRegistry.h"
#include "McBlockEntity.h"
#include "McBlockRegistry.h"
#include "McChunk.h"
#include "McChunkSave.h"
#include "McDimensionType.h"
#include "McNbt.h"
#include "McServer.h"
#include "McWorldSave.h"
#include "McWorldStorage.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int32 SectorSize = 4096;
	constexpr int32 SectionVolume = McCoords::SectionSize * McCoords::SectionSize * McCoords::SectionSize;
	constexpr int32 MaxInflated = 256 * 1024 * 1024;
	constexpr uint8 ExternalFlag = 128;

	int32 ReadInt32(TConstArrayView<uint8> Bytes, int32 At)
	{
		return static_cast<int32>((uint32(Bytes[At]) << 24) | (uint32(Bytes[At + 1]) << 16) | (uint32(Bytes[At + 2]) << 8) | uint32(Bytes[At + 3]));
	}

	// The slots a container block has, by block name; 0 for blocks that hold nothing we keep.
	int32 GetContainerSlots(const FString& Id)
	{
		static const TMap<FString, int32> Slots = {
			{ TEXT("minecraft:chest"), 27 }, { TEXT("minecraft:trapped_chest"), 27 }, { TEXT("minecraft:barrel"), 27 },
			{ TEXT("minecraft:hopper"), 5 }, { TEXT("minecraft:dispenser"), 9 }, { TEXT("minecraft:dropper"), 9 },
		};
		if (const int32* Found = Slots.Find(Id))
		{
			return *Found;
		}
		return Id.EndsWith(TEXT("shulker_box")) ? 27 : 0;
	}

	bool GetPaletteEntry(const FMcNbt& Entry, FString& OutName, TMap<FString, FString>& OutProperties)
	{
		if (Entry.Type == FMcNbt::EType::String)
		{
			OutName = Entry.String;
			return true;
		}
		const FMcNbt* Name = Entry.Find(TEXT("Name"));
		if (!Name)
		{
			Name = Entry.Find(TEXT("id"));
		}
		if (!Name || Name->Type != FMcNbt::EType::String)
		{
			return false;
		}
		OutName = Name->String;
		const FMcNbt* Properties = Entry.Find(TEXT("Properties"));
		if (!Properties)
		{
			Properties = Entry.Find(TEXT("properties"));
		}
		if (Properties && Properties->Type == FMcNbt::EType::Compound)
		{
			for (int32 Index = 0; Index < Properties->Num(); ++Index)
			{
				const FMcNbt& Value = Properties->Values[Index];
				// Properties are written as text; a number is kept as its text in case a version writes one.
				OutProperties.Add(Properties->Keys[Index], Value.Type == FMcNbt::EType::String ? Value.String : FString::FromInt(static_cast<int32>(Value.Integer)));
			}
		}
		return true;
	}

	TArray<int64> GetLongs(const FMcNbt* Array)
	{
		TArray<int64> Longs;
		if (Array && Array->Type == FMcNbt::EType::LongArray)
		{
			for (const FMcNbt& Value : Array->Values)
			{
				Longs.Add(Value.Integer);
			}
		}
		return Longs;
	}

	const FMcNbt* FindList(const FMcNbt& Root, const TCHAR* Key)
	{
		const FMcNbt* List = Root.Find(Key);
		return List && List->Type == FMcNbt::EType::List ? List : nullptr;
	}

	FString DescribeEntry(const FString& Name, const TMap<FString, FString>& Properties)
	{
		FString Text = Name;
		bool bFirst = true;
		for (const TPair<FString, FString>& Pair : Properties)
		{
			Text += FString::Printf(TEXT("%s%s=%s"), bFirst ? TEXT("[") : TEXT(","), *Pair.Key, *Pair.Value);
			bFirst = false;
		}
		return bFirst ? Text : Text + TEXT("]");
	}

	// The folder that holds a vanilla dimension's region files, whichever layout the world has; empty if it has none.
	FString FindRegionFolder(const FString& Source, const FString& Dimension)
	{
		FString Namespace, Path;
		Dimension.Split(TEXT(":"), &Namespace, &Path);
		IFileManager& Files = IFileManager::Get();
		const FString Current = Source / TEXT("dimensions") / Namespace / Path / TEXT("region");
		if (Files.DirectoryExists(*Current))
		{
			return Current;
		}
		const FString Legacy = Dimension == TEXT("minecraft:overworld") ? Source / TEXT("region")
			: Dimension == TEXT("minecraft:the_nether") ? Source / TEXT("DIM-1") / TEXT("region")
			: Dimension == TEXT("minecraft:the_end") ? Source / TEXT("DIM1") / TEXT("region") : FString();
		return !Legacy.IsEmpty() && Files.DirectoryExists(*Legacy) ? Legacy : FString();
	}

	void AddBlockEntities(const FMcNbt& Root, FMcChunk& Chunk, McJavaImport::FStats& Stats)
	{
		const FMcNbt* List = FindList(Root, TEXT("block_entities"));
		if (!List)
		{
			return;
		}
		for (const FMcNbt& Entity : List->Values)
		{
			const FString Id = Entity.GetString(TEXT("id"));
			const int32 X = static_cast<int32>(Entity.GetInt(TEXT("x"))) & 15;
			const int32 Z = static_cast<int32>(Entity.GetInt(TEXT("z"))) & 15;
			const int32 Y = static_cast<int32>(Entity.GetInt(TEXT("y")));
			if (!Chunk.IsInHeight(Y))
			{
				continue;
			}
			if (const int32 NumSlots = GetContainerSlots(Id))
			{
				FMcBlockEntity Container = FMcBlockEntity::MakeContainer(NumSlots);
				if (const FMcNbt* Items = FindList(Entity, TEXT("Items")))
				{
					for (const FMcNbt& Item : Items->Values)
					{
						const int32 Slot = static_cast<int32>(Item.GetInt(TEXT("Slot"))) & 0xff;
						const FString ItemId = Item.GetString(TEXT("id"));
						const int32 Count = static_cast<int32>(Item.GetInt(TEXT("count"), Item.GetInt(TEXT("Count"), 1)));
						if (Slot < NumSlots && !ItemId.IsEmpty() && Count > 0)
						{
							Container.Slots[Slot] = FMcItemStack(FName(*ItemId), Count);
						}
					}
				}
				const FString Loot = Entity.GetString(TEXT("LootTable"));
				if (!Loot.IsEmpty())
				{
					Container.LootTable = FName(*Loot);
					Container.LootSeed = Entity.GetInt(TEXT("LootTableSeed"));
				}
				Chunk.SetBlockEntity(X, Y, Z, MoveTemp(Container));
				++Stats.BlockEntities;
			}
			else if (Id == TEXT("minecraft:mob_spawner"))
			{
				FString Mob = TEXT("minecraft:pig");
				if (const FMcNbt* Data = Entity.Find(TEXT("SpawnData")))
				{
					if (const FMcNbt* Inner = Data->Find(TEXT("entity")))
					{
						const FString Found = Inner->GetString(TEXT("id"));
						if (!Found.IsEmpty())
						{
							Mob = Found;
						}
					}
				}
				Chunk.SetBlockEntity(X, Y, Z, FMcBlockEntity::MakeSpawner(FName(*Mob)));
				++Stats.BlockEntities;
			}
			else
			{
				++Stats.DroppedBlockEntities.FindOrAdd(Id);
			}
		}
	}
}

FName McJavaImport::GetPlaceholderBlock()
{
	return TEXT("minecraft:magenta_concrete");
}

bool McJavaImport::Decompress(uint8 Compression, TConstArrayView<uint8> Data, TArray<uint8>& Out)
{
	Out.Reset();
	if (Compression == 3)
	{
		Out = Data;
		return true;
	}
	if (Compression != 1 && Compression != 2)
	{
		return false;
	}
	if (Compression == 1)
	{
		// A gzip stream ends with the size it inflates to, little endian.
		if (Data.Num() < 18)
		{
			return false;
		}
		const int32 Size = Data[Data.Num() - 4] | (Data[Data.Num() - 3] << 8) | (Data[Data.Num() - 2] << 16) | (Data[Data.Num() - 1] << 24);
		if (Size <= 0 || Size > MaxInflated)
		{
			return false;
		}
		Out.SetNumUninitialized(Size);
		return FCompression::UncompressMemory(NAME_Gzip, Out.GetData(), Size, Data.GetData(), Data.Num());
	}
	// Zlib does not say its size: try a guess and double it until the stream fits. The NBT reader stops at the end of the
	// root compound, so a tail of unused bytes after it does no harm.
	for (int64 Size = FMath::Max<int64>(Data.Num() * 8, 64 * 1024); Size <= MaxInflated; Size *= 2)
	{
		Out.SetNumUninitialized(static_cast<int32>(Size));
		if (FCompression::UncompressMemory(NAME_Zlib, Out.GetData(), static_cast<int32>(Size), Data.GetData(), Data.Num()))
		{
			return true;
		}
	}
	Out.Reset();
	return false;
}

bool McJavaImport::ReadChunkBytes(TConstArrayView<uint8> RegionFile, int32 LocalX, int32 LocalZ, const FString& RegionFolder,
	int32 ChunkX, int32 ChunkZ, TArray<uint8>& OutNbt, bool& bOutMissing)
{
	bOutMissing = true;
	if (RegionFile.Num() < 2 * SectorSize || LocalX < 0 || LocalX > 31 || LocalZ < 0 || LocalZ > 31)
	{
		return false;
	}
	const int32 Entry = ReadInt32(RegionFile, 4 * (LocalX + 32 * LocalZ));
	const int64 Offset = static_cast<int64>((Entry >> 8) & 0xffffff) * SectorSize;
	const int32 Sectors = Entry & 0xff;
	if (Entry == 0 || Sectors == 0)
	{
		return false;
	}
	bOutMissing = false;
	if (Offset < 2 * SectorSize || Offset + 5 > RegionFile.Num())
	{
		return false;
	}
	const int32 Length = ReadInt32(RegionFile, static_cast<int32>(Offset));
	uint8 Compression = RegionFile[static_cast<int32>(Offset) + 4];
	if (Length < 1 || Offset + 4 + Length > RegionFile.Num())
	{
		return false;
	}
	if (Compression & ExternalFlag)
	{
		Compression &= ~ExternalFlag;
		TArray<uint8> External;
		if (!FFileHelper::LoadFileToArray(External, *(RegionFolder / FString::Printf(TEXT("c.%d.%d.mcc"), ChunkX, ChunkZ))))
		{
			return false;
		}
		return Decompress(Compression, External, OutNbt);
	}
	return Decompress(Compression, MakeArrayView(RegionFile.GetData() + Offset + 5, Length - 1), OutNbt);
}

int32 McJavaImport::GetBitsPerEntry(int32 PaletteSize, int32 MinBits)
{
	return FMath::Max(MinBits, static_cast<int32>(FMath::CeilLogTwo(static_cast<uint32>(FMath::Max(PaletteSize, 1)))));
}

bool McJavaImport::UnpackIndices(TConstArrayView<int64> Longs, int32 Bits, int32 Count, TArray<int32>& Out)
{
	Out.Reset();
	if (Bits < 1 || Bits > 32 || Count < 0)
	{
		return false;
	}
	const int32 PerLong = 64 / Bits;
	if (Longs.Num() < (Count + PerLong - 1) / PerLong)
	{
		return false;
	}
	const uint64 Mask = (uint64(1) << Bits) - 1;
	Out.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const uint64 Word = static_cast<uint64>(Longs[Index / PerLong]);
		Out[Index] = static_cast<int32>((Word >> ((Index % PerLong) * Bits)) & Mask);
	}
	return true;
}

FMcStateId McJavaImport::MapState(const FString& Name, const TMap<FString, FString>& Properties, FStats& Stats, int64 Uses)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FName Block(*(Name.Contains(TEXT(":")) ? Name : TEXT("minecraft:") + Name));
	if (Registry.FindBlock(Block))
	{
		TMap<FName, FName> Wanted;
		for (const TPair<FString, FString>& Pair : Properties)
		{
			Wanted.Add(FName(*Pair.Key), FName(*Pair.Value));
		}
		if (const TOptional<FMcStateId> State = Registry.FindState(Block, Wanted))
		{
			// Properties the block does not have (or values it does not allow) are left out by FindState, so check that
			// every property asked for was kept; if not it still is the block, which beats the placeholder.
			for (const TPair<FName, FName>& Pair : Wanted)
			{
				if (Registry.GetPropertyValue(*State, Pair.Key) != Pair.Value)
				{
					Stats.UnknownStates.FindOrAdd(DescribeEntry(Name, Properties) + TEXT(" (properties differ)")) += Uses;
					break;
				}
			}
			return *State;
		}
		if (const TOptional<FMcStateId> Default = Registry.FindDefaultState(Block))
		{
			Stats.UnknownStates.FindOrAdd(DescribeEntry(Name, Properties) + TEXT(" (properties differ)")) += Uses;
			return *Default;
		}
	}
	Stats.UnknownStates.FindOrAdd(DescribeEntry(Name, Properties)) += Uses;
	return Registry.FindDefaultState(GetPlaceholderBlock()).Get(0);
}

bool McJavaImport::GetChunkPos(const FMcNbt& Root, FIntPoint& OutPos)
{
	const FMcNbt* X = Root.Find(TEXT("xPos"));
	const FMcNbt* Z = Root.Find(TEXT("zPos"));
	if (!X || !Z || !X->IsNumber() || !Z->IsNumber())
	{
		return false;
	}
	OutPos = FIntPoint(static_cast<int32>(X->Integer), static_cast<int32>(Z->Integer));
	return true;
}

bool McJavaImport::ConvertChunk(const FMcNbt& Root, FMcChunk& Chunk, FStats& Stats)
{
	const FMcNbt* Sections = FindList(Root, TEXT("sections"));
	if (!Sections)
	{
		return false;
	}
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	TArray<FMcStateId> States;
	States.SetNumUninitialized(SectionVolume);
	TArray<FMcBiomeId> BiomeCells;
	BiomeCells.Init(Biomes.GetPlains(), 64 * Chunk.NumSections());
	for (const FMcNbt& Section : Sections->Values)
	{
		const int32 Index = static_cast<int32>(Section.GetInt(TEXT("Y"))) - Chunk.GetMinY() / McCoords::SectionSize;
		if (Index < 0 || Index >= Chunk.NumSections())
		{
			continue;
		}
		if (const FMcNbt* BlockStates = Section.Find(TEXT("block_states")))
		{
			const FMcNbt* Palette = FindList(*BlockStates, TEXT("palette"));
			if (Palette && Palette->Num() > 0)
			{
				TArray<int32> Indexes;
				if (Palette->Num() > 1 && !UnpackIndices(GetLongs(BlockStates->Find(TEXT("data"))), GetBitsPerEntry(Palette->Num(), 4), SectionVolume, Indexes))
				{
					continue;
				}
				// How often each entry is used, so unknown ones are counted in blocks and not in sections.
				TArray<int64> Uses;
				Uses.Init(0, Palette->Num());
				if (Palette->Num() == 1)
				{
					Uses[0] = SectionVolume;
				}
				else
				{
					for (int32 Entry : Indexes)
					{
						if (Entry < Palette->Num())
						{
							++Uses[Entry];
						}
					}
				}
				TArray<FMcStateId> Mapped;
				for (int32 Entry = 0; Entry < Palette->Num(); ++Entry)
				{
					FString Name;
					TMap<FString, FString> Properties;
					if (!GetPaletteEntry(Palette->Values[Entry], Name, Properties))
					{
						Name = TEXT("minecraft:air");
					}
					Mapped.Add(MapState(Name, Properties, Stats, Uses[Entry]));
				}
				for (int32 Block = 0; Block < SectionVolume; ++Block)
				{
					const int32 Entry = Palette->Num() == 1 ? 0 : Indexes[Block];
					States[Block] = Mapped.IsValidIndex(Entry) ? Mapped[Entry] : Mapped[0];
				}
				Chunk.LoadSection(Index, States);
				Stats.Blocks += SectionVolume;
			}
		}
		if (const FMcNbt* BiomeTag = Section.Find(TEXT("biomes")))
		{
			const FMcNbt* Palette = FindList(*BiomeTag, TEXT("palette"));
			if (!Palette || Palette->Num() == 0)
			{
				continue;
			}
			TArray<FMcBiomeId> Mapped;
			for (const FMcNbt& Entry : Palette->Values)
			{
				FString Name;
				TMap<FString, FString> Unused;
				GetPaletteEntry(Entry, Name, Unused);
				Mapped.Add(Biomes.Find(FName(*Name)).Get(Biomes.GetPlains()));
			}
			TArray<int32> Indexes;
			if (Palette->Num() > 1 && !UnpackIndices(GetLongs(BiomeTag->Find(TEXT("data"))), GetBitsPerEntry(Palette->Num(), 1), 64, Indexes))
			{
				continue;
			}
			for (int32 Cell = 0; Cell < 64; ++Cell)
			{
				const int32 Entry = Palette->Num() == 1 ? 0 : Indexes[Cell];
				BiomeCells[Index * 64 + Cell] = Mapped.IsValidIndex(Entry) ? Mapped[Entry] : Mapped[0];
			}
		}
	}
	Chunk.LoadBiomes(BiomeCells);
	AddBlockEntities(Root, Chunk, Stats);
	return true;
}

bool McJavaImport::IsJavaWorld(const FString& Folder)
{
	return IFileManager::Get().FileExists(*(Folder / TEXT("level.dat")));
}

bool McJavaImport::ReadLevel(const FString& Folder, FMcLevelData& OutLevel)
{
	FMcNbt Level;
	if (!FMcNbt::LoadFile(Folder / TEXT("level.dat"), Level))
	{
		return false;
	}
	const FMcNbt* Data = Level.Find(TEXT("Data"));
	if (!Data)
	{
		return false;
	}
	OutLevel.Name = Data->GetString(TEXT("LevelName"));
	OutLevel.GameMode = Data->GetInt(TEXT("GameType")) == 1 ? TEXT("creative") : TEXT("survival");
	OutLevel.DayTime = static_cast<double>(Data->GetInt(TEXT("DayTime"), Data->GetInt(TEXT("Time"))));
	// The spawn: a `spawn` compound with a `pos` int array, or the older SpawnX, SpawnY and SpawnZ.
	if (const FMcNbt* Spawn = Data->Find(TEXT("spawn")))
	{
		const FMcNbt* Pos = Spawn->Find(TEXT("pos"));
		if (Pos && Pos->Type == FMcNbt::EType::IntArray && Pos->Num() >= 3)
		{
			OutLevel.Spawn = FIntVector(static_cast<int32>(Pos->Values[0].Integer), static_cast<int32>(Pos->Values[1].Integer), static_cast<int32>(Pos->Values[2].Integer));
		}
		const FString Dimension = Spawn->GetString(TEXT("dimension"));
		if (!Dimension.IsEmpty())
		{
			OutLevel.Dimension = Dimension;
		}
	}
	else
	{
		OutLevel.Spawn = FIntVector(static_cast<int32>(Data->GetInt(TEXT("SpawnX"))), static_cast<int32>(Data->GetInt(TEXT("SpawnY"))), static_cast<int32>(Data->GetInt(TEXT("SpawnZ"))));
	}
	if (const FMcNbt* Difficulty = Data->Find(TEXT("difficulty_settings")))
	{
		static const TCHAR* Names[] = { TEXT("peaceful"), TEXT("easy"), TEXT("normal"), TEXT("hard") };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Difficulty->GetString(TEXT("difficulty")) == Names[Index])
			{
				OutLevel.Difficulty = Index;
			}
		}
	}
	else if (Data->Find(TEXT("Difficulty")))
	{
		OutLevel.Difficulty = static_cast<int32>(Data->GetInt(TEXT("Difficulty"), 2));
	}
	// The seed: world_gen_settings.dat in the current layout, in level.dat before.
	FMcNbt Settings;
	if (FMcNbt::LoadFile(Folder / TEXT("data") / TEXT("minecraft") / TEXT("world_gen_settings.dat"), Settings))
	{
		if (const FMcNbt* Inner = Settings.Find(TEXT("data")))
		{
			OutLevel.Seed = Inner->GetInt(TEXT("seed"));
		}
	}
	else if (const FMcNbt* Old = Data->Find(TEXT("WorldGenSettings")))
	{
		OutLevel.Seed = Old->GetInt(TEXT("seed"));
	}
	else
	{
		OutLevel.Seed = Data->GetInt(TEXT("RandomSeed"));
	}
	FMcNbt Rules;
	if (FMcNbt::LoadFile(Folder / TEXT("data") / TEXT("minecraft") / TEXT("game_rules.dat"), Rules))
	{
		if (const FMcNbt* Inner = Rules.Find(TEXT("data")))
		{
			for (int32 Index = 0; Index < Inner->Num(); ++Index)
			{
				const FMcNbt& Value = Inner->Values[Index];
				FString Key = Inner->Keys[Index];
				Key.RemoveFromStart(TEXT("minecraft:"));
				OutLevel.GameRules.Add(Key, Value.Type == FMcNbt::EType::String ? Value.String
					: Value.Type == FMcNbt::EType::Byte ? (Value.Integer ? TEXT("true") : TEXT("false")) : FString::Printf(TEXT("%lld"), Value.Integer));
			}
		}
	}
	return true;
}

FString McJavaImport::ImportWorld(const FString& Source, const FString& Name, FStats& OutStats)
{
	FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	if (!Blocks.EnsureLoaded() || !Biomes.EnsureLoaded() || !IsJavaWorld(Source))
	{
		UE_LOG(LogMcServer, Error, TEXT("Cannot import '%s': the registries did not load or it has no level.dat."), *Source);
		return FString();
	}
	FMcLevelData Level;
	if (!ReadLevel(Source, Level))
	{
		UE_LOG(LogMcServer, Error, TEXT("Cannot import '%s': level.dat is unreadable."), *Source);
		return FString();
	}
	if (!Name.IsEmpty())
	{
		Level.Name = Name;
	}
	if (Level.Name.IsEmpty())
	{
		Level.Name = FPaths::GetCleanFilename(Source);
	}
	const FString Folder = McWorldSave::MakeNewFolder(Level.Name);
	const FString WorldDir = McWorldSave::GetWorldDir(Folder);
	IFileManager& Files = IFileManager::Get();
	for (const TCHAR* Dimension : { TEXT("minecraft:overworld"), TEXT("minecraft:the_nether"), TEXT("minecraft:the_end") })
	{
		const FString RegionFolder = FindRegionFolder(Source, Dimension);
		FMcDimensionType Type;
		if (RegionFolder.IsEmpty() || !FMcDimensionType::Load(Dimension, Type))
		{
			continue;
		}
		FMcWorldStorage Storage(WorldDir / McWorldSave::GetDimensionFolder(Dimension));
		TArray<FString> Names;
		Files.FindFiles(Names, *(RegionFolder / TEXT("*.mca")), true, false);
		for (const FString& File : Names)
		{
			// r.<x>.<z>.mca
			TArray<FString> Parts;
			File.ParseIntoArray(Parts, TEXT("."));
			if (Parts.Num() != 4 || Parts[0] != TEXT("r"))
			{
				continue;
			}
			const int32 RegionX = FCString::Atoi(*Parts[1]);
			const int32 RegionZ = FCString::Atoi(*Parts[2]);
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *(RegionFolder / File)))
			{
				continue;
			}
			++OutStats.Regions;
			for (int32 LocalZ = 0; LocalZ < 32; ++LocalZ)
			{
				for (int32 LocalX = 0; LocalX < 32; ++LocalX)
				{
					const FIntPoint Pos(RegionX * 32 + LocalX, RegionZ * 32 + LocalZ);
					TArray<uint8> Raw;
					bool bMissing = false;
					if (!ReadChunkBytes(Bytes, LocalX, LocalZ, RegionFolder, Pos.X, Pos.Y, Raw, bMissing))
					{
						OutStats.ChunksFailed += bMissing ? 0 : 1;
						continue;
					}
					FMcNbt Root;
					FMcChunk Chunk(Pos, Type.MinY, Type.NumSections());
					Chunk.FillBiome(Biomes.GetPlains());
					if (!FMcNbt::Parse(Raw, Root) || !ConvertChunk(Root, Chunk, OutStats))
					{
						++OutStats.ChunksFailed;
						continue;
					}
					FMcChunkSaveExtras Extras;
					Extras.bPopulated = true;
					Storage.QueueSave(Pos, McChunkSave::Encode(Chunk, Extras));
					++OutStats.Chunks;
				}
			}
		}
		Storage.Flush();
	}
	if (!McWorldSave::SaveLevel(WorldDir, Level))
	{
		UE_LOG(LogMcServer, Error, TEXT("Could not write %s."), *WorldDir);
		return FString();
	}
	UE_LOG(LogMcServer, Log, TEXT("Imported '%s' as '%s': %s"), *Source, *Folder, *Describe(OutStats));
	return Folder;
}

FString McJavaImport::Describe(const FStats& Stats)
{
	FString Out = FString::Printf(TEXT("%d regions, %d chunks (%d unreadable), %lld blocks, %d block entities kept"),
		Stats.Regions, Stats.Chunks, Stats.ChunksFailed, Stats.Blocks, Stats.BlockEntities);
	for (const TPair<FString, int64>& Unknown : Stats.UnknownStates)
	{
		Out += FString::Printf(TEXT("; unknown %s x%lld"), *Unknown.Key, Unknown.Value);
	}
	for (const TPair<FString, int32>& Dropped : Stats.DroppedBlockEntities)
	{
		Out += FString::Printf(TEXT("; dropped %s x%d"), *Dropped.Key, Dropped.Value);
	}
	return Out;
}
