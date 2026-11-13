#include "McJavaExport.h"
#include "McJavaImport.h"
#include "McNbt.h"
#include "McWorldSave.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// A world's players written as the game's player files, and what the game's own files hold (the ref saves).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPlayerExportTest, "McClient.McServer.JavaExport.Players",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPlayerExportTest::RunTest(const FString& Parameters)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Tests") / (TEXT("McPlayerExport_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString World = Folder / TEXT("world");
	const FString Out = Folder / TEXT("out");

	// One player of ours, with a hotbar stone, boots, the off hand, an ender chest item and a recipe book.
	FMcPlayerData Player;
	Player.Position = FVector(12.5, 70.0, -3.5);
	Player.Yaw = 90.0f;
	Player.Pitch = 15.0f;
	Player.bHasSurvival = true;
	Player.bCreative = true;
	Player.SelectedSlot = 4;
	Player.Stats.Add(TEXT("health"), 14.0);
	Player.Stats.Add(TEXT("food"), 12.0);
	Player.Stats.Add(TEXT("xp_level"), 7.0);
	Player.Stats.Add(TEXT("advancement:minecraft:story/mine_stone"), 1.0);
	Player.Stats.Add(TEXT("criterion:minecraft:adventure/adventuring_time/minecraft:plains"), 1.0);
	Player.Stats.Add(TEXT("stat:custom:play_time"), 307.0);
	Player.Stats.Add(TEXT("stat:crafted:minecraft:stick"), 4.0);
	Player.Stats.Add(TEXT("recipe:minecraft:stick"), 1.0);
	Player.Stats.Add(TEXT("recipe:minecraft:furnace"), 2.0);
	Player.Slots.Add({ 3, FMcItemStack(FName(TEXT("minecraft:stone")), 20) });
	Player.Slots.Add({ 100, FMcItemStack(FName(TEXT("minecraft:iron_boots")), 1) });
	Player.Slots.Add({ 150, FMcItemStack(FName(TEXT("minecraft:shield")), 1) });
	Player.Slots.Add({ 203, FMcItemStack(FName(TEXT("minecraft:diamond")), 9) });
	FMcLevelData Level;
	Level.Seed = 3;
	bool bOk = McWorldSave::SaveLevel(World, Level) && McWorldSave::SavePlayer(World, TEXT("Steve"), Player);
	McJavaExport::FStats Stats;
	bOk = bOk && McJavaExport::ExportWorld(World, Out, Stats, false);
	if (!TestTrue(TEXT("the export runs"), bOk))
	{
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
		return false;
	}
	TestEqual(TEXT("one player written"), Stats.Players, 1);

	// Named by the UUID a world without accounts gives that name.
	const FString Uuid = McJavaExport::OfflineUuid(TEXT("Steve"));
	TestEqual(TEXT("a version 3 uuid"), Uuid.Mid(14, 1), FString(TEXT("3")));
	FMcNbt File;
	if (!TestTrue(TEXT("the player file reads back"), FMcNbt::LoadFile(Out / TEXT("players/data") / (Uuid + TEXT(".dat")), File)))
	{
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
		return false;
	}
	TestNotNull(TEXT("with a UUID"), File.Find(TEXT("UUID")));
	// Advancements and statistics, in the game's files.
	FString Text;
	TSharedPtr<FJsonObject> Advancements, StatsFile;
	if (TestTrue(TEXT("the advancements file is there"), FFileHelper::LoadFileToString(Text, *(Out / TEXT("players/advancements") / (Uuid + TEXT(".json")))))
		&& TestTrue(TEXT("and is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Advancements) && Advancements.IsValid()))
	{
		const TSharedPtr<FJsonObject>* Made = nullptr;
		if (TestTrue(TEXT("Stone Age is in it"), Advancements->TryGetObjectField(TEXT("minecraft:story/mine_stone"), Made)))
		{
			TestTrue(TEXT("done"), (*Made)->GetBoolField(TEXT("done")));
			TestTrue(TEXT("with its criterion"), (*Made)->GetObjectField(TEXT("criteria"))->HasField(TEXT("get_stone")) || (*Made)->GetObjectField(TEXT("criteria"))->Values.Num() > 0);
		}
		const TSharedPtr<FJsonObject>* Begun = nullptr;
		if (TestTrue(TEXT("a begun advancement is in it"), Advancements->TryGetObjectField(TEXT("minecraft:adventure/adventuring_time"), Begun)))
		{
			TestFalse(TEXT("not done"), (*Begun)->GetBoolField(TEXT("done")));
			TestTrue(TEXT("with the biome it has"), (*Begun)->GetObjectField(TEXT("criteria"))->HasField(TEXT("minecraft:plains")));
		}
		TestEqual(TEXT("with the data version"), static_cast<int32>(Advancements->GetNumberField(TEXT("DataVersion"))), McJavaExport::DataVersion);
	}
	if (TestTrue(TEXT("the statistics file is there"), FFileHelper::LoadFileToString(Text, *(Out / TEXT("players/stats") / (Uuid + TEXT(".json")))))
		&& TestTrue(TEXT("and is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), StatsFile) && StatsFile.IsValid()))
	{
		const TSharedPtr<FJsonObject>* All = nullptr;
		const TSharedPtr<FJsonObject>* Custom = nullptr;
		const TSharedPtr<FJsonObject>* Crafted = nullptr;
		if (TestTrue(TEXT("a stats compound"), StatsFile->TryGetObjectField(TEXT("stats"), All)))
		{
			if (TestTrue(TEXT("custom"), (*All)->TryGetObjectField(TEXT("minecraft:custom"), Custom)))
			{
				TestEqual(TEXT("play time"), static_cast<int32>((*Custom)->GetNumberField(TEXT("minecraft:play_time"))), 307);
			}
			if (TestTrue(TEXT("crafted"), (*All)->TryGetObjectField(TEXT("minecraft:crafted"), Crafted)))
			{
				TestEqual(TEXT("sticks"), static_cast<int32>((*Crafted)->GetNumberField(TEXT("minecraft:stick"))), 4);
			}
		}
	}
	const FMcNbt* Book = File.Find(TEXT("recipeBook"));
	if (TestNotNull(TEXT("with a recipe book"), Book) && TestNotNull(TEXT("recipes"), Book->Find(TEXT("recipes"))) && TestNotNull(TEXT("toBeDisplayed"), Book->Find(TEXT("toBeDisplayed"))))
	{
		TestEqual(TEXT("both recipes"), Book->Find(TEXT("recipes"))->Num(), 2);
		TestEqual(TEXT("one still to be shown"), Book->Find(TEXT("toBeDisplayed"))->Num(), 1);
		TestEqual(TEXT("the furnace"), Book->Find(TEXT("toBeDisplayed"))->Values[0].String, FString(TEXT("minecraft:furnace")));
	}
	TestNotNull(TEXT("the worn boots are under equipment"), File.Find(TEXT("equipment")) ? File.Find(TEXT("equipment"))->Find(TEXT("feet")) : nullptr);

	// What we read back from it is the player (the import's own reading of the game's file).
	FMcPlayerData Back;
	if (TestTrue(TEXT("it converts"), McJavaImport::ConvertPlayer(File, Back)))
	{
		TestEqual(TEXT("x"), Back.Position.X, 12.5);
		TestEqual(TEXT("yaw"), Back.Yaw, 90.0f);
		TestTrue(TEXT("creative"), Back.bCreative);
		TestEqual(TEXT("selected slot"), Back.SelectedSlot, 4);
		TestEqual(TEXT("health"), Back.Stats.FindRef(TEXT("health")), 14.0);
		TestEqual(TEXT("food"), Back.Stats.FindRef(TEXT("food")), 12.0);
		TestEqual(TEXT("level"), Back.Stats.FindRef(TEXT("xp_level")), 7.0);
		TestEqual(TEXT("a shown recipe"), Back.Stats.FindRef(TEXT("recipe:minecraft:stick")), 1.0);
		TestEqual(TEXT("a recipe to be shown"), Back.Stats.FindRef(TEXT("recipe:minecraft:furnace")), 2.0);
		TestEqual(TEXT("the same slots"), Back.Slots.Num(), 4);
		for (const FMcSavedSlot& Saved : Player.Slots)
		{
			const FMcSavedSlot* Found = Back.Slots.FindByPredicate([&Saved](const FMcSavedSlot& Other) { return Other.Slot == Saved.Slot; });
			if (TestNotNull(*FString::Printf(TEXT("slot %d is back"), Saved.Slot), Found))
			{
				TestEqual(*FString::Printf(TEXT("slot %d item"), Saved.Slot), Found->Stack.Item, Saved.Stack.Item);
				TestEqual(*FString::Printf(TEXT("slot %d count"), Saved.Slot), Found->Stack.Count, Saved.Stack.Count);
			}
		}
	}

	// A player imported from a Java save keeps the file name they had.
	FMcPlayerData Imported = Player;
	Imported.Uuid = TEXT("12345678-1234-3234-8234-1234567890ab");
	McWorldSave::SavePlayer(World, TEXT("imported-player"), Imported);
	McJavaExport::FStats Again;
	TestTrue(TEXT("exports again"), McJavaExport::ExportWorld(World, Out, Again, false));
	TestEqual(TEXT("two players now"), Again.Players, 2);
	TestTrue(TEXT("the imported one under its own id"), IFileManager::Get().FileExists(*(Out / TEXT("players/data/12345678-1234-3234-8234-1234567890ab.dat"))));

	// Against the game's own files: every field we write is one it has, and its file name for "Player150" is our offline uuid.
	const TCHAR* Candidates[] = { TEXT("E:/mc-reference/run/saves/importtest2"), TEXT("C:/mc-reference/run/saves/importtest2") };
	for (const TCHAR* Save : Candidates)
	{
		if (!IFileManager::Get().DirectoryExists(Save))
		{
			continue;
		}
		const FString Named = FString(Save) / TEXT("players/data") / (McJavaExport::OfflineUuid(TEXT("Player150")) + TEXT(".dat"));
		TestTrue(TEXT("vanilla's file for Player150 has our offline uuid as its name"), IFileManager::Get().FileExists(*Named));
		FMcNbt Vanilla;
		if (FMcNbt::LoadFile(Named, Vanilla))
		{
			for (const FString& Key : File.Keys)
			{
				if (Key == TEXT("equipment"))
				{
					continue;
				}
				TestNotNull(*FString::Printf(TEXT("vanilla's player file has %s"), *Key), Vanilla.Find(*Key));
			}
		}
		break;
	}
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif
