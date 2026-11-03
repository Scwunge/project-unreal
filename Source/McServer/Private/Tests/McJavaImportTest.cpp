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
#include "McJavaImport.h"
#include "McFurnace.h"
#include "McItemRegistry.h"
#include "McNbt.h"
#include "McWorldSave.h"
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

#endif
