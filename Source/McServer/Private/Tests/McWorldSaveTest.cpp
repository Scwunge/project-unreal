#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McChunkSave.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McMapStore.h"
#include "McMob.h"
#include "McTnt.h"
#include "McRegionFile.h"
#include "McWorldSave.h"
#include "McWorldStorage.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// A folder of its own under Saved/, removed when the test is done.
	struct FTempFolder
	{
		FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Tests") / (TEXT("McWorldSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		~FTempFolder() { IFileManager::Get().DeleteDirectory(*Path, /*RequireExists=*/false, /*Tree=*/true); }
	};

	TArray<uint8> MakeBytes(int32 Num, uint8 Seed)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Bytes[Index] = static_cast<uint8>(Seed + Index * 7);
		}
		return Bytes;
	}

	bool LoadSaveRegistries()
	{
		return FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded();
	}

	// A chunk with some terrain, a variety of states, a biome, and a chest holding something.
	TUniquePtr<FMcChunk> MakeEditedChunk(FMcBlockWorld& World, FIntPoint Pos)
	{
		const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
		TUniquePtr<FMcChunk> Chunk = World.MakeChunk(Pos);
		const FMcStateId Stone = Blocks.ParseState(TEXT("minecraft:stone")).Get(0);
		const FMcStateId Stairs = Blocks.ParseState(TEXT("minecraft:oak_stairs[facing=east,half=top]")).Get(0);
		const FMcStateId ChestState = Blocks.ParseState(TEXT("minecraft:chest")).Get(0);
		Chunk->FillLayer(World.GetDimension().MinY, Stone);
		Chunk->FillLayer(World.GetDimension().MinY + 1, Stone);
		Chunk->SetBlock(3, 70, 4, ChestState);
		Chunk->SetBlock(5, 71, 6, Stairs);
		Chunk->SetBlock(15, 200, 15, Stone);
		FMcBlockEntity Chest = FMcBlockEntity::MakeContainer(27);
		Chest.Slots[0] = FMcItemStack(TEXT("minecraft:diamond"), 5);
		Chest.LootTable = TEXT("minecraft:chests/simple_dungeon");
		Chest.LootSeed = 123456789012345LL;
		Chunk->SetBlockEntity(3, 70, 4, Chest);
		Chunk->SetBiomeCell(1, 2, 3, FMcBiomeRegistry::Get().Find(TEXT("minecraft:desert")).Get(0));
		return Chunk;
	}

	bool SameChunk(const FMcChunk& A, const FMcChunk& B)
	{
		const TConstArrayView<FMcBiomeId> BiomesA = A.GetBiomes(), BiomesB = B.GetBiomes();
		if (A.NumSections() != B.NumSections() || BiomesA.Num() != BiomesB.Num()
			|| FMemory::Memcmp(BiomesA.GetData(), BiomesB.GetData(), BiomesA.Num()) != 0
			|| A.GetBlockEntities().Num() != B.GetBlockEntities().Num())
		{
			return false;
		}
		for (int32 Y = A.GetMinY(); Y <= A.GetMaxY(); ++Y)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					if (A.GetBlock(X, Y, Z) != B.GetBlock(X, Y, Z))
					{
						return false;
					}
				}
			}
		}
		for (const TPair<FIntVector, FMcBlockEntity>& Entry : A.GetBlockEntities())
		{
			const FMcBlockEntity* Other = B.FindBlockEntity(Entry.Key.X, Entry.Key.Y, Entry.Key.Z);
			if (!Other || !(*Other == Entry.Value))
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRegionFileTest, "McClient.McServer.SaveRegionFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcRegionFileTest::RunTest(const FString& Parameters)
{
	// Damage and junk are logged as errors, which is right; the test makes some on purpose.
	AddExpectedError(TEXT("is damaged; it will be generated again"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("isn't a region file this version can read"), EAutomationExpectedErrorFlags::Contains, 0);
	FTempFolder Folder;
	const FString Path = Folder.Path / FMcRegionFile::FileNameOf(FIntPoint(-1, 0));
	// Chunks of region (-1, 0): x from -32 to -1, z from 0 to 31. Negative coordinates must land in the right slots.
	const FIntPoint A(-1, 0), B(-32, 31), C(-17, 5);
	const TArray<uint8> BytesA = MakeBytes(100, 1), BytesB = MakeBytes(9000, 2), BytesC = MakeBytes(4096, 3);
	TestEqual(TEXT("region of a negative chunk"), FMcRegionFile::RegionOf(A), FIntPoint(-1, 0));
	TestEqual(TEXT("region of the first chunk past it"), FMcRegionFile::RegionOf(FIntPoint(0, 32)), FIntPoint(0, 1));
	{
		FMcRegionFile File(Path);
		TArray<uint8> Out;
		TestFalse(TEXT("a file that doesn't exist has nothing"), File.Read(A, Out));
		TestTrue(TEXT("write a"), File.Write(A, BytesA));
		TestTrue(TEXT("write b"), File.Write(B, BytesB));
		TestTrue(TEXT("write c"), File.Write(C, BytesC));
		TestTrue(TEXT("read a"), File.Read(A, Out) && Out == BytesA);
		TestTrue(TEXT("read b"), File.Read(B, Out) && Out == BytesB);
		TestTrue(TEXT("read c"), File.Read(C, Out) && Out == BytesC);
		TestFalse(TEXT("a chunk never written"), File.Contains(FIntPoint(-2, 0)));
		TestEqual(TEXT("three chunks"), File.NumChunks(), 3);

		// Smaller or the same size is written in place; bigger moves to the end. Neither disturbs the others.
		const TArray<uint8> SmallerB = MakeBytes(5000, 9), BiggerA = MakeBytes(20000, 8);
		TestTrue(TEXT("rewrite b smaller"), File.Write(B, SmallerB));
		TestTrue(TEXT("rewrite a bigger"), File.Write(A, BiggerA));
		TestTrue(TEXT("b is the new bytes"), File.Read(B, Out) && Out == SmallerB);
		TestTrue(TEXT("a is the new bytes"), File.Read(A, Out) && Out == BiggerA);
		TestTrue(TEXT("c is untouched"), File.Read(C, Out) && Out == BytesC);
		TestEqual(TEXT("still three chunks"), File.NumChunks(), 3);
		TestFalse(TEXT("empty payloads aren't written"), File.Write(A, TArray<uint8>()));
	}
	{
		// A new object over the same file finds it all, which is what a restart does.
		FMcRegionFile Reopened(Path);
		TArray<uint8> Out;
		TestEqual(TEXT("reopened: three chunks"), Reopened.NumChunks(), 3);
		TestTrue(TEXT("reopened: c"), Reopened.Read(C, Out) && Out == BytesC);
		TestTrue(TEXT("reopened: a"), Reopened.Read(A, Out) && Out == MakeBytes(20000, 8));
	}

	// A flipped byte in a payload is noticed rather than loaded.
	const FString DamagedPath = Folder.Path / FMcRegionFile::FileNameOf(FIntPoint(2, 2));
	const FIntPoint First(64, 64), Second(65, 64);
	{
		FMcRegionFile File(DamagedPath);
		File.Write(First, BytesA);
		File.Write(Second, BytesC);
	}
	TArray<uint8> Whole;
	if (TestTrue(TEXT("file can be read back"), FFileHelper::LoadFileToArray(Whole, *DamagedPath)))
	{
		// The first chunk written is in the first payload sector.
		Whole[FMcRegionFile::HeaderSectors * FMcRegionFile::SectorSize] ^= 0xFF;
		FFileHelper::SaveArrayToFile(Whole, *DamagedPath);
		FMcRegionFile Damaged(DamagedPath);
		TArray<uint8> Out;
		TestFalse(TEXT("damaged payload is refused"), Damaged.Read(First, Out));
		TestTrue(TEXT("its neighbour is fine"), Damaged.Read(Second, Out) && Out == BytesC);
	}

	// A file that isn't ours is set aside, not written over.
	const FString Junk = Folder.Path / FMcRegionFile::FileNameOf(FIntPoint(5, 5));
	FFileHelper::SaveArrayToFile(MakeBytes(30000, 4), *Junk);
	{
		FMcRegionFile File(Junk);
		TestFalse(TEXT("junk has no chunks"), File.Contains(FIntPoint(160, 160)));
		TestTrue(TEXT("a new file can be started"), File.Write(FIntPoint(160, 160), BytesA));
		TArray<uint8> Out;
		TestTrue(TEXT("and read"), File.Read(FIntPoint(160, 160), Out) && Out == BytesA);
	}
	TestTrue(TEXT("the junk was kept"), IFileManager::Get().FileExists(*(Junk + TEXT(".bad"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcChunkSaveTest, "McClient.McServer.SaveChunkCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcChunkSaveTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadSaveRegistries()))
	{
		return false;
	}
	// The cut-off save below makes zlib complain, as it should.
	AddExpectedError(TEXT("Failed to uncompress memory"), EAutomationExpectedErrorFlags::Contains, 0);
	FMcBlockWorld World{FMcDimensionType()};
	const TUniquePtr<FMcChunk> Original = MakeEditedChunk(World, FIntPoint(-3, 7));
	FMcChunkSaveExtras Extras;
	Extras.bPopulated = true;
	Extras.Entities = MakeBytes(50, 6);

	const TArray<uint8> Bytes = McChunkSave::Encode(*Original, Extras);
	if (!TestFalse(TEXT("it encodes"), Bytes.IsEmpty()))
	{
		return false;
	}
	TUniquePtr<FMcChunk> Loaded = World.MakeChunk(FIntPoint(-3, 7));
	FMcChunkSaveExtras LoadedExtras;
	TestTrue(TEXT("it decodes"), McChunkSave::Decode(Bytes, *Loaded, LoadedExtras));
	TestTrue(TEXT("blocks, biomes and block entities come back"), SameChunk(*Original, *Loaded));
	TestTrue(TEXT("populated"), LoadedExtras.bPopulated);
	TestTrue(TEXT("entity bytes"), LoadedExtras.Entities == Extras.Entities);

	const FMcBlockEntity* Chest = Loaded->FindBlockEntity(3, 70, 4);
	if (TestNotNull(TEXT("the chest's block entity"), Chest) && Chest->Slots.Num() > 0)
	{
		TestEqual(TEXT("its diamonds"), Chest->Slots[0].Count, 5);
		TestEqual(TEXT("its loot table"), Chest->LootTable, FName(TEXT("minecraft:chests/simple_dungeon")));
		TestEqual(TEXT("its loot seed"), Chest->LootSeed, 123456789012345LL);
	}
	TestEqual(TEXT("the stairs keep their properties"),
		FMcBlockRegistry::Get().Describe(Loaded->GetBlock(5, 71, 6)), FMcBlockRegistry::Get().Describe(Original->GetBlock(5, 71, 6)));

	// Damage is refused, and a chunk it was refused for is left as it was.
	TUniquePtr<FMcChunk> Untouched = World.MakeChunk(FIntPoint(-3, 7));
	FMcChunkSaveExtras Ignored;
	TArray<uint8> Cut(Bytes.GetData(), Bytes.Num() / 2);
	TestFalse(TEXT("a cut-off save"), McChunkSave::Decode(Cut, *Untouched, Ignored));
	TArray<uint8> WrongVersion = Bytes;
	WrongVersion[0] = 99;
	TestFalse(TEXT("an unknown version"), McChunkSave::Decode(WrongVersion, *Untouched, Ignored));
	TestFalse(TEXT("nothing"), McChunkSave::Decode(TArray<uint8>(), *Untouched, Ignored));
	TestEqual(TEXT("still empty"), Untouched->GetBlock(3, 70, 4), static_cast<FMcStateId>(0));

	// A chunk of another height isn't loaded into this one.
	FMcDimensionType Short;
	Short.Height = 64;
	FMcBlockWorld ShortWorld{Short};
	TUniquePtr<FMcChunk> Mismatch = ShortWorld.MakeChunk(FIntPoint(-3, 7));
	TestFalse(TEXT("a different height"), McChunkSave::Decode(Bytes, *Mismatch, Ignored));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorldStorageTest, "McClient.McServer.SaveUnloadReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWorldStorageTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadSaveRegistries()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	FTempFolder Folder;
	const FIntPoint Near(2, -3), Far(40, 41);
	const FMcStateId Gold = FMcBlockRegistry::Get().ParseState(TEXT("minecraft:gold_block")).Get(0);

	// A chunk is edited, saved and dropped, as it is when the player walks away; then it's loaded again.
	TUniquePtr<FMcChunk> Original = MakeEditedChunk(World, Near);
	Original->SetBlock(7, 100, 8, Gold);
	{
		FMcWorldStorage Storage(Folder.Path / TEXT("overworld"));
		TArray<uint8> Bytes;
		TestFalse(TEXT("nothing saved yet"), Storage.Load(Near, Bytes));
		FMcChunkSaveExtras Extras;
		Extras.bPopulated = true;
		Storage.QueueSave(Near, McChunkSave::Encode(*Original, Extras));
		// Straight back, before the writer has necessarily got to it: served from the queue.
		TestTrue(TEXT("loads while queued"), Storage.Load(Near, Bytes));
		TUniquePtr<FMcChunk> Early = World.MakeChunk(Near);
		FMcChunkSaveExtras EarlyExtras;
		TestTrue(TEXT("and decodes"), McChunkSave::Decode(Bytes, *Early, EarlyExtras) && SameChunk(*Original, *Early));

		// Edited again and saved again: the newest wins.
		Original->SetBlock(7, 100, 8, FMcStateId(0));
		Original->SetBlock(9, 101, 9, Gold);
		Storage.QueueSave(Near, McChunkSave::Encode(*Original, Extras));
		Storage.QueueSave(Far, McChunkSave::Encode(*MakeEditedChunk(World, Far), Extras));
		Storage.Flush();
		TestEqual(TEXT("everything written"), Storage.NumQueued(), 0);
		TestEqual(TEXT("no write failed"), Storage.NumFailedWrites(), 0);
	}
	{
		// A new storage over the folder, as after a restart.
		FMcWorldStorage Storage(Folder.Path / TEXT("overworld"));
		TArray<uint8> Bytes;
		if (!TestTrue(TEXT("the saved chunk is found"), Storage.Load(Near, Bytes)))
		{
			return false;
		}
		TUniquePtr<FMcChunk> Reloaded = World.MakeChunk(Near);
		FMcChunkSaveExtras Extras;
		TestTrue(TEXT("it decodes"), McChunkSave::Decode(Bytes, *Reloaded, Extras));
		TestTrue(TEXT("with all its edits"), SameChunk(*Original, *Reloaded));
		TestEqual(TEXT("the newer edit"), Reloaded->GetBlock(9, 101, 9), Gold);
		TestEqual(TEXT("the older edit undone"), Reloaded->GetBlock(7, 100, 8), static_cast<FMcStateId>(0));
		TestTrue(TEXT("a far chunk, in another region file, too"), Storage.Contains(Far));
		TestFalse(TEXT("a chunk never saved"), Storage.Contains(FIntPoint(0, 0)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMapsFileTest, "McClient.McServer.SaveMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcMapsFileTest::RunTest(const FString& Parameters)
{
	FTempFolder Folder;
	McMapStore::Reset();
	const int32 Id = McMapStore::Create(1, 3040, 0, TEXT("minecraft:overworld"));
	McMapStore::Find(Id)->Colors[5] = 123;
	McMapStore::Lock(Id);
	TestTrue(TEXT("the maps are written"), McWorldSave::SaveMaps(Folder.Path));
	McMapStore::Reset();
	TestEqual(TEXT("the store is empty"), McMapStore::Num(), 0);
	TestTrue(TEXT("and read back"), McWorldSave::LoadMaps(Folder.Path));
	const FMcMapData* Map = McMapStore::Find(Id);
	TestNotNull(TEXT("with the same id"), Map);
	if (Map)
	{
		TestEqual(TEXT("scale"), Map->Scale, 1);
		TestEqual(TEXT("a colour"), static_cast<int32>(Map->Colors[5]), 123);
		TestTrue(TEXT("locked"), Map->bLocked);
	}
	// A world with no maps file starts with none.
	FTempFolder Other;
	TestFalse(TEXT("no file"), McWorldSave::LoadMaps(Other.Path));
	TestEqual(TEXT("an empty store"), McMapStore::Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLevelFileTest, "McClient.McServer.SaveLevelFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLevelFileTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("isn't valid JSON"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("has no usable version and seed"), EAutomationExpectedErrorFlags::Contains, 1);
	FTempFolder Folder;
	FMcLevelData Level;
	// Over 2^53, which a JSON number can't hold exactly.
	Level.Seed = -9007199254740993LL;
	Level.Spawn = FIntVector(-12, 80, 345);
	Level.DayTime = 123456.5;
	Level.Dimension = TEXT("minecraft:the_nether");
	Level.NoiseSettings = TEXT("minecraft:nether");
	Level.bFlat = true;
	Level.FlatPreset = TEXT("minecraft:classic_flat");
	Level.GameRules.Add(TEXT("doDaylightCycle"), TEXT("false"));
	Level.GameRules.Add(TEXT("randomTickSpeed"), TEXT("3"));

	FMcLevelData Loaded;
	TestFalse(TEXT("no level file yet"), McWorldSave::LoadLevel(Folder.Path, Loaded));
	TestTrue(TEXT("saves"), McWorldSave::SaveLevel(Folder.Path, Level));
	TestTrue(TEXT("loads"), McWorldSave::LoadLevel(Folder.Path, Loaded));
	TestTrue(TEXT("round-trips"), Loaded == Level);
	TestEqual(TEXT("the seed exactly"), Loaded.Seed, Level.Seed);

	// Saving again replaces it.
	Level.DayTime = 5.0;
	TestTrue(TEXT("saves again"), McWorldSave::SaveLevel(Folder.Path, Level));
	TestTrue(TEXT("loads again"), McWorldSave::LoadLevel(Folder.Path, Loaded) && Loaded == Level);
	TestFalse(TEXT("no temporary file is left"), IFileManager::Get().FileExists(*(Folder.Path / TEXT("level.json.tmp"))));

	// Text that isn't a level is refused.
	FFileHelper::SaveStringToFile(TEXT("{ not json"), *(Folder.Path / TEXT("level.json")));
	TestFalse(TEXT("broken JSON"), McWorldSave::LoadLevel(Folder.Path, Loaded));
	FFileHelper::SaveStringToFile(TEXT("{\"version\": 1}"), *(Folder.Path / TEXT("level.json")));
	TestFalse(TEXT("no seed"), McWorldSave::LoadLevel(Folder.Path, Loaded));

	FMcPlayerData Player;
	Player.Position = FVector(10.5, 64.0, -20.25);
	Player.Yaw = 135.5f;
	Player.Pitch = -30.0f;
	Player.Dimension = TEXT("minecraft:the_end");
	Player.bFlying = true;
	FMcPlayerData LoadedPlayer;
	TestFalse(TEXT("no player data yet"), McWorldSave::LoadPlayer(Folder.Path, TEXT("Steve"), LoadedPlayer));
	TestTrue(TEXT("saves a player"), McWorldSave::SavePlayer(Folder.Path, TEXT("Steve"), Player));
	TestTrue(TEXT("loads the player"), McWorldSave::LoadPlayer(Folder.Path, TEXT("Steve"), LoadedPlayer) && LoadedPlayer == Player);
	TestFalse(TEXT("another player has none"), McWorldSave::LoadPlayer(Folder.Path, TEXT("Alex"), LoadedPlayer));
	// An id with path characters can't leave the folder.
	TestTrue(TEXT("a hostile id is made safe"), McWorldSave::SavePlayer(Folder.Path, TEXT("../../evil"), Player));
	TestTrue(TEXT("and stays in playerdata"), IFileManager::Get().FileExists(*(Folder.Path / TEXT("playerdata") / TEXT(".._.._evil.json"))));

	TestEqual(TEXT("a plain name"), McWorldSave::SanitizeName(TEXT("My World-2")), FString(TEXT("My_World-2")));
	TestEqual(TEXT("path separators"), McWorldSave::SanitizeName(TEXT("a/b\\c:d")), FString(TEXT("a_b_c_d")));
	TestTrue(TEXT("dots alone are no name"), McWorldSave::SanitizeName(TEXT("..")).IsEmpty());
	TestTrue(TEXT("empty is no name"), McWorldSave::SanitizeName(TEXT("  ")).IsEmpty());
	TestEqual(TEXT("overworld folder"), McWorldSave::GetDimensionFolder(TEXT("minecraft:overworld")), FString(TEXT("overworld")));
	TestEqual(TEXT("another namespace"), McWorldSave::GetDimensionFolder(TEXT("mod:void")), FString(TEXT("mod_void")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEntitySaveTest, "McClient.McServer.SaveEntities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcEntitySaveTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadSaveRegistries()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	World.AddChunk(World.MakeChunk(FIntPoint(1, 0)));
	FMcEntityHost Host(World, 1);

	FMcMob* Pig = new FMcMob(*FMcMobType::Find(TEXT("minecraft:pig")));
	Pig->Body.Position = FVector(4.25, 70.0, 9.75);
	Pig->Body.Velocity = FVector(0.1, 0.0, -0.2);
	Pig->Yaw = 77.0f;
	Pig->Health = 6.5f;
	Pig->Age = 321;
	Host.GetManager().Add(TUniquePtr<FMcEntity>(Pig));
	FMcItemEntity& Drop = Host.GetManager().DropItem(FVector(12.5, 70.0, 3.5), FMcItemStack(TEXT("minecraft:iron_ingot"), 17), 33);
	Drop.Body.Velocity = FVector::ZeroVector;
	Host.GetManager().DropItem(FVector(20.5, 70.0, 3.5), FMcItemStack(TEXT("minecraft:stick"), 2));  // chunk 1, 0

	const TArray<uint8> Bytes = Host.CaptureChunk(FIntPoint(0, 0));
	TestFalse(TEXT("chunk 0, 0 has entities to save"), Bytes.IsEmpty());
	TestTrue(TEXT("an empty chunk has none"), Host.CaptureChunk(FIntPoint(5, 5)).IsEmpty());

	// Another host, as after a restart, gets them back.
	FMcEntityHost Fresh(World, 1);
	TestEqual(TEXT("two came back"), Fresh.RestoreChunk(Bytes), 2);
	TestEqual(TEXT("and only those"), Fresh.GetManager().GetEntities().Num(), 2);
	bool bPig = false, bIron = false;
	for (const TUniquePtr<FMcEntity>& Entity : Fresh.GetManager().GetEntities())
	{
		if (Entity->IsLiving())
		{
			const FMcMob& Mob = static_cast<const FMcMob&>(*Entity);
			bPig = Mob.GetType() == FName(TEXT("minecraft:pig")) && Mob.Body.Position.Equals(Pig->Body.Position)
				&& Mob.Body.Velocity.Equals(Pig->Body.Velocity) && Mob.Yaw == 77.0f && Mob.Health == 6.5f && Mob.Age == 321;
		}
		else if (Entity->GetType() == FMcItemEntity::StaticType())
		{
			const FMcItemEntity& Item = static_cast<const FMcItemEntity&>(*Entity);
			bIron = Item.Stack.Item == FName(TEXT("minecraft:iron_ingot")) && Item.Stack.Count == 17 && Item.PickupDelay == 33
				&& Item.Body.Position.Equals(Drop.Body.Position);
		}
	}
	TestTrue(TEXT("the pig is as it was"), bPig);
	TestTrue(TEXT("the iron is as it was"), bIron);

	// A cut-off save loses the damaged part but doesn't crash or restore half an entity.
	FMcEntityHost Cut(World, 1);
	const TArray<uint8> Half(Bytes.GetData(), Bytes.Num() - 20);
	TestTrue(TEXT("a damaged save restores at most what's whole"), Cut.RestoreChunk(Half) <= 1);

	// Entities outside every loaded chunk are the strays.
	FMcItemEntity& Lost = Host.GetManager().DropItem(FVector(500.5, 70.0, 500.5), FMcItemStack(TEXT("minecraft:stick"), 1));
	TestEqual(TEXT("one stray"), Host.RemoveStrays(), 1);
	TestTrue(TEXT("and it's the one outside"), Lost.bRemoved);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPrimedTntSaveTest, "McClient.McServer.SavePrimedTnt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPrimedTntSaveTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadSaveRegistries()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	FMcEntityHost Host(World, 1);
	// A TNT with 37 ticks of fuse left, a little way off the ground and moving, as a world saved mid-fuse holds it.
	FMcPrimedTnt* Lit = new FMcPrimedTnt();
	Lit->Fuse = 37;
	Lit->Body.Position = FVector(4.5, 70.5, 9.5);
	Lit->Body.Velocity = FVector(0.01, 0.1, -0.02);
	Host.GetManager().Add(TUniquePtr<FMcEntity>(Lit));
	const TArray<uint8> Bytes = Host.CaptureChunk(FIntPoint(0, 0));
	TestFalse(TEXT("a lit TNT is saved"), Bytes.IsEmpty());

	FMcEntityHost Fresh(World, 1);
	TestEqual(TEXT("it comes back"), Fresh.RestoreChunk(Bytes), 1);
	bool bSame = false;
	for (const TUniquePtr<FMcEntity>& Entity : Fresh.GetManager().GetEntities())
	{
		if (Entity->GetType() == FMcPrimedTnt::StaticType())
		{
			const FMcPrimedTnt& Back = static_cast<const FMcPrimedTnt&>(*Entity);
			bSame = Back.Fuse == 37 && Back.Body.Position.Equals(Lit->Body.Position) && Back.Body.Velocity.Equals(Lit->Body.Velocity);
		}
	}
	TestTrue(TEXT("with the same fuse, place and motion"), bSame);
	return true;
}

#endif
