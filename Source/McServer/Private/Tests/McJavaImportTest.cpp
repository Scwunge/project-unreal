// The Java world importer: palette unpacking, the region file reader, the block mapping and a chunk in NBT bytes.

#include "McBiomeRegistry.h"
#include "McBlockEntity.h"
#include "McBlockRegistry.h"
#include "McChunk.h"
#include "McJavaImport.h"
#include "McNbt.h"
#include "McWorldSave.h"
#include "Misc/AutomationTest.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// A little NBT writer, big endian, enough for a chunk.
	struct FNbtWriter
	{
		TArray<uint8> Bytes;

		void Big(uint64 Value, int32 Size)
		{
			for (int32 Shift = (Size - 1) * 8; Shift >= 0; Shift -= 8)
			{
				Bytes.Add(static_cast<uint8>(Value >> Shift));
			}
		}
		void String(const FString& Text)
		{
			const FTCHARToUTF8 Utf8(*Text);
			Big(Utf8.Length(), 2);
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		void Tag(uint8 Type, const FString& Name)
		{
			Big(Type, 1);
			String(Name);
		}
		void Int(const FString& Name, int32 Value) { Tag(3, Name); Big(static_cast<uint32>(Value), 4); }
		void Str(const FString& Name, const FString& Value) { Tag(8, Name); String(Value); }
		void Byte(const FString& Name, int32 Value) { Tag(1, Name); Big(static_cast<uint8>(Value), 1); }
		void Compound(const FString& Name) { Tag(10, Name); }
		void End() { Big(0, 1); }
		void List(const FString& Name, uint8 ElementType, int32 Count) { Tag(9, Name); Big(ElementType, 1); Big(static_cast<uint32>(Count), 4); }
		void Longs(const FString& Name, const TArray<int64>& Values)
		{
			Tag(12, Name);
			Big(static_cast<uint32>(Values.Num()), 4);
			for (int64 Value : Values)
			{
				Big(static_cast<uint64>(Value), 8);
			}
		}
	};

	// Packs indexes as the game does: whole entries only, a new long when the next does not fit.
	TArray<int64> Pack(const TArray<int32>& Indexes, int32 Bits)
	{
		const int32 PerLong = 64 / Bits;
		TArray<int64> Longs;
		Longs.SetNumZeroed((Indexes.Num() + PerLong - 1) / PerLong);
		for (int32 Index = 0; Index < Indexes.Num(); ++Index)
		{
			Longs[Index / PerLong] |= static_cast<int64>(static_cast<uint64>(Indexes[Index]) << ((Index % PerLong) * Bits));
		}
		return Longs;
	}

	bool LoadRegistries()
	{
		return FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportUnpackTest, "McClient.McServer.JavaImport.Unpack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportUnpackTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("2 entries need the minimum of 4 bits"), McJavaImport::GetBitsPerEntry(2, 4), 4);
	TestEqual(TEXT("16 entries are 4 bits"), McJavaImport::GetBitsPerEntry(16, 4), 4);
	TestEqual(TEXT("17 entries are 5 bits"), McJavaImport::GetBitsPerEntry(17, 4), 5);
	TestEqual(TEXT("biomes have no floor above 1"), McJavaImport::GetBitsPerEntry(3, 1), 2);
	TestEqual(TEXT("4097 entries are 13 bits"), McJavaImport::GetBitsPerEntry(4097, 4), 13);

	// 5 bits: 12 entries to a long, 4 bits left over that are not used.
	TArray<int32> Indexes;
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		Indexes.Add((Index * 7 + 3) % 20);
	}
	const TArray<int64> Longs = Pack(Indexes, 5);
	TestEqual(TEXT("342 longs for 4096 entries at 5 bits"), Longs.Num(), 342);
	TArray<int32> Out;
	TestTrue(TEXT("unpacks"), McJavaImport::UnpackIndices(Longs, 5, 4096, Out));
	TestTrue(TEXT("round trip"), Out == Indexes);
	// Entry 12 starts the second long at bit 0 rather than straddling the first.
	TestEqual(TEXT("entry 12 is at bit 0 of long 1"), static_cast<int32>(static_cast<uint64>(Longs[1]) & 31), Indexes[12]);
	TestFalse(TEXT("too few longs fail"), McJavaImport::UnpackIndices(MakeArrayView(Longs.GetData(), 10), 5, 4096, Out));

	// A high bit set in the long (a negative value) still reads as the top entry.
	const TArray<int64> Top = { static_cast<int64>(uint64(0xf) << 60) };
	TestTrue(TEXT("unpacks 4 bits"), McJavaImport::UnpackIndices(Top, 4, 16, Out));
	TestEqual(TEXT("top entry of a negative long"), Out[15], 15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportRegionTest, "McClient.McServer.JavaImport.Region",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportRegionTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Payload = { 10, 0, 0, 3, 0, 1, 'a', 0, 0, 0, 7, 0 };
	TArray<uint8> Zlib;
	Zlib.SetNumUninitialized(FCompression::CompressMemoryBound(NAME_Zlib, Payload.Num()));
	int32 ZlibSize = Zlib.Num();
	TestTrue(TEXT("compress"), FCompression::CompressMemory(NAME_Zlib, Zlib.GetData(), ZlibSize, Payload.GetData(), Payload.Num()));
	Zlib.SetNum(ZlibSize);

	// A region file with the zlib chunk at (3, 2) in sector 2, an uncompressed one at (0, 0) in sector 3, and one at (1, 0) that points at an external file.
	TArray<uint8> File;
	File.SetNumZeroed(4 * 4096);
	const auto SetEntry = [&](int32 X, int32 Z, int32 Sector, int32 Count)
	{
		const int32 At = 4 * (X + 32 * Z);
		File[At] = static_cast<uint8>(Sector >> 16);
		File[At + 1] = static_cast<uint8>(Sector >> 8);
		File[At + 2] = static_cast<uint8>(Sector);
		File[At + 3] = static_cast<uint8>(Count);
	};
	const auto SetChunk = [&](int32 Sector, uint8 Compression, const TArray<uint8>& Data)
	{
		const int32 At = Sector * 4096;
		const int32 Length = Data.Num() + 1;
		File[At] = static_cast<uint8>(Length >> 24);
		File[At + 1] = static_cast<uint8>(Length >> 16);
		File[At + 2] = static_cast<uint8>(Length >> 8);
		File[At + 3] = static_cast<uint8>(Length);
		File[At + 4] = Compression;
		FMemory::Memcpy(&File[At + 5], Data.GetData(), Data.Num());
	};
	SetEntry(3, 2, 2, 1);
	SetChunk(2, 2, Zlib);
	SetEntry(0, 0, 3, 1);
	SetChunk(3, 3, Payload);
	SetEntry(1, 0, 0, 0);

	TArray<uint8> Out;
	bool bMissing = false;
	TestTrue(TEXT("zlib chunk reads"), McJavaImport::ReadChunkBytes(File, 3, 2, FString(), 3, 2, Out, bMissing));
	TestTrue(TEXT("zlib chunk inflates to the payload"), Out.Num() >= Payload.Num() && FMemory::Memcmp(Out.GetData(), Payload.GetData(), Payload.Num()) == 0);
	TestTrue(TEXT("uncompressed chunk reads"), McJavaImport::ReadChunkBytes(File, 0, 0, FString(), 0, 0, Out, bMissing));
	TestTrue(TEXT("uncompressed chunk is the payload"), Out == Payload);
	TestFalse(TEXT("an empty slot is not a chunk"), McJavaImport::ReadChunkBytes(File, 1, 0, FString(), 1, 0, Out, bMissing));
	TestTrue(TEXT("an empty slot is reported missing"), bMissing);

	// A chunk marked external reads c.<x>.<z>.mcc from the region folder.
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("JavaImportRegion");
	FFileHelper::SaveArrayToFile(Zlib, *(Folder / TEXT("c.5.4.mcc")));
	SetEntry(5, 4, 2, 1);
	SetChunk(2, 2 | 128, TArray<uint8>());
	TestTrue(TEXT("external chunk reads"), McJavaImport::ReadChunkBytes(File, 5, 4, Folder, 5, 4, Out, bMissing));
	TestTrue(TEXT("external chunk is the payload"), Out.Num() >= Payload.Num() && FMemory::Memcmp(Out.GetData(), Payload.GetData(), Payload.Num()) == 0);
	IFileManager::Get().DeleteDirectory(*Folder, false, true);

	// LZ4 and custom compression are refused, not misread.
	TestFalse(TEXT("LZ4 is not read"), McJavaImport::Decompress(4, Payload, Out));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportMapTest, "McClient.McServer.JavaImport.Mapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportMapTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	McJavaImport::FStats Stats;
	const FMcStateId Stone = McJavaImport::MapState(TEXT("minecraft:stone"), {}, Stats, 10);
	TestEqual(TEXT("stone"), Registry.GetBlockOf(Stone).Name, FName(TEXT("minecraft:stone")));
	const FMcStateId Stairs = McJavaImport::MapState(TEXT("minecraft:oak_stairs"),
		{ { TEXT("facing"), TEXT("east") }, { TEXT("half"), TEXT("top") }, { TEXT("shape"), TEXT("straight") }, { TEXT("waterlogged"), TEXT("false") } }, Stats, 5);
	TestEqual(TEXT("stairs facing"), Registry.GetPropertyValue(Stairs, TEXT("facing")), FName(TEXT("east")));
	TestEqual(TEXT("stairs half"), Registry.GetPropertyValue(Stairs, TEXT("half")), FName(TEXT("top")));
	TestEqual(TEXT("nothing unknown yet"), Stats.UnknownStates.Num(), 0);
	// A name without the namespace is minecraft's.
	TestEqual(TEXT("bare name"), Registry.GetBlockOf(McJavaImport::MapState(TEXT("dirt"), {}, Stats, 1)).Name, FName(TEXT("minecraft:dirt")));

	const FMcStateId Unknown = McJavaImport::MapState(TEXT("somemod:widget"), { { TEXT("size"), TEXT("3") } }, Stats, 7);
	TestEqual(TEXT("an unknown block is the placeholder"), Registry.GetBlockOf(Unknown).Name, McJavaImport::GetPlaceholderBlock());
	TestEqual(TEXT("it is counted by blocks"), Stats.UnknownStates.FindRef(TEXT("somemod:widget[size=3]")), int64(7));

	// A known block with a property it has not got keeps its look and is counted.
	const FMcStateId Odd = McJavaImport::MapState(TEXT("minecraft:oak_stairs"), { { TEXT("facing"), TEXT("east") }, { TEXT("bogus"), TEXT("yes") } }, Stats, 2);
	TestEqual(TEXT("still stairs"), Registry.GetBlockOf(Odd).Name, FName(TEXT("minecraft:oak_stairs")));
	TestEqual(TEXT("still facing east"), Registry.GetPropertyValue(Odd, TEXT("facing")), FName(TEXT("east")));
	TestEqual(TEXT("two kinds unknown now"), Stats.UnknownStates.Num(), 2);

	// Values that are not the first of their property (low and tall, small and large, 1) are kept as written.
	McJavaImport::FStats Exact;
	const FMcStateId Wall = McJavaImport::MapState(TEXT("minecraft:cobblestone_wall"),
		{ { TEXT("east"), TEXT("low") }, { TEXT("north"), TEXT("tall") }, { TEXT("south"), TEXT("none") }, { TEXT("west"), TEXT("none") }, { TEXT("up"), TEXT("true") }, { TEXT("waterlogged"), TEXT("false") } }, Exact, 1);
	TestEqual(TEXT("wall east"), Registry.GetPropertyValue(Wall, TEXT("east")), FName(TEXT("low")));
	TestEqual(TEXT("wall north"), Registry.GetPropertyValue(Wall, TEXT("north")), FName(TEXT("tall")));
	const FMcStateId Bamboo = McJavaImport::MapState(TEXT("minecraft:bamboo"), { { TEXT("age"), TEXT("1") }, { TEXT("leaves"), TEXT("small") }, { TEXT("stage"), TEXT("0") } }, Exact, 1);
	TestEqual(TEXT("bamboo leaves"), Registry.GetPropertyValue(Bamboo, TEXT("leaves")), FName(TEXT("small")));
	TestEqual(TEXT("bamboo age"), Registry.GetPropertyValue(Bamboo, TEXT("age")), FName(TEXT("1")));
	TestEqual(TEXT("these are all known"), Exact.UnknownStates.Num(), 0);
	if (Exact.UnknownStates.Num() > 0)
	{
		AddInfo(Registry.Describe(Wall) + TEXT(" / ") + Registry.Describe(Bamboo));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportChunkTest, "McClient.McServer.JavaImport.Chunk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportChunkTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();

	// Section Y=-4 (the bottom): stone, with a cobblestone at (1, 2, 3) and an unknown block at (15, 15, 15), 4 bits;
	// section Y=-3: all one block (no data); biomes of section -4: two palette entries, 1 bit.
	TArray<int32> Blocks;
	Blocks.Init(0, 4096);
	Blocks[(2 << 8) | (3 << 4) | 1] = 1;
	Blocks[4095] = 2;
	TArray<int32> Cells;
	Cells.Init(0, 64);
	Cells[63] = 1;

	FNbtWriter Writer;
	Writer.Compound(TEXT(""));
	Writer.Int(TEXT("DataVersion"), 5023);
	Writer.Int(TEXT("xPos"), -2);
	Writer.Int(TEXT("zPos"), 7);
	Writer.Int(TEXT("yPos"), -4);
	Writer.List(TEXT("sections"), 10, 2);
	{
		Writer.Byte(TEXT("Y"), -4);
		Writer.Compound(TEXT("block_states"));
		Writer.List(TEXT("palette"), 10, 3);
		Writer.Str(TEXT("Name"), TEXT("minecraft:stone")); Writer.End();
		Writer.Str(TEXT("Name"), TEXT("minecraft:cobblestone")); Writer.End();
		Writer.Str(TEXT("Name"), TEXT("somemod:widget")); Writer.End();
		Writer.Longs(TEXT("data"), Pack(Blocks, 4));
		Writer.End();
		Writer.Compound(TEXT("biomes"));
		Writer.List(TEXT("palette"), 8, 2);
		Writer.String(TEXT("minecraft:plains"));
		Writer.String(TEXT("minecraft:desert"));
		Writer.Longs(TEXT("data"), Pack(Cells, 1));
		Writer.End();
		Writer.End();

		Writer.Byte(TEXT("Y"), -3);
		Writer.Compound(TEXT("block_states"));
		Writer.List(TEXT("palette"), 10, 1);
		Writer.Str(TEXT("Name"), TEXT("minecraft:oak_stairs"));
		Writer.Compound(TEXT("Properties"));
		Writer.Str(TEXT("facing"), TEXT("south"));
		Writer.Str(TEXT("half"), TEXT("bottom"));
		Writer.End();
		Writer.End();
		Writer.End();
		Writer.End();
	}
	Writer.List(TEXT("block_entities"), 10, 1);
	{
		Writer.Str(TEXT("id"), TEXT("minecraft:chest"));
		Writer.Int(TEXT("x"), -2 * 16 + 4);
		Writer.Int(TEXT("y"), -62);
		Writer.Int(TEXT("z"), 7 * 16 + 5);
		Writer.List(TEXT("Items"), 10, 1);
		Writer.Byte(TEXT("Slot"), 13);
		Writer.Str(TEXT("id"), TEXT("minecraft:diamond"));
		Writer.Int(TEXT("count"), 5);
		Writer.End();
		Writer.End();
	}
	Writer.End();

	FMcNbt Root;
	if (!TestTrue(TEXT("the NBT parses"), FMcNbt::Parse(Writer.Bytes, Root)))
	{
		return false;
	}
	FIntPoint Pos;
	TestTrue(TEXT("chunk position"), McJavaImport::GetChunkPos(Root, Pos));
	TestTrue(TEXT("chunk position value"), Pos == FIntPoint(-2, 7));

	FMcChunk Chunk(Pos, -64, 24);
	McJavaImport::FStats Stats;
	TestTrue(TEXT("converts"), McJavaImport::ConvertChunk(Root, Chunk, Stats));
	TestEqual(TEXT("stone at the origin"), Registry.GetBlockOf(Chunk.GetBlock(0, -64, 0)).Name, FName(TEXT("minecraft:stone")));
	TestEqual(TEXT("cobblestone where it was put"), Registry.GetBlockOf(Chunk.GetBlock(1, -64 + 2, 3)).Name, FName(TEXT("minecraft:cobblestone")));
	TestEqual(TEXT("the unknown block is the placeholder"), Registry.GetBlockOf(Chunk.GetBlock(15, -64 + 15, 15)).Name, McJavaImport::GetPlaceholderBlock());
	const FMcStateId Stairs = Chunk.GetBlock(8, -48 + 3, 8);
	TestEqual(TEXT("one-entry palette section"), Registry.GetBlockOf(Stairs).Name, FName(TEXT("minecraft:oak_stairs")));
	TestEqual(TEXT("its facing"), Registry.GetPropertyValue(Stairs, TEXT("facing")), FName(TEXT("south")));
	TestTrue(TEXT("sections not in the file are air"), Registry.IsAir(Chunk.GetBlock(0, 0, 0)));
	TestEqual(TEXT("one unknown kind"), Stats.UnknownStates.Num(), 1);
	TestEqual(TEXT("biome cell"), Chunk.GetBiome(0, -64, 0), FMcBiomeRegistry::Get().Find(TEXT("minecraft:plains")).Get(0));
	TestEqual(TEXT("last biome cell"), Chunk.GetBiome(15, -64 + 15, 15), FMcBiomeRegistry::Get().Find(TEXT("minecraft:desert")).Get(0));

	const FMcBlockEntity* Chest = Chunk.FindBlockEntity(4, -62, 5);
	if (TestNotNull(TEXT("the chest keeps its block entity"), Chest) && TestEqual(TEXT("27 slots"), Chest->Slots.Num(), 27))
	{
		TestEqual(TEXT("item in slot 13"), Chest->Slots[13].Item, FName(TEXT("minecraft:diamond")));
		TestEqual(TEXT("and its count"), Chest->Slots[13].Count, 5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportRealWorldTest, "McClient.McServer.JavaImport.RealWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// Imports a world the reference client generated, if this machine has one (E:\mc-reference or C:\mc-reference).
bool FMcJavaImportRealWorldTest::RunTest(const FString& Parameters)
{
	FString Source;
	for (const TCHAR* Candidate : { TEXT("E:/mc-reference/run/saves/terrain3"), TEXT("C:/mc-reference/run/saves/terrain3") })
	{
		if (McJavaImport::IsJavaWorld(Candidate))
		{
			Source = Candidate;
			break;
		}
	}
	if (Source.IsEmpty())
	{
		AddInfo(TEXT("no reference world on this machine; skipped"));
		return true;
	}
	McJavaImport::FStats Stats;
	const FString Folder = McJavaImport::ImportWorld(Source, TEXT("JavaImportTest"), Stats);
	AddInfo(McJavaImport::Describe(Stats));
	if (!TestFalse(TEXT("the import made a world"), Folder.IsEmpty()))
	{
		return false;
	}
	TestTrue(TEXT("chunks were imported"), Stats.Chunks > 0);
	TestEqual(TEXT("none unreadable"), Stats.ChunksFailed, 0);
	TestEqual(TEXT("no unknown block states"), Stats.UnknownStates.Num(), 0);
	FMcLevelData Level;
	TestTrue(TEXT("the level reads back"), McWorldSave::LoadLevel(McWorldSave::GetWorldDir(Folder), Level));
	TestEqual(TEXT("seed from world_gen_settings.dat"), Level.Seed, int64(-77777));
	McWorldSave::DeleteWorld(Folder);
	return true;
}

#endif
