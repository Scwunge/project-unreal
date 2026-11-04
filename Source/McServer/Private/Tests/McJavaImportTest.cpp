// The Java world importer: palette unpacking, the region file reader, the block mapping and a chunk in NBT bytes.

#include "McBiomeRegistry.h"
#include "McBlockEntity.h"
#include "McBannerEntity.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McDecor.h"
#include "McDecorEntity.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McEntityManager.h"
#include "McChunk.h"
#include "McChunkSave.h"
#include "McJavaExport.h"
#include "McJavaImport.h"
#include "McFurnace.h"
#include "McItemRegistry.h"
#include "McNbt.h"
#include "McWorldSave.h"
#include "McWorldStorage.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

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
		void Doubles(std::initializer_list<double> Values)
		{
			for (double Value : Values)
			{
				uint64 Bits;
				FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
				Big(Bits, 8);
			}
		}
		void Floats(std::initializer_list<float> Values)
		{
			for (float Value : Values)
			{
				uint32 Bits;
				FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
				Big(Bits, 4);
			}
		}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportBlockEntityTest, "McClient.McServer.JavaImport.BlockEntities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportBlockEntityTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries() && FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	FNbtWriter Writer;
	Writer.Compound(TEXT(""));
	Writer.List(TEXT("sections"), 10, 0);
	Writer.List(TEXT("block_entities"), 10, 5);
	{
		// A sign: a plain line and one written as JSON, as older saves keep them.
		Writer.Str(TEXT("id"), TEXT("minecraft:sign"));
		Writer.Int(TEXT("x"), 1); Writer.Int(TEXT("y"), -60); Writer.Int(TEXT("z"), 2);
		Writer.Compound(TEXT("front_text"));
		Writer.List(TEXT("messages"), 8, 2);
		Writer.String(TEXT("Imported"));
		Writer.String(TEXT("{\"text\":\"si\",\"extra\":[\"gn\"]}"));
		Writer.Str(TEXT("color"), TEXT("red"));
		Writer.Byte(TEXT("has_glowing_text"), 1);
		Writer.End();
		Writer.Byte(TEXT("is_waxed"), 1);
		Writer.End();
		// A skull: nothing of ours reads one, so it is kept whole.
		Writer.Str(TEXT("id"), TEXT("minecraft:skull"));
		Writer.Int(TEXT("x"), 1); Writer.Int(TEXT("y"), -60); Writer.Int(TEXT("z"), 4);
		Writer.Str(TEXT("note_block_sound"), TEXT("minecraft:entity.zombie.ambient"));
		Writer.End();
		// A decorated pot with a sherd on its left and nothing listed for its front.
		Writer.Str(TEXT("id"), TEXT("minecraft:decorated_pot"));
		Writer.Int(TEXT("x"), 7); Writer.Int(TEXT("y"), -60); Writer.Int(TEXT("z"), 7);
		Writer.List(TEXT("sherds"), 8, 3);
		Writer.String(TEXT("minecraft:brick"));
		Writer.String(TEXT("minecraft:angler_pottery_sherd"));
		Writer.String(TEXT("minecraft:brick"));
		Writer.End();
		// A furnace with iron ore in the input slot.
		Writer.Str(TEXT("id"), TEXT("minecraft:furnace"));
		Writer.Int(TEXT("x"), 3); Writer.Int(TEXT("y"), -60); Writer.Int(TEXT("z"), 3);
		Writer.List(TEXT("Items"), 10, 1);
		Writer.Byte(TEXT("Slot"), 0);
		Writer.Str(TEXT("id"), TEXT("minecraft:iron_ore"));
		Writer.Int(TEXT("count"), 7);
		Writer.End();
		Writer.End();
		// A chest with a damaged sword in slot 2.
		Writer.Str(TEXT("id"), TEXT("minecraft:chest"));
		Writer.Int(TEXT("x"), 5); Writer.Int(TEXT("y"), -60); Writer.Int(TEXT("z"), 5);
		Writer.List(TEXT("Items"), 10, 1);
		Writer.Byte(TEXT("Slot"), 2);
		Writer.Str(TEXT("id"), TEXT("minecraft:diamond_sword"));
		Writer.Int(TEXT("count"), 1);
		Writer.Compound(TEXT("components"));
		Writer.Int(TEXT("minecraft:damage"), 5);
		Writer.End();
		Writer.End();
		Writer.End();
	}
	Writer.End();
	FMcNbt Root;
	if (!TestTrue(TEXT("the NBT parses"), FMcNbt::Parse(Writer.Bytes, Root)))
	{
		return false;
	}
	FMcChunk Chunk(FIntPoint(0, 0), -64, 24);
	McJavaImport::FStats Stats;
	TestTrue(TEXT("converts"), McJavaImport::ConvertChunk(Root, Chunk, Stats));

	const FMcBlockEntity* Sign = Chunk.FindBlockEntity(1, -60, 2);
	if (TestNotNull(TEXT("the sign is kept"), Sign) && TestTrue(TEXT("as a sign"), Sign->Kind == EMcBlockEntityKind::Sign))
	{
		TestEqual(TEXT("the plain line"), Sign->SignSides[0].Lines[0], FString(TEXT("Imported")));
		TestEqual(TEXT("the JSON line's words"), Sign->SignSides[0].Lines[1], FString(TEXT("sign")));
		TestTrue(TEXT("the rest empty"), Sign->SignSides[0].Lines[2].IsEmpty() && !Sign->SignSides[1].HasText());
		TestEqual(TEXT("colour"), Sign->SignSides[0].Color, FName(TEXT("red")));
		TestTrue(TEXT("glowing"), Sign->SignSides[0].bGlowing);
		TestTrue(TEXT("waxed"), Sign->bWaxed);
	}

	const FMcBlockEntity* Skull = Chunk.FindBlockEntity(1, -60, 4);
	if (TestNotNull(TEXT("the skull is kept"), Skull) && TestTrue(TEXT("as raw"), Skull->Kind == EMcBlockEntityKind::Raw))
	{
		TestEqual(TEXT("with its id"), Skull->EntityId, FName(TEXT("minecraft:skull")));
		FMcNbt Back;
		TestTrue(TEXT("its NBT reads back"), FMcNbt::Parse(Skull->Data, Back));
		TestEqual(TEXT("its sound"), Back.GetString(TEXT("note_block_sound")), FString(TEXT("minecraft:entity.zombie.ambient")));
		// And it survives the save format.
		TArray<uint8> Bytes;
		FMemoryWriter Writer2(Bytes);
		FMcBlockEntity Copy = *Skull;
		Copy.Serialize(Writer2);
		FMcBlockEntity Loaded;
		FMemoryReader Reader(Bytes);
		TestTrue(TEXT("loads"), Loaded.Serialize(Reader));
		TestTrue(TEXT("round trips"), Loaded == *Skull);
	}

	const FMcBlockEntity* Pot = Chunk.FindBlockEntity(7, -60, 7);
	if (TestNotNull(TEXT("the pot is kept"), Pot) && TestTrue(TEXT("as a pot"), Pot->Kind == EMcBlockEntityKind::Pot)
		&& TestEqual(TEXT("four sides"), Pot->Slots.Num(), 4))
	{
		TestEqual(TEXT("the sherd on the left"), Pot->Slots[1].Item, FName(TEXT("minecraft:angler_pottery_sherd")));
		TestEqual(TEXT("a brick for the front"), Pot->Slots[3].Item, FName(TEXT("minecraft:brick")));
	}

	const FMcBlockEntity* Furnace = Chunk.FindBlockEntity(3, -60, 3);
	if (TestNotNull(TEXT("the furnace is kept"), Furnace) && TestTrue(TEXT("as a furnace"), Furnace->Kind == EMcBlockEntityKind::Furnace))
	{
		FMcFurnace Machine;
		FMemoryReader Reader(Furnace->Data);
		TestTrue(TEXT("the machine reads it"), Machine.Serialize(Reader));
		TestEqual(TEXT("iron ore in the input"), Machine.GetItems()->Get(FMcFurnace::InputSlot).Item, FName(TEXT("minecraft:iron_ore")));
		TestEqual(TEXT("seven of them"), Machine.GetItems()->Get(FMcFurnace::InputSlot).Count, 7);
	}

	const FMcBlockEntity* Chest = Chunk.FindBlockEntity(5, -60, 5);
	if (TestNotNull(TEXT("the chest is kept"), Chest) && TestEqual(TEXT("27 slots"), Chest->Slots.Num(), 27))
	{
		TestEqual(TEXT("the sword"), Chest->Slots[2].Item, FName(TEXT("minecraft:diamond_sword")));
		TestEqual(TEXT("with its damage"), Chest->Slots[2].GetDamage(), 5);
	}
	TestEqual(TEXT("one raw kind counted"), Stats.KeptRawBlockEntities.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportBannerTest, "McClient.McServer.JavaImport.Banner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportBannerTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	World.SetBlock(FIntVector(3, 5, 4), Registry.ParseState(TEXT("minecraft:red_banner[rotation=4]")).Get(0));
	FNbtWriter Writer;
	Writer.Compound(TEXT(""));
	Writer.List(TEXT("patterns"), 10, 2);
	Writer.Str(TEXT("pattern"), TEXT("minecraft:stripe_downright"));
	Writer.Str(TEXT("color"), TEXT("blue"));
	Writer.End();
	Writer.Str(TEXT("pattern"), TEXT("minecraft:border"));
	Writer.Str(TEXT("color"), TEXT("white"));
	Writer.End();
	Writer.End();
	FMcBlockEntity Raw;
	Raw.Kind = EMcBlockEntityKind::Raw;
	Raw.EntityId = TEXT("minecraft:banner");
	Raw.Data = Writer.Bytes;
	FMcChunk* Chunk = World.FindChunk(FIntPoint(0, 0));
	Chunk->SetBlockEntity(3, 5, 4, Raw);
	FMcEntityManager Manager(World);
	TestEqual(TEXT("one flag stood"), McJavaImport::StandBanners(*Chunk, Manager), 1);
	const FMcBannerEntity* Flag = FMcBannerEntity::FindAt(Manager, FIntVector(3, 5, 4));
	if (TestNotNull(TEXT("the flag is there"), Flag))
	{
		TestEqual(TEXT("colour, standing, both patterns in order"), Flag->Layers, FString(TEXT("red;s;stripe_downright=blue,border=white")));
	}
	TestEqual(TEXT("a second pass adds nothing"), McJavaImport::StandBanners(*Chunk, Manager), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportSceneryTest, "McClient.McServer.JavaImport.Scenery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportSceneryTest::RunTest(const FString& Parameters)
{
	const auto Parse = [](const FNbtWriter& Writer, FMcNbt& Out) { return FMcNbt::Parse(Writer.Bytes, Out); };
	FString Line;
	FVector Pos;
	float Yaw = 0.0f;
	{
		// An item frame as the game saved it: on the south side of a wall, a sword turned two eighths.
		FNbtWriter Writer;
		Writer.Compound(TEXT(""));
		Writer.Str(TEXT("id"), TEXT("minecraft:item_frame"));
		Writer.Byte(TEXT("Facing"), 3);
		Writer.Byte(TEXT("ItemRotation"), 2);
		Writer.List(TEXT("Pos"), 6, 3);
		Writer.Doubles({ 8.5, -58.5, 1.03125 });
		Writer.Compound(TEXT("Item"));
		Writer.Str(TEXT("id"), TEXT("minecraft:diamond_sword"));
		Writer.Int(TEXT("count"), 1);
		Writer.End();
		Writer.End();
		FMcNbt Entity;
		if (TestTrue(TEXT("frame parses"), Parse(Writer, Entity)) && TestTrue(TEXT("frame is scenery"), McJavaImport::GetSceneryLine(Entity, Line, Pos, Yaw)))
		{
			TestEqual(TEXT("frame line"), Line, FString(TEXT("frame;f=3;rot=2;invis=0;item=minecraft:diamond_sword")));
			TestEqual(TEXT("frame z"), Pos.Z, 1.03125);
			McDecor::FDecor Decor;
			TestTrue(TEXT("decodes"), McDecor::Decode(Line, Decor));
			TestEqual(TEXT("item"), Decor.Item, FString(TEXT("minecraft:diamond_sword")));
			TestEqual(TEXT("rotation"), Decor.ItemRotation, 2);
		}
	}
	{
		FNbtWriter Writer;
		Writer.Compound(TEXT(""));
		Writer.Str(TEXT("id"), TEXT("minecraft:armor_stand"));
		Writer.Byte(TEXT("ShowArms"), 1);
		Writer.Byte(TEXT("NoBasePlate"), 1);
		Writer.List(TEXT("Rotation"), 5, 2);
		Writer.Floats({ 30.0f, 0.0f });
		Writer.List(TEXT("Pos"), 6, 3);
		Writer.Doubles({ 4.5, -60.0, 6.5 });
		Writer.Compound(TEXT("Pose"));
		Writer.List(TEXT("Head"), 5, 3);
		Writer.Floats({ 10.0f, 20.0f, 0.0f });
		Writer.End();
		Writer.Compound(TEXT("equipment"));
		Writer.Compound(TEXT("head"));
		Writer.Str(TEXT("id"), TEXT("minecraft:iron_helmet"));
		Writer.End();
		Writer.End();
		Writer.End();
		FMcNbt Entity;
		McDecor::FDecor Decor;
		if (TestTrue(TEXT("stand parses"), Parse(Writer, Entity)) && TestTrue(TEXT("stand is scenery"), McJavaImport::GetSceneryLine(Entity, Line, Pos, Yaw)) && TestTrue(TEXT("decodes"), McDecor::Decode(Line, Decor)))
		{
			TestEqual(TEXT("yaw"), Yaw, 30.0f);
			TestEqual(TEXT("yaw kept"), Decor.Yaw, 30.0f);
			TestTrue(TEXT("arms"), Decor.bArms);
			TestFalse(TEXT("no base plate"), Decor.bBasePlate);
			TestEqual(TEXT("head pitch"), Decor.Pose[static_cast<int32>(McDecor::EPart::Head)].X, 10.0f);
			TestEqual(TEXT("head turn"), Decor.Pose[static_cast<int32>(McDecor::EPart::Head)].Y, 20.0f);
			TestEqual(TEXT("unset arm is the default"), Decor.Pose[static_cast<int32>(McDecor::EPart::RightArm)], McDecor::GetDefaultPose(McDecor::EPart::RightArm));
			TestEqual(TEXT("helmet"), Decor.Equipment[static_cast<int32>(McDecor::ESlot::Head)], FString(TEXT("minecraft:iron_helmet")));
		}
	}
	{
		FNbtWriter Writer;
		Writer.Compound(TEXT(""));
		Writer.Str(TEXT("id"), TEXT("minecraft:painting"));
		Writer.Str(TEXT("variant"), TEXT("minecraft:kebab"));
		Writer.Byte(TEXT("facing"), 0);
		Writer.List(TEXT("Pos"), 6, 3);
		Writer.Doubles({ 1.5, -58.5, 1.5 });
		Writer.End();
		FMcNbt Entity;
		if (TestTrue(TEXT("painting parses"), Parse(Writer, Entity)) && TestTrue(TEXT("painting is scenery"), McJavaImport::GetSceneryLine(Entity, Line, Pos, Yaw)))
		{
			TestEqual(TEXT("painting line"), Line, FString(TEXT("painting;f=3;v=kebab")));
		}
	}
	{
		FNbtWriter Writer;
		Writer.Compound(TEXT(""));
		Writer.Str(TEXT("id"), TEXT("minecraft:villager"));
		Writer.List(TEXT("Pos"), 6, 3);
		Writer.Doubles({ 1.5, -58.5, 1.5 });
		Writer.End();
		FMcNbt Entity;
		TestTrue(TEXT("villager parses"), Parse(Writer, Entity));
		TestFalse(TEXT("a villager is not scenery"), McJavaImport::GetSceneryLine(Entity, Line, Pos, Yaw));
	}
	{
		// Written the way the importer writes a chunk's scenery, a host restores it.
		FMcDecorEntity Scenic;
		Scenic.Body.Position = FVector(4.5, 260.0, 6.5);
		Scenic.Look = TEXT("armorstand;arms=1");
		FString Type = FMcDecorEntity::StaticType().ToString();
		TArray<uint8> Data;
		FMemoryWriter DataWriter(Data);
		Scenic.Serialize(DataWriter);
		TArray<uint8> Bytes;
		FMemoryWriter BodyWriter(Bytes);
		int32 Count = 1;
		BodyWriter << Count << Type << Data;
		FMcBlockWorld World{FMcDimensionType()};
		FMcEntityHost Host(World, 1);
		TestEqual(TEXT("the scenery comes back from a chunk save"), Host.RestoreChunk(Bytes), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportBuildTest, "McClient.McServer.JavaImport.ImportBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// The build of E:\mc-reference\scripts\import_build.txt, as the reference client saved it (save importtest2), read back
// through the importer: every block of the script where the script put it.
bool FMcJavaImportBuildTest::RunTest(const FString& Parameters)
{
	FString Folder;
	for (const TCHAR* Candidate : { TEXT("E:/mc-reference/run/saves/importtest2"), TEXT("C:/mc-reference/run/saves/importtest2") })
	{
		if (McJavaImport::IsJavaWorld(Candidate) && IFileManager::Get().FileExists(*(FString(Candidate) / TEXT("dimensions/minecraft/overworld/region/r.0.0.mca"))))
		{
			Folder = Candidate;
			break;
		}
	}
	if (Folder.IsEmpty() || !LoadRegistries())
	{
		AddInfo(TEXT("no import_build save on this machine; skipped"));
		return true;
	}
	TArray<uint8> File, Raw;
	const FString RegionFolder = Folder / TEXT("dimensions/minecraft/overworld/region");
	TestTrue(TEXT("region reads"), FFileHelper::LoadFileToArray(File, *(RegionFolder / TEXT("r.0.0.mca"))));
	bool bMissing = false;
	FMcNbt Root;
	if (!TestTrue(TEXT("chunk 0,0 reads"), McJavaImport::ReadChunkBytes(File, 0, 0, RegionFolder, 0, 0, Raw, bMissing) && FMcNbt::Parse(Raw, Root)))
	{
		return false;
	}
	FMcChunk Chunk(FIntPoint(0, 0), -64, 24);
	McJavaImport::FStats Stats;
	TestTrue(TEXT("converts"), McJavaImport::ConvertChunk(Root, Chunk, Stats));
	TestEqual(TEXT("no unknown block states"), Stats.UnknownStates.Num(), 0);

	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const auto Check = [&](int32 X, int32 Y, int32 Z, const TCHAR* Expected)
	{
		const TOptional<FMcStateId> State = Registry.ParseState(Expected);
		const FMcStateId Actual = Chunk.GetBlock(X, Y, Z);
		TestTrue(*FString::Printf(TEXT("(%d,%d,%d) is %s, was %s"), X, Y, Z, Expected, *Registry.Describe(Actual)), State && *State == Actual);
	};
	Check(2, -61, 2, TEXT("minecraft:stone"));
	Check(0, -60, 0, TEXT("minecraft:oak_log[axis=y]"));
	Check(0, -58, 0, TEXT("minecraft:oak_log[axis=y]"));
	Check(2, -60, 0, TEXT("minecraft:oak_stairs[facing=east,half=top,shape=straight]"));
	Check(3, -60, 0, TEXT("minecraft:stone_brick_slab[type=top]"));
	Check(4, -60, 0, TEXT("minecraft:oak_door[facing=north,half=lower,hinge=left,open=false]"));
	Check(4, -59, 0, TEXT("minecraft:oak_door[facing=north,half=upper,hinge=left,open=false]"));
	Check(6, -60, 0, TEXT("minecraft:red_bed[facing=south,part=foot]"));
	Check(6, -60, 1, TEXT("minecraft:red_bed[facing=south,part=head]"));
	Check(8, -60, 0, TEXT("minecraft:chest[facing=south,type=single]"));
	Check(10, -60, 0, TEXT("minecraft:barrel[facing=up]"));
	Check(12, -60, 0, TEXT("minecraft:oak_sign[rotation=4]"));
	Check(14, -60, 0, TEXT("minecraft:white_banner[rotation=0]"));
	Check(7, -60, 4, TEXT("minecraft:lever[face=floor,facing=north,powered=true]"));
	Check(9, -60, 4, TEXT("minecraft:furnace[facing=west,lit=false]"));
	for (int32 X = 0; X <= 5; ++X)
	{
		TestEqual(*FString::Printf(TEXT("wall at %d"), X), Registry.GetBlockOf(Chunk.GetBlock(X, -60, 4)).Name, FName(TEXT("minecraft:cobblestone_wall")));
	}

	const FMcBlockEntity* Chest = Chunk.FindBlockEntity(8, -60, 0);
	if (TestNotNull(TEXT("the chest keeps its items"), Chest) && Chest->Slots.Num() == 27)
	{
		TestEqual(TEXT("diamonds in slot 0"), Chest->Slots[0].Item, FName(TEXT("minecraft:diamond")));
		TestEqual(TEXT("five of them"), Chest->Slots[0].Count, 5);
		TestEqual(TEXT("planks in slot 13"), Chest->Slots[13].Item, FName(TEXT("minecraft:oak_planks")));
		TestEqual(TEXT("a stack of them"), Chest->Slots[13].Count, 64);
	}
	const FMcBlockEntity* Barrel = Chunk.FindBlockEntity(10, -60, 0);
	if (TestNotNull(TEXT("the barrel keeps its items"), Barrel) && Barrel->Slots.Num() == 27)
	{
		TestEqual(TEXT("iron in slot 3"), Barrel->Slots[3].Item, FName(TEXT("minecraft:iron_ingot")));
		TestEqual(TEXT("seventeen"), Barrel->Slots[3].Count, 17);
	}
	const FMcBlockEntity* Sign = Chunk.FindBlockEntity(12, -60, 0);
	if (TestNotNull(TEXT("the sign is kept"), Sign) && TestTrue(TEXT("as a sign"), Sign->Kind == EMcBlockEntityKind::Sign))
	{
		TestTrue(TEXT("its first line is the text that was set"), Sign->SignSides[0].Lines[0].Contains(TEXT("Imported")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportLiveTest, "McClient.McServer.JavaImport.Live",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// A world begun with BeginImport has no chunks until the live import runs: a chunk is asked for before its turn and comes
// at once, and the rest follow until the dimension is marked done.
bool FMcJavaImportLiveTest::RunTest(const FString& Parameters)
{
	FString Source;
	for (const TCHAR* Candidate : { TEXT("E:/mc-reference/run/saves/importflat"), TEXT("C:/mc-reference/run/saves/importflat") })
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
	const FString Folder = McJavaImport::BeginImport(Source, TEXT("JavaImportLiveTest"));
	if (!TestFalse(TEXT("the import began"), Folder.IsEmpty()))
	{
		return false;
	}
	const FString WorldDir = McWorldSave::GetWorldDir(Folder);
	FMcLevelData Level;
	TestTrue(TEXT("the level is there"), McWorldSave::LoadLevel(WorldDir, Level));
	{
		TSharedPtr<FMcWorldStorage, ESPMode::ThreadSafe> Storage = MakeShared<FMcWorldStorage, ESPMode::ThreadSafe>(WorldDir / McWorldSave::GetDimensionFolder(TEXT("minecraft:overworld")));
		TArray<uint8> Early;
		TestFalse(TEXT("nothing is saved before the import runs"), Storage->Load(FIntPoint(0, 0), Early));
		const TSharedPtr<McJavaImport::FLiveImport, ESPMode::ThreadSafe> Live = McJavaImport::FLiveImport::Start(WorldDir, TEXT("minecraft:overworld"), Storage, FIntPoint(0, 0));
		if (TestTrue(TEXT("the live import starts"), Live.IsValid()))
		{
			TArray<uint8> Bytes;
			TestTrue(TEXT("a chunk asked for before its turn comes at once"), Storage->Load(FIntPoint(0, 0), Bytes) && Bytes.Num() > 0);
			const double Deadline = FPlatformTime::Seconds() + 120.0;
			while (!Live->IsFinished() && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.05f);
			}
			TestTrue(TEXT("it finishes"), Live->IsFinished());
			Live->Stop();
			TestTrue(TEXT("it is marked done"), IFileManager::Get().FileExists(*(WorldDir / McWorldSave::GetDimensionFolder(TEXT("minecraft:overworld")) / TEXT("javaimport.done"))));
			TestTrue(TEXT("a chunk is in the save"), Storage->Contains(FIntPoint(0, 0)));
		}
		Storage->Flush();
	}
	McWorldSave::DeleteWorld(Folder);
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
	TestTrue(*FString::Printf(TEXT("some of the world is not air (%lld of %lld blocks)"), Stats.NonAirBlocks, Stats.Blocks), Stats.NonAirBlocks > 0);
	FMcLevelData Level;
	TestTrue(TEXT("the level reads back"), McWorldSave::LoadLevel(McWorldSave::GetWorldDir(Folder), Level));
	TestEqual(TEXT("seed from world_gen_settings.dat"), Level.Seed, int64(-77777));
	McWorldSave::DeleteWorld(Folder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportPlayerTest, "McClient.McServer.JavaImport.Player",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportPlayerTest::RunTest(const FString& Parameters)
{
	// A player file as the game saves it: place, mode, hunger, experience, a hotbar stone, the off hand and an ender chest item.
	FNbtWriter Writer;
	Writer.Compound(TEXT(""));
	Writer.List(TEXT("Pos"), 6, 3);
	Writer.Doubles({ 12.5, 70.0, -3.5 });
	Writer.List(TEXT("Rotation"), 5, 2);
	Writer.Floats({ 90.0f, 15.0f });
	Writer.Str(TEXT("Dimension"), TEXT("minecraft:overworld"));
	Writer.Int(TEXT("playerGameType"), 1);
	Writer.Int(TEXT("SelectedItemSlot"), 4);
	Writer.Int(TEXT("foodLevel"), 12);
	Writer.Int(TEXT("XpLevel"), 7);
	Writer.List(TEXT("Inventory"), 10, 2);
	Writer.Byte(TEXT("Slot"), 3);
	Writer.Str(TEXT("id"), TEXT("minecraft:stone"));
	Writer.Int(TEXT("count"), 20);
	Writer.End();
	Writer.Byte(TEXT("Slot"), -106);
	Writer.Str(TEXT("id"), TEXT("minecraft:shield"));
	Writer.Int(TEXT("count"), 1);
	Writer.End();
	Writer.Compound(TEXT("equipment"));
	Writer.Compound(TEXT("head"));
	Writer.Str(TEXT("id"), TEXT("minecraft:iron_helmet"));
	Writer.Int(TEXT("count"), 1);
	Writer.End();
	Writer.End();
	Writer.List(TEXT("EnderItems"), 10, 1);
	Writer.Byte(TEXT("Slot"), 5);
	Writer.Str(TEXT("id"), TEXT("minecraft:diamond"));
	Writer.Int(TEXT("count"), 9);
	Writer.End();
	Writer.End();
	FMcNbt Root;
	if (!TestTrue(TEXT("the player parses"), FMcNbt::Parse(Writer.Bytes, Root)))
	{
		return false;
	}
	FMcPlayerData Player;
	if (!TestTrue(TEXT("it converts"), McJavaImport::ConvertPlayer(Root, Player)))
	{
		return false;
	}
	TestEqual(TEXT("x"), Player.Position.X, 12.5);
	TestEqual(TEXT("z"), Player.Position.Z, -3.5);
	TestEqual(TEXT("yaw"), Player.Yaw, 90.0f);
	TestTrue(TEXT("creative"), Player.bCreative);
	TestEqual(TEXT("selected slot"), Player.SelectedSlot, 4);
	TestEqual(TEXT("food"), Player.Stats.FindRef(TEXT("food")), 12.0);
	TestEqual(TEXT("experience level"), Player.Stats.FindRef(TEXT("xp_level")), 7.0);
	const auto Count = [&Player](int32 Slot, const TCHAR* Item) -> int32
	{
		for (const FMcSavedSlot& Saved : Player.Slots)
		{
			if (Saved.Slot == Slot && Saved.Stack.Item == FName(Item))
			{
				return Saved.Stack.Count;
			}
		}
		return 0;
	};
	TestEqual(TEXT("hotbar stone"), Count(3, TEXT("minecraft:stone")), 20);
	TestEqual(TEXT("off hand shield at 150"), Count(150, TEXT("minecraft:shield")), 1);
	TestEqual(TEXT("helmet at 103"), Count(103, TEXT("minecraft:iron_helmet")), 1);
	TestEqual(TEXT("ender chest slot 5 at 205"), Count(205, TEXT("minecraft:diamond")), 9);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaExportTest, "McClient.McServer.JavaExport.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaExportTest::RunTest(const FString& Parameters)
{
	// Packing is the inverse of unpacking.
	TArray<int32> Indexes;
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		Indexes.Add((Index * 7 + 3) % 20);
	}
	TArray<int64> Longs;
	McJavaExport::PackIndices(Indexes, 5, Longs);
	TestEqual(TEXT("342 longs for 4096 entries at 5 bits"), Longs.Num(), 342);
	TArray<int32> Back;
	TestTrue(TEXT("unpacks"), McJavaImport::UnpackIndices(Longs, 5, 4096, Back));
	TestTrue(TEXT("packed entries come back"), Back == Indexes);

	if (!LoadRegistries())
	{
		AddError(TEXT("registries did not load"));
		return false;
	}
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId Stone = Registry.ParseState(TEXT("minecraft:stone")).Get(0);
	const FMcStateId Stairs = Registry.ParseState(TEXT("minecraft:oak_stairs[facing=east,half=top]")).Get(0);
	FMcChunk Source(FIntPoint(-3, 5), -64, 24);
	Source.SetBlock(0, -64, 0, Stone);
	Source.SetBlock(7, -40, 9, Stairs);
	Source.SetBlock(15, 100, 15, Stone);

	FMcNbt Root;
	McJavaExport::MakeChunk(Source, Root);
	FIntPoint Pos;
	TestTrue(TEXT("position is written"), McJavaImport::GetChunkPos(Root, Pos) && Pos == FIntPoint(-3, 5));
	FMcChunk Result(FIntPoint(-3, 5), -64, 24);
	McJavaImport::FStats Stats;
	TestTrue(TEXT("our own chunk converts back"), McJavaImport::ConvertChunk(Root, Result, Stats));
	TestTrue(TEXT("stone"), Result.GetBlock(0, -64, 0) == Stone);
	TestTrue(TEXT("stairs with properties"), Result.GetBlock(7, -40, 9) == Stairs);
	TestTrue(TEXT("stone high up"), Result.GetBlock(15, 100, 15) == Stone);
	TestTrue(TEXT("air elsewhere"), Registry.IsAir(Result.GetBlock(1, 0, 1)));

	// A region file made of it reads back through the importer's reader.
	TArray<uint8> Bytes;
	Root.WriteRoot(Bytes);
	TMap<FIntPoint, TArray<uint8>> Chunks;
	Chunks.Add(FIntPoint(-3, 5), Bytes);
	TArray<uint8> File;
	McJavaExport::MakeRegion(Chunks, File);
	TArray<uint8> Read;
	bool bMissing = false;
	TestTrue(TEXT("chunk is in the region"), McJavaImport::ReadChunkBytes(File, 29, 5, FString(), -3, 5, Read, bMissing));
	TestTrue(TEXT("region holds the chunk's bytes"), Read.Num() >= Bytes.Num() && FMemory::Memcmp(Read.GetData(), Bytes.GetData(), Bytes.Num()) == 0);
	TestFalse(TEXT("other slots are empty"), McJavaImport::ReadChunkBytes(File, 0, 0, FString(), -3, 5, Read, bMissing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportDimensionsTest, "McClient.McServer.JavaImport.Dimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportDimensionsTest::RunTest(const FString& Parameters)
{
	// The ref save (a vanilla world with a Nether): its Nether is imported and written back out as a Java save.
	FString Source;
	for (const TCHAR* Candidate : { TEXT("E:/mc-reference/run/saves/ref"), TEXT("C:/mc-reference/run/saves/ref") })
	{
		if (McJavaImport::IsJavaWorld(Candidate))
		{
			Source = Candidate;
			break;
		}
	}
	if (Source.IsEmpty())
	{
		AddInfo(TEXT("no ref world on this machine; skipped"));
		return true;
	}
	McJavaImport::FStats Stats;
	const FString Folder = McJavaImport::ImportWorld(Source, TEXT("JavaImportDims"), Stats);
	if (!TestFalse(TEXT("the import made a world"), Folder.IsEmpty()))
	{
		return false;
	}
	AddInfo(McJavaImport::Describe(Stats));
	const FString WorldDir = McWorldSave::GetWorldDir(Folder);
	TArray<FString> Overworld, Nether, End;
	IFileManager::Get().FindFiles(Overworld, *(WorldDir / McWorldSave::GetDimensionFolder(TEXT("minecraft:overworld")) / TEXT("*.mcr")), true, false);
	IFileManager::Get().FindFiles(Nether, *(WorldDir / McWorldSave::GetDimensionFolder(TEXT("minecraft:the_nether")) / TEXT("*.mcr")), true, false);
	IFileManager::Get().FindFiles(End, *(WorldDir / McWorldSave::GetDimensionFolder(TEXT("minecraft:the_end")) / TEXT("*.mcr")), true, false);
	AddInfo(FString::Printf(TEXT("region files: overworld %d, nether %d, end %d"), Overworld.Num(), Nether.Num(), End.Num()));
	TestTrue(TEXT("overworld regions imported"), Overworld.Num() > 0);
	TestTrue(TEXT("nether regions imported"), Nether.Num() > 0);
	TestEqual(TEXT("none unreadable"), Stats.ChunksFailed, 0);

	const FString Out = FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("JavaExportDims");
	IFileManager::Get().DeleteDirectory(*Out, false, true);
	McJavaExport::FStats Exported;
	TestTrue(TEXT("exports"), McJavaExport::ExportWorld(WorldDir, Out, Exported));
	AddInfo(FString::Printf(TEXT("exported %d chunks in %d regions, %d failed"), Exported.Chunks, Exported.Regions, Exported.Failed));
	TestEqual(TEXT("every chunk exported"), Exported.Chunks, Stats.Chunks);
	TestTrue(TEXT("level.dat written"), IFileManager::Get().FileExists(*(Out / TEXT("level.dat"))));
	TestTrue(TEXT("a nether region written"), IFileManager::Get().DirectoryExists(*(Out / TEXT("dimensions/minecraft/the_nether/region"))));
	// The exported save reads as a Java world again.
	TestTrue(TEXT("the export is a Java world"), McJavaImport::IsJavaWorld(Out));
	McJavaImport::FStats Again;
	const FString Second = McJavaImport::ImportWorld(Out, TEXT("JavaImportDims2"), Again);
	TestFalse(TEXT("the export imports"), Second.IsEmpty());
	TestEqual(TEXT("same chunks after the round trip"), Again.Chunks, Stats.Chunks);
	TestEqual(TEXT("same blocks after the round trip"), Again.NonAirBlocks, Stats.NonAirBlocks);
	IFileManager::Get().DeleteDirectory(*Out, false, true);
	McWorldSave::DeleteWorld(Second);
	McWorldSave::DeleteWorld(Folder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportTradesTest, "McClient.McServer.JavaImport.TradesInRealSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportTradesTest::RunTest(const FString& Parameters)
{
	// The playerref save (1070 only) has a farmer with an Offers recipe: the field names the importer reads are the ones in it
	// (a fresh recipe has maxUses buy sell xp uses priceMultiplier; demand and buyB appear only when they apply).
	const FString Folder = TEXT("C:/mc-reference/run/saves/playerref/dimensions/minecraft/overworld/entities");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.mca")), true, false);
	if (Files.IsEmpty())
	{
		AddInfo(TEXT("no playerref save on this machine; skipped"));
		return true;
	}
	int32 Found = 0;
	for (const FString& File : Files)
	{
		TArray<uint8> Region;
		if (!FFileHelper::LoadFileToArray(Region, *(Folder / File)))
		{
			continue;
		}
		for (int32 Slot = 0; Slot < 1024; ++Slot)
		{
			TArray<uint8> Bytes;
			bool bMissing = false;
			FMcNbt Root;
			if (!McJavaImport::ReadChunkBytes(Region, Slot % 32, Slot / 32, Folder, 0, 0, Bytes, bMissing) || !FMcNbt::Parse(Bytes, Root))
			{
				continue;
			}
			const FMcNbt* List = Root.Find(TEXT("Entities"));
			if (!List)
			{
				List = Root.Find(TEXT("entities"));
			}
			if (!List)
			{
				continue;
			}
			for (const FMcNbt& Entity : List->Values)
			{
				const FMcNbt* Offers = Entity.Find(TEXT("Offers"));
				const FMcNbt* Recipes = Offers ? Offers->Find(TEXT("Recipes")) : nullptr;
				if (!Recipes || Recipes->Values.IsEmpty())
				{
					continue;
				}
				++Found;
				const FMcNbt& Recipe = Recipes->Values[0];
				FString Keys;
				for (const FString& Key : Recipe.Keys)
				{
					Keys += Key + TEXT(" ");
				}
				AddInfo(FString::Printf(TEXT("%s: %d recipes, first has: %s"), *Entity.GetString(TEXT("id")), Recipes->Values.Num(), *Keys));
				for (const TCHAR* Key : { TEXT("buy"), TEXT("sell"), TEXT("uses"), TEXT("maxUses"), TEXT("xp"), TEXT("priceMultiplier") })
				{
					TestNotNull(*FString::Printf(TEXT("recipe has %s"), Key), Recipe.Find(Key));
				}
			}
		}
	}
	TestTrue(TEXT("a villager with offers was found"), Found > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportPlayerRealTest, "McClient.McServer.JavaImport.PlayerInRealSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportPlayerRealTest::RunTest(const FString& Parameters)
{
	// The playerref save (1070 only): what the importer made of its player file.
	const FString Source = TEXT("C:/mc-reference/run/saves/playerref");
	if (!McJavaImport::IsJavaWorld(Source))
	{
		AddInfo(TEXT("no playerref save on this machine; skipped"));
		return true;
	}
	McJavaImport::FStats Stats;
	const FString Folder = McJavaImport::ImportWorld(Source, TEXT("JavaImportPlayerReal"), Stats);
	if (!TestFalse(TEXT("the import made a world"), Folder.IsEmpty()))
	{
		return false;
	}
	FString Json;
	FFileHelper::LoadFileToString(Json, *(McWorldSave::GetWorldDir(Folder) / TEXT("playerdata") / TEXT("imported-player.json")));
	AddInfo(TEXT("imported-player.json: ") + Json);
	FMcPlayerData Player;
	TestTrue(TEXT("the player loads"), McWorldSave::LoadPlayer(McWorldSave::GetWorldDir(Folder), TEXT("some-other-name"), Player));
	bool bOffHand = false;
	for (const FMcSavedSlot& Slot : Player.Slots)
	{
		bOffHand |= Slot.Slot == 150;
	}
	TestTrue(TEXT("the off hand slot (150) is in the loaded player"), bOffHand);
	McWorldSave::DeleteWorld(Folder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportEndTest, "McClient.McServer.JavaImport.EndInRealSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportEndTest::RunTest(const FString& Parameters)
{
	// The endref save (1070 only): a vanilla world whose End was visited, so it has End chunks.
	const FString Source = TEXT("C:/mc-reference/run/saves/endref");
	if (!McJavaImport::IsJavaWorld(Source))
	{
		AddInfo(TEXT("no endref save on this machine; skipped"));
		return true;
	}
	McJavaImport::FStats Stats;
	const FString Folder = McJavaImport::ImportWorld(Source, TEXT("JavaImportEnd"), Stats);
	if (!TestFalse(TEXT("the import made a world"), Folder.IsEmpty()))
	{
		return false;
	}
	AddInfo(McJavaImport::Describe(Stats));
	const FString WorldDir = McWorldSave::GetWorldDir(Folder);
	TArray<FString> End;
	IFileManager::Get().FindFiles(End, *(WorldDir / McWorldSave::GetDimensionFolder(TEXT("minecraft:the_end")) / TEXT("*.mcr")), true, false);
	AddInfo(FString::Printf(TEXT("End region files: %d"), End.Num()));
	TestTrue(TEXT("End regions imported"), End.Num() > 0);
	TestEqual(TEXT("none unreadable"), Stats.ChunksFailed, 0);
	TestEqual(TEXT("no unknown block states"), Stats.UnknownStates.Num(), 0);

	// The End's chunks load: the island's centre has end stone and our two placed blocks are where they were put.
	FMcDimensionType Type;
	TestTrue(TEXT("End dimension type"), FMcDimensionType::Load(TEXT("minecraft:the_end"), Type));
	FMcWorldStorage Storage(WorldDir / McWorldSave::GetDimensionFolder(TEXT("minecraft:the_end")));
	FMcChunk Chunk(FIntPoint(0, 0), Type.MinY, Type.Height / 16);
	FMcChunkSaveExtras Extras;
	TArray<uint8> Payload;
	if (TestTrue(TEXT("chunk 0,0 is saved"), Storage.Load(FIntPoint(0, 0), Payload)) && TestTrue(TEXT("it decodes"), McChunkSave::Decode(Payload, Chunk, Extras)))
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		TestEqual(TEXT("purpur block where it was set"), Registry.GetBlockOf(Chunk.GetBlock(3, 75, 3)).Name, FName(TEXT("minecraft:purpur_block")));
		TestEqual(TEXT("end rod above it"), Registry.GetBlockOf(Chunk.GetBlock(3, 76, 3)).Name, FName(TEXT("minecraft:end_rod")));
	}
	McWorldSave::DeleteWorld(Folder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportCampfireDumpTest, "McClient.McServer.JavaImport.CampfireInRealSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportCampfireDumpTest::RunTest(const FString& Parameters)
{
	// The campref save (1070 only): what a vanilla campfire block entity holds, key by key.
	const FString Folder = TEXT("C:/mc-reference/run/saves/campref/dimensions/minecraft/overworld/region");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.mca")), true, false);
	if (Files.IsEmpty())
	{
		AddInfo(TEXT("no campref save on this machine; skipped"));
		return true;
	}
	int32 Found = 0;
	const auto Describe = [](const FMcNbt& Tag, auto&& Self, int32 Depth) -> FString
	{
		if (Tag.Type == FMcNbt::EType::Compound)
		{
			FString Out = TEXT("{");
			for (int32 Index = 0; Index < Tag.Keys.Num(); ++Index)
			{
				Out += Tag.Keys[Index] + TEXT("=") + Self(Tag.Values[Index], Self, Depth + 1) + TEXT(" ");
			}
			return Out + TEXT("}");
		}
		if (Tag.Type == FMcNbt::EType::List || Tag.Type == FMcNbt::EType::IntArray)
		{
			FString Out = FString::Printf(TEXT("[type %d:"), static_cast<int32>(Tag.Type));
			for (const FMcNbt& Value : Tag.Values)
			{
				Out += TEXT(" ") + Self(Value, Self, Depth + 1);
			}
			return Out + TEXT("]");
		}
		return Tag.Type == FMcNbt::EType::String ? Tag.String : FString::Printf(TEXT("%lld(t%d)"), Tag.Integer, static_cast<int32>(Tag.Type));
	};
	for (const FString& File : Files)
	{
		TArray<uint8> Region;
		if (!FFileHelper::LoadFileToArray(Region, *(Folder / File)))
		{
			continue;
		}
		for (int32 Slot = 0; Slot < 1024; ++Slot)
		{
			TArray<uint8> Bytes;
			bool bMissing = false;
			FMcNbt Root;
			if (!McJavaImport::ReadChunkBytes(Region, Slot % 32, Slot / 32, Folder, 0, 0, Bytes, bMissing) || !FMcNbt::Parse(Bytes, Root))
			{
				continue;
			}
			const FMcNbt* List = Root.Find(TEXT("block_entities"));
			if (!List)
			{
				continue;
			}
			for (const FMcNbt& Entity : List->Values)
			{
				if (Entity.GetString(TEXT("id")).Contains(TEXT("campfire")))
				{
					++Found;
					AddInfo(TEXT("campfire block entity: ") + Describe(Entity, Describe, 0));
				}
			}
		}
	}
	TestTrue(TEXT("campfires were found"), Found > 0);

	// The logged campfire at 3,260,3 holds beef (spot 0, 212 of 600) and porkchop (spot 1, 402 of 600): imported, it keeps both.
	FMcBlockEntity Source = FMcBlockEntity::MakeCampfire();
	Source.Slots[0] = FMcItemStack(TEXT("minecraft:beef"), 1);
	Source.Slots[3] = FMcItemStack(TEXT("minecraft:salmon"), 1);
	Source.CookingTimes[0] = 212;
	Source.CookingTotals[0] = 600;
	Source.CookingTimes[3] = 232;
	Source.CookingTotals[3] = 600;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Source.Serialize(Writer);
	FMcBlockEntity Loaded;
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("a campfire reads back"), Loaded.Serialize(Reader));
	TestTrue(TEXT("a campfire reads back as it was saved"), Loaded == Source);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcJavaImportRegionDumpTest, "McClient.McServer.JavaImport.RegionLayoutInRealSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcJavaImportRegionDumpTest::RunTest(const FString& Parameters)
{
	// A real region file of the campref save (1070 only): the header and the first chunks' root keys, to fit the export.
	const FString Folder = TEXT("C:/mc-reference/run/saves/campref/dimensions/minecraft/overworld/region");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.mca")), true, false);
	if (Files.IsEmpty())
	{
		AddInfo(TEXT("no campref save on this machine; skipped"));
		return true;
	}
	TArray<uint8> Region;
	if (!FFileHelper::LoadFileToArray(Region, *(Folder / Files[0])))
	{
		return true;
	}
	AddInfo(FString::Printf(TEXT("region dump: file %s, %d bytes (%d sectors of 4096)"), *Files[0], Region.Num(), Region.Num() / 4096));
	int32 Shown = 0, NumChunks = 0, MaxEnd = 0;
	for (int32 Slot = 0; Slot < 1024; ++Slot)
	{
		const int32 Offset = (Region[Slot * 4] << 16) | (Region[Slot * 4 + 1] << 8) | Region[Slot * 4 + 2];
		const int32 Count = Region[Slot * 4 + 3];
		if (Offset == 0 && Count == 0)
		{
			continue;
		}
		++NumChunks;
		MaxEnd = FMath::Max(MaxEnd, Offset + Count);
		const int32 Stamp = (Region[4096 + Slot * 4] << 24) | (Region[4096 + Slot * 4 + 1] << 16) | (Region[4096 + Slot * 4 + 2] << 8) | Region[4096 + Slot * 4 + 3];
		if (Shown < 4 && Offset * 4096 + 5 <= Region.Num())
		{
			++Shown;
			const int32 At = Offset * 4096;
			const int32 Length = (Region[At] << 24) | (Region[At + 1] << 16) | (Region[At + 2] << 8) | Region[At + 3];
			AddInfo(FString::Printf(TEXT("region dump: slot %d offset %d sectors %d stamp %d length %d type %d (length+4 -> %d sectors)"), Slot, Offset, Count, Stamp, Length, Region[At + 4], (Length + 4 + 4095) / 4096));
		}
	}
	AddInfo(FString::Printf(TEXT("region dump: %d chunks, last sector used ends at %d"), NumChunks, MaxEnd));
	int32 Looked = 0;
	for (int32 Slot = 0; Slot < 1024 && Looked < 3; ++Slot)
	{
		TArray<uint8> Bytes;
		bool bMissing = false;
		FMcNbt Root;
		if (!McJavaImport::ReadChunkBytes(Region, Slot % 32, Slot / 32, Folder, 0, 0, Bytes, bMissing) || !FMcNbt::Parse(Bytes, Root))
		{
			continue;
		}
		++Looked;
		FString Keys;
		for (int32 Index = 0; Index < Root.Keys.Num(); ++Index)
		{
			const FMcNbt& Value = Root.Values[Index];
			Keys += FString::Printf(TEXT("%s(t%d%s) "), *Root.Keys[Index], static_cast<int32>(Value.Type), Value.Type == FMcNbt::EType::Int || Value.Type == FMcNbt::EType::Long ? *FString::Printf(TEXT("=%lld"), Value.Integer) : Value.Type == FMcNbt::EType::String ? *(TEXT("=") + Value.String) : TEXT(""));
		}
		AddInfo(FString::Printf(TEXT("region dump chunk %d keys: %s"), Slot, *Keys));
		if (const FMcNbt* List = Root.Find(TEXT("sections")); List && !List->Values.IsEmpty())
		{
			FString SectionKeys;
			for (int32 Index = 0; Index < List->Values[0].Keys.Num(); ++Index)
			{
				SectionKeys += List->Values[0].Keys[Index] + TEXT(" ");
			}
			AddInfo(FString::Printf(TEXT("region dump chunk %d first section keys: %s"), Slot, *SectionKeys));
		}
	}
	TestTrue(TEXT("chunks were found"), NumChunks > 0);
	return true;
}

#endif
