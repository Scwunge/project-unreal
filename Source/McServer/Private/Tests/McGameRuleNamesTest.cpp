#include "McCache.h"
#include "McCommands.h"
#include "McGameRules.h"
#include "McJavaExport.h"
#include "McJavaImport.h"
#include "McNbt.h"
#include "McWorldSave.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// The game rules are named as 26.3 names them (snake_case), checked against the game's own command report.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGameRuleNamesTest, "McClient.McServer.GameRuleNames.VanillaNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGameRuleNamesTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Commands = McCache::LoadJsonObject(McCache::Report(TEXT("commands.json")));
	const TSharedPtr<FJsonObject>* Root = nullptr;
	const TSharedPtr<FJsonObject>* Rule = nullptr;
	const TSharedPtr<FJsonObject>* Children = nullptr;
	if (!Commands.IsValid() || !Commands->TryGetObjectField(TEXT("children"), Children) || !(*Children)->TryGetObjectField(TEXT("gamerule"), Root)
		|| !(*Root)->TryGetObjectField(TEXT("children"), Rule))
	{
		AddInfo(TEXT("no commands.json report on this machine; skipped"));
		return true;
	}
	int32 Vanilla = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Rule)->Values)
	{
		if (Entry.Key.Contains(TEXT(":")))
		{
			continue;
		}
		++Vanilla;
		const McCommands::FGameRule* Ours = McCommands::FindGameRule(Entry.Key);
		if (TestNotNull(*FString::Printf(TEXT("we have the rule %s"), *Entry.Key), Ours))
		{
			const TSharedPtr<FJsonObject>* Value = nullptr;
			const TSharedPtr<FJsonObject>* Parser = nullptr;
			const TSharedPtr<FJsonObject>* Kids = nullptr;
			FString Type;
			if (Entry.Value->AsObject()->TryGetObjectField(TEXT("children"), Kids) && (*Kids)->TryGetObjectField(TEXT("value"), Value))
			{
				(*Value)->TryGetStringField(TEXT("parser"), Type);
				TestEqual(*FString::Printf(TEXT("%s is of the same kind"), *Entry.Key), Ours->bInteger, Type == TEXT("brigadier:integer"));
			}
		}
	}
	// And nothing else: /gamerule lists only vanilla's names.
	TestEqual(TEXT("as many rules as the game has"), McCommands::GetGameRules().Num(), Vanilla);
	TestTrue(TEXT("a lot of rules were read"), Vanilla >= 59);
	for (const McCommands::FGameRule& Ours : McCommands::GetGameRules())
	{
		TestTrue(*FString::Printf(TEXT("%s is lower case with underscores"), Ours.Name), FString(Ours.Name).ToLower() == Ours.Name && !FString(Ours.Name).Contains(TEXT(" ")));
	}
	TestNull(TEXT("the old name is not a rule"), McCommands::FindGameRule(TEXT("doDaylightCycle")));
	TestNull(TEXT("the old name is not a rule (2)"), McCommands::FindGameRule(TEXT("keepInventory")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGameRuleMigrateTest, "McClient.McServer.GameRuleNames.Migrate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGameRuleMigrateTest::RunTest(const FString& Parameters)
{
	// A world saved before the new names: renamed, the three opposites flipped, the rules vanilla dropped gone.
	TMap<FString, FString> Rules;
	Rules.Add(TEXT("doDaylightCycle"), TEXT("false"));
	Rules.Add(TEXT("keepInventory"), TEXT("true"));
	Rules.Add(TEXT("disableRaids"), TEXT("true"));
	Rules.Add(TEXT("disableElytraMovementCheck"), TEXT("false"));
	Rules.Add(TEXT("randomTickSpeed"), TEXT("7"));
	Rules.Add(TEXT("spawnChunkRadius"), TEXT("2"));
	Rules.Add(TEXT("doFireTick"), TEXT("false"));
	Rules.Add(TEXT("pvp"), TEXT("false"));
	Rules.Add(TEXT("mob_griefing"), TEXT("false"));
	FMcGameRules::MigrateLegacyNames(Rules);
	TestEqual(TEXT("time of day"), Rules.FindRef(TEXT("advance_time")), FString(TEXT("false")));
	TestEqual(TEXT("keep inventory"), Rules.FindRef(TEXT("keep_inventory")), FString(TEXT("true")));
	TestEqual(TEXT("raids are on when disableRaids was on, off"), Rules.FindRef(TEXT("raids")), FString(TEXT("false")));
	TestEqual(TEXT("elytra check flipped"), Rules.FindRef(TEXT("elytra_movement_check")), FString(TEXT("true")));
	TestEqual(TEXT("a number is kept"), Rules.FindRef(TEXT("random_tick_speed")), FString(TEXT("7")));
	TestEqual(TEXT("fire off is no radius"), Rules.FindRef(TEXT("fire_spread_radius_around_player")), FString(TEXT("0")));
	TestEqual(TEXT("a name that did not change"), Rules.FindRef(TEXT("pvp")), FString(TEXT("false")));
	TestEqual(TEXT("a new name stays"), Rules.FindRef(TEXT("mob_griefing")), FString(TEXT("false")));
	for (const TCHAR* Gone : { TEXT("doDaylightCycle"), TEXT("keepInventory"), TEXT("disableRaids"), TEXT("spawnChunkRadius"), TEXT("doFireTick") })
	{
		TestFalse(*FString::Printf(TEXT("%s is gone"), Gone), Rules.Contains(Gone));
	}
	// Every name it makes is a rule vanilla has.
	for (const TPair<FString, FString>& Entry : Rules)
	{
		TestNotNull(*FString::Printf(TEXT("%s is a vanilla rule"), *Entry.Key), McCommands::FindGameRule(Entry.Key));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcGameRuleExportTest, "McClient.McServer.GameRuleNames.Export",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcGameRuleExportTest::RunTest(const FString& Parameters)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Tests") / (TEXT("McGameRuleExport_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString World = Folder / TEXT("world");
	const FString Out = Folder / TEXT("out");
	FMcLevelData Level;
	Level.Seed = 5;
	Level.GameRules.Add(TEXT("keep_inventory"), TEXT("true"));
	Level.GameRules.Add(TEXT("random_tick_speed"), TEXT("9"));
	bool bOk = McWorldSave::SaveLevel(World, Level);
	McJavaExport::FStats Stats;
	bOk = bOk && McJavaExport::ExportWorld(World, Out, Stats, false);
	FMcNbt File;
	const FMcNbt* Data = nullptr;
	if (TestTrue(TEXT("the export runs"), bOk) && TestTrue(TEXT("game_rules.dat reads back"), FMcNbt::LoadFile(Out / TEXT("data/minecraft/game_rules.dat"), File))
		&& TestNotNull(TEXT("with data"), Data = File.Find(TEXT("data"))))
	{
		const FMcNbt* Kept = Data->Find(TEXT("minecraft:keep_inventory"));
		if (TestNotNull(TEXT("keep_inventory is there"), Kept))
		{
			TestEqual(TEXT("as a byte, on"), Kept->Integer, int64(1));
			TestTrue(TEXT("a byte"), Kept->Type == FMcNbt::EType::Byte);
		}
		const FMcNbt* Tick = Data->Find(TEXT("minecraft:random_tick_speed"));
		if (TestNotNull(TEXT("random_tick_speed is there"), Tick))
		{
			TestEqual(TEXT("as an int, 9"), Tick->Integer, int64(9));
			TestTrue(TEXT("an int"), Tick->Type == FMcNbt::EType::Int);
		}
		TestNotNull(TEXT("a default is written too"), Data->Find(TEXT("minecraft:pvp")));
		for (const FString& Key : Data->Keys)
		{
			FString Name = Key;
			Name.RemoveFromStart(TEXT("minecraft:"));
			TestNotNull(*FString::Printf(TEXT("%s is a vanilla rule"), *Name), McCommands::FindGameRule(Name));
		}
	}
	// And it reads back as the same rules.
	FMcLevelData After;
	if (TestTrue(TEXT("the export reads back as a level"), McJavaImport::ReadLevel(Out, After)))
	{
		TestEqual(TEXT("keep_inventory survives"), After.GameRules.FindRef(TEXT("keep_inventory")), FString(TEXT("true")));
		TestEqual(TEXT("random_tick_speed survives"), After.GameRules.FindRef(TEXT("random_tick_speed")), FString(TEXT("9")));
	}
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	return true;
}

#endif
