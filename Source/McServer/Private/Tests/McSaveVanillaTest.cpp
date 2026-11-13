#include "McJavaExport.h"
#include "McNbt.h"
#include "McWorldActor.h"
#include "McWorldSave.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Saving and loading worlds checked against vanilla 26.3, measured on 2026-11-13 from a vanilla run (ref.sh, script
// save_files.txt: `log files` lists the save's files with their times every 280 ticks) and from the ref saves.
namespace
{
	struct FTempFolder
	{
		FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Tests") / (TEXT("McSaveVanilla_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
		~FTempFolder() { IFileManager::Get().DeleteDirectory(*Path, /*RequireExists=*/false, /*Tree=*/true); }
	};
	// A compound's child by key, matching the case: FMcNbt::Find is case-insensitive, and level.dat has both `Version` (a
	// compound) and `version` (an int).
	const FMcNbt* FindExact(const FMcNbt& Compound, const TCHAR* Key)
	{
		for (int32 Index = 0; Index < Compound.Keys.Num() && Index < Compound.Values.Num(); ++Index)
		{
			if (Compound.Keys[Index].Equals(Key, ESearchCase::CaseSensitive))
			{
				return &Compound.Values[Index];
			}
		}
		return nullptr;
	}
	// The vanilla reference save: on this PC under E:, on the 1070 under C:.
	FString GetVanillaRefSave()
	{
		return IFileManager::Get().DirectoryExists(TEXT("E:/mc-reference/run/saves/ref")) ? FString(TEXT("E:/mc-reference/run/saves/ref")) : FString(TEXT("C:/mc-reference/run/saves/ref"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSaveIntervalTest, "McClient.McServer.SaveVsVanilla.AutosaveInterval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSaveIntervalTest::RunTest(const FString& Parameters)
{
	// Vanilla: in a 342 s listing (every 280 ticks) only one batch of level.dat, data/*.dat, per-dimension raids and
	// chunk_tickets, the entities region files and the player's files was written, between game ticks 525452 and
	// 525733; the singleplayer server saves every 6000 ticks (five minutes at 20 a second).
	TestEqual(TEXT("the autosave comes every 6000 ticks"), AMcWorldActor::AutosaveIntervalTicks, 6000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSaveOrderTest, "McClient.McServer.SaveVsVanilla.QuitOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSaveOrderTest::RunTest(const FString& Parameters)
{
	// Vanilla's file times after a quit, in order: the player's data, stats and advancements, the region files, the
	// dimensions' raids and chunk_tickets with the entities files, then level.dat (451 to 452 bytes) last of all.
	const TConstArrayView<McWorldSave::ESaveStep> Order = McWorldSave::GetQuitSaveOrder();
	if (!TestEqual(TEXT("four steps"), Order.Num(), 4))
	{
		return false;
	}
	TestTrue(TEXT("the players come first"), Order[0] == McWorldSave::ESaveStep::Players);
	TestTrue(TEXT("then the chunks"), Order[1] == McWorldSave::ESaveStep::Chunks);
	TestTrue(TEXT("the level file is the last thing written"), Order[Order.Num() - 1] == McWorldSave::ESaveStep::Level);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSessionLockTest, "McClient.McServer.SaveVsVanilla.SessionLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSessionLockTest::RunTest(const FString& Parameters)
{
	FTempFolder Folder;
	TestTrue(TEXT("the lock is written"), McWorldSave::WriteSessionLock(Folder.Path));
	TArray<uint8> Ours;
	TestTrue(TEXT("and read back"), FFileHelper::LoadFileToArray(Ours, *(Folder.Path / TEXT("session.lock"))));
	TestEqual(TEXT("three bytes"), Ours.Num(), 3);
	if (Ours.Num() == 3)
	{
		TestTrue(TEXT("a snowman, U+2603 in UTF-8"), Ours[0] == 0xE2 && Ours[1] == 0x98 && Ours[2] == 0x83);
	}
	// And the same bytes as in a save the game wrote.
	TArray<uint8> Vanilla;
	if (FFileHelper::LoadFileToArray(Vanilla, *(GetVanillaRefSave() / TEXT("session.lock"))))
	{
		TestTrue(TEXT("the same as the vanilla save's session.lock"), Vanilla == Ours);
	}
	else
	{
		AddInfo(TEXT("no vanilla ref save on this machine; compared with the observed bytes only"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLevelDatVanillaTest, "McClient.McServer.SaveVsVanilla.LevelDat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLevelDatVanillaTest::RunTest(const FString& Parameters)
{
	// A world with a level file and no chunks, exported: only level.dat and the data files come out.
	FTempFolder Folder;
	const FString World = Folder.Path / TEXT("world");
	const FString Out = Folder.Path / TEXT("out");
	FMcLevelData Level;
	Level.Name = TEXT("Vanilla Check");
	Level.Seed = 77;
	if (!TestTrue(TEXT("the level is saved"), McWorldSave::SaveLevel(World, Level)))
	{
		return false;
	}
	McJavaExport::FStats Stats;
	if (!TestTrue(TEXT("the export runs"), McJavaExport::ExportWorld(World, Out, Stats, false)))
	{
		return false;
	}
	FMcNbt Ours;
	if (!TestTrue(TEXT("level.dat reads back"), FMcNbt::LoadFile(Out / TEXT("level.dat"), Ours)))
	{
		return false;
	}
	const FMcNbt* OurData = Ours.Find(TEXT("Data"));
	if (!TestNotNull(TEXT("with a Data compound"), OurData))
	{
		return false;
	}
	TestEqual(TEXT("the level's name is kept"), OurData->GetString(TEXT("LevelName")), FString(TEXT("Vanilla Check")));
	FMcNbt Vanilla;
	if (!FMcNbt::LoadFile(GetVanillaRefSave() / TEXT("level.dat"), Vanilla) || !Vanilla.Find(TEXT("Data")))
	{
		AddInfo(TEXT("no vanilla ref save on this machine; skipped"));
		return true;
	}
	const FMcNbt& VanillaData = *Vanilla.Find(TEXT("Data"));
	// Nothing the port writes is a field vanilla does not have, and the fixed ones agree.
	for (const FString& Key : OurData->Keys)
	{
		TestNotNull(*FString::Printf(TEXT("vanilla's level.dat has the field %s"), *Key), VanillaData.Find(*Key));
	}
	for (const TCHAR* Key : { TEXT("DataVersion"), TEXT("version"), TEXT("initialized"), TEXT("allowCommands") })
	{
		const FMcNbt* Mine = FindExact(*OurData, Key);
		const FMcNbt* Theirs = FindExact(VanillaData, Key);
		if (TestNotNull(*FString::Printf(TEXT("we write %s"), Key), Mine) && TestNotNull(*FString::Printf(TEXT("vanilla has %s"), Key), Theirs))
		{
			TestEqual(*FString::Printf(TEXT("%s is vanilla's"), Key), Mine->Integer, Theirs->Integer);
		}
	}
	const FMcNbt* OurVersion = FindExact(*OurData, TEXT("Version"));
	const FMcNbt* VanillaVersion = FindExact(VanillaData, TEXT("Version"));
	if (TestNotNull(TEXT("a Version compound"), OurVersion) && TestNotNull(TEXT("in vanilla's too"), VanillaVersion))
	{
		TestEqual(TEXT("the game version's id"), OurVersion->GetInt(TEXT("Id")), VanillaVersion->GetInt(TEXT("Id")));
		TestEqual(TEXT("name"), OurVersion->GetString(TEXT("Name")), VanillaVersion->GetString(TEXT("Name")));
		TestEqual(TEXT("series"), OurVersion->GetString(TEXT("Series")), VanillaVersion->GetString(TEXT("Series")));
	}
	// The time of day is in world_clocks.dat, not level.dat.
	TestTrue(TEXT("world_clocks.dat is written"), IFileManager::Get().FileExists(*(Out / TEXT("data") / TEXT("minecraft") / TEXT("world_clocks.dat"))));
	return true;
}

#endif
