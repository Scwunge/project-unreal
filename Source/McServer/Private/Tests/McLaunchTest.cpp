#include "McLaunch.h"
#include "McWorldSave.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcServerPropertiesTest, "McClient.McServer.ServerProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcServerPropertiesTest::RunTest(const FString& Parameters)
{
	// As the game writes it: a comment, escaped colons, keys in any order.
	FMcServerProperties Properties;
	Properties.Parse(TEXT("#Minecraft server properties\n#Tue Oct 28 2026\nlevel-type=minecraft\\:flat\nmax-players = 5\nwhite-list=true\nmotd=Hello\\=there\n\n"));
	TestEqual(TEXT("an escaped colon is read as one"), Properties.GetString(TEXT("level-type")), FString(TEXT("minecraft:flat")));
	TestEqual(TEXT("spaces round the = are not part of the key or value"), Properties.GetInt(TEXT("max-players"), 20), 5);
	TestTrue(TEXT("white-list"), Properties.GetBool(TEXT("white-list")));
	TestEqual(TEXT("an escaped = stays in the value"), Properties.GetString(TEXT("motd")), FString(TEXT("Hello=there")));
	TestFalse(TEXT("a key the file doesn't have"), Properties.Has(TEXT("server-port")));
	TestEqual(TEXT("which reads as its default"), Properties.GetString(TEXT("server-port")), FString(TEXT("25565")));
	TestEqual(TEXT("a number that isn't one falls back"), Properties.GetInt(TEXT("motd"), 7), 7);

	TestTrue(TEXT("the missing defaults are added"), Properties.AddMissingDefaults());
	TestFalse(TEXT("once"), Properties.AddMissingDefaults());
	const FString Written = Properties.ToString();
	TestTrue(TEXT("comments are kept"), Written.StartsWith(TEXT("#Minecraft server properties\n#Tue Oct 28 2026\n")));
	TestTrue(TEXT("a line the owner wrote is kept as written"), Written.Contains(TEXT("max-players = 5\n")));
	TestTrue(TEXT("an added default is escaped as the game does"), Written.Contains(TEXT("server-port=25565\n")));
	TestFalse(TEXT("no blank line from the file's last newline"), Written.Contains(TEXT("\n\n")));

	FMcServerProperties Again;
	Again.Parse(Written);
	TestEqual(TEXT("reads back the same"), Again.ToString(), Written);
	Again.Set(TEXT("level-type"), TEXT("minecraft:normal"));
	TestTrue(TEXT("a changed value is written escaped"), Again.ToString().Contains(TEXT("level-type=minecraft\\:normal\n")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcServerAccessTest, "McClient.McServer.ServerAccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcServerAccessTest::RunTest(const FString& Parameters)
{
	FMcServerAccess Access;
	TestTrue(TEXT("allowlist"), Access.ParseAllowlist(TEXT("[{\"uuid\": \"x\", \"name\": \"Alice\"}, {\"name\": \"\"}, {\"uuid\": \"y\"}]")));
	TestTrue(TEXT("ops"), Access.ParseOps(TEXT("[{\"name\": \"Bob\", \"level\": 2, \"bypassesPlayerLimit\": true}, {\"name\": \"Dan\"}]")));
	TestEqual(TEXT("entries without a name are skipped"), Access.Allowlist.Num(), 1);
	TestTrue(TEXT("names match ignoring case"), Access.IsAllowlisted(TEXT("alice")));
	TestNotNull(TEXT("an op"), Access.FindOp(TEXT("BOB")));
	TestEqual(TEXT("their level"), Access.FindOp(TEXT("Bob")) ? Access.FindOp(TEXT("Bob"))->Level : 0, 2);
	TestEqual(TEXT("level 4 when the file doesn't say"), Access.FindOp(TEXT("Dan")) ? Access.FindOp(TEXT("Dan"))->Level : 0, 4);
	FMcServerAccess Bad;
	TestFalse(TEXT("text that isn't a list"), Bad.ParseAllowlist(TEXT("{\"name\": \"Alice\"}")));

	FMcServerProperties Open;
	Open.Parse(TEXT("max-players=2\nwhite-list=false"));
	TestEqual(TEXT("anyone, with room"), FMcServerAccess::CheckJoin(Open, Access, TEXT("Eve"), 1), FString());
	TestEqual(TEXT("nobody once full"), FMcServerAccess::CheckJoin(Open, Access, TEXT("Alice"), 2), FString(TEXT("The server is full!")));
	TestEqual(TEXT("an op who bypasses the limit"), FMcServerAccess::CheckJoin(Open, Access, TEXT("Bob"), 2), FString());
	TestEqual(TEXT("an op who doesn't"), FMcServerAccess::CheckJoin(Open, Access, TEXT("Dan"), 2), FString(TEXT("The server is full!")));

	FMcServerProperties Closed;
	Closed.Parse(TEXT("max-players=2\nwhite-list=true"));
	TestEqual(TEXT("not on the allowlist"), FMcServerAccess::CheckJoin(Closed, Access, TEXT("Eve"), 0), FString(TEXT("You are not white-listed on this server!")));
	TestEqual(TEXT("on it"), FMcServerAccess::CheckJoin(Closed, Access, TEXT("ALICE"), 0), FString());
	TestEqual(TEXT("ops get past it"), FMcServerAccess::CheckJoin(Closed, Access, TEXT("Dan"), 0), FString());

	TestTrue(TEXT("ban files"), Access.ParseBans(TEXT("[{\"name\": \"Mallory\", \"source\": \"Server\", \"reason\": \"cheating\"}, {\"name\": \"\"}]"), false));
	TestTrue(TEXT("ip ban files"), Access.ParseBans(TEXT("[{\"ip\": \"9.9.9.9\", \"reason\": \"spam\"}]"), true));
	TestEqual(TEXT("a banned name"), FMcServerAccess::CheckJoin(Open, Access, TEXT("mallory"), 0), FString(TEXT("You are banned from this server.\nReason: cheating")));
	TestEqual(TEXT("a banned address"), FMcServerAccess::CheckJoin(Open, Access, TEXT("Eve"), 0, TEXT("9.9.9.9")), FString(TEXT("Your IP address is banned from this server.\nReason: spam")));
	TestEqual(TEXT("another address"), FMcServerAccess::CheckJoin(Open, Access, TEXT("Eve"), 0, TEXT("9.9.9.8")), FString());
	FMcServerAccess Again;
	Again.ParseBans(Access.BansToJson(false), false);
	TestEqual(TEXT("the file round-trips"), Again.Bans.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLaunchOptionsTest, "McClient.McServer.LaunchOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLaunchOptionsTest::RunTest(const FString& Parameters)
{
	// Seeds: numbers as they are, text by Java's String.hashCode (minecraft.wiki, Seed (level generation)).
	TestEqual(TEXT("empty is random"), McLaunch::ParseSeed(TEXT("  ")), static_cast<int64>(0));
	TestEqual(TEXT("a number"), McLaunch::ParseSeed(TEXT("-4172144997902289642")), static_cast<int64>(-4172144997902289642LL));
	TestEqual(TEXT("with a plus"), McLaunch::ParseSeed(TEXT("+12")), static_cast<int64>(12));
	TestEqual(TEXT("\"hello\".hashCode()"), McLaunch::ParseSeed(TEXT("hello")), static_cast<int64>(99162322));
	TestEqual(TEXT("\"Minecraft\".hashCode(), which overflows"), McLaunch::ParseSeed(TEXT("Minecraft")), static_cast<int64>(-1595926131));
	TestEqual(TEXT("too big for a long is text"), McLaunch::ParseSeed(TEXT("99999999999999999999")), static_cast<int64>(1260560192));
	TestEqual(TEXT("a lone minus is text"), McLaunch::ParseSeed(TEXT("-")), static_cast<int64>(45));

	TestTrue(TEXT("creative"), McLaunch::IsCreativeMode(TEXT("Creative")));
	TestTrue(TEXT("creative by number"), McLaunch::IsCreativeMode(TEXT("1")));
	TestFalse(TEXT("survival"), McLaunch::IsCreativeMode(TEXT("survival")));

	const FString Name = TEXT("My World? 100% = fun#1 é");
	const FString Encoded = McLaunch::EncodeOption(Name);
	TestFalse(TEXT("no space"), Encoded.Contains(TEXT(" ")));
	TestFalse(TEXT("no ?"), Encoded.Contains(TEXT("?")));
	TestFalse(TEXT("no ="), Encoded.Contains(TEXT("=")));
	TestFalse(TEXT("no #"), Encoded.Contains(TEXT("#")));
	TestEqual(TEXT("round trip"), McLaunch::DecodeOption(Encoded), Name);
	TestEqual(TEXT("a stray % is kept"), McLaunch::DecodeOption(TEXT("50%")), FString(TEXT("50%")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorldListTest, "McClient.McServer.WorldList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWorldListTest::RunTest(const FString& Parameters)
{
	// A world of its own in Saved/Worlds, deleted by the test (that deleting works is part of it).
	const FString Name = TEXT("Test ") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Folder = McWorldSave::MakeNewFolder(Name);
	TestEqual(TEXT("the folder is the safe name"), Folder, McWorldSave::SanitizeName(Name));

	FMcLevelData Level;
	Level.Seed = 42;
	Level.Name = Name;
	Level.GameMode = TEXT("creative");
	TestTrue(TEXT("saved"), McWorldSave::SaveLevel(McWorldSave::GetWorldDir(Folder), Level));
	FMcLevelData Loaded;
	TestTrue(TEXT("loaded"), McWorldSave::LoadLevel(McWorldSave::GetWorldDir(Folder), Loaded));
	TestTrue(TEXT("the name and game mode come back"), Loaded == Level);

	TestEqual(TEXT("a taken name gets a number"), McWorldSave::MakeNewFolder(Name), Folder + TEXT("_2_"));
	const TArray<McWorldSave::FWorldSummary> Worlds = McWorldSave::ListWorlds();
	const McWorldSave::FWorldSummary* Listed = Worlds.FindByPredicate([&Folder](const McWorldSave::FWorldSummary& World) { return World.Folder == Folder; });
	TestNotNull(TEXT("the list has it"), Listed);
	TestEqual(TEXT("by its own name"), Listed ? Listed->Level.Name : FString(), Name);
	TestTrue(TEXT("newest first"), Worlds.Num() > 0 && Worlds[0].Folder == Folder);

	TestFalse(TEXT("a path is not a world"), McWorldSave::DeleteWorld(TEXT("../") + Folder));
	TestTrue(TEXT("deleted"), McWorldSave::DeleteWorld(Folder));
	TestFalse(TEXT("and gone"), IFileManager::Get().DirectoryExists(*McWorldSave::GetWorldDir(Folder)));
	TestFalse(TEXT("a folder with no level.json isn't deleted"), McWorldSave::DeleteWorld(TEXT("no_such_world_here")));
	return true;
}

#endif
