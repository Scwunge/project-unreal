#include "McItemRegistry.h"
#include "McJavaImport.h"
#include "McPlayerData.h"
#include "McPlayerSave.h"
#include "McWorldSave.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPlayerImportTest, "McClient.McPlayer.ImportedPlayerWearsArmour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPlayerImportTest::RunTest(const FString& Parameters)
{
	// The playerref save (1070 only), imported and loaded the way a shot loads it: the iron helmet is worn and counts.
	const FString Source = TEXT("C:/mc-reference/run/saves/playerref");
	if (!McJavaImport::IsJavaWorld(Source))
	{
		AddInfo(TEXT("no playerref save on this machine; skipped"));
		return true;
	}
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	McJavaImport::FStats Stats;
	const FString Folder = McJavaImport::ImportWorld(Source, TEXT("PlayerImportArmour"), Stats);
	if (!TestFalse(TEXT("the import made a world"), Folder.IsEmpty()))
	{
		return false;
	}
	FMcPlayerData Saved;
	TestTrue(TEXT("the player loads"), McWorldSave::LoadPlayer(McWorldSave::GetWorldDir(Folder), TEXT("Player0"), Saved));
	UMcPlayerDataComponent* Data = NewObject<UMcPlayerDataComponent>(GetTransientPackage());
	TestTrue(TEXT("applies"), McPlayerSave::Apply(Saved, *Data));
	AddInfo(FString::Printf(TEXT("head: %s, armour points %d, off hand: %s"), *Data->Armor->Get(0).Item.ToString(), Data->Stats.Armor, *Data->Offhand->Get(0).Item.ToString()));
	TestEqual(TEXT("iron helmet on the head"), Data->Armor->Get(0).Item, FName(TEXT("minecraft:iron_helmet")));
	TestEqual(TEXT("it is two armour points"), Data->Stats.Armor, 2);
	TestEqual(TEXT("shield in the off hand"), Data->Offhand->Get(0).Item, FName(TEXT("minecraft:shield")));
	McWorldSave::DeleteWorld(Folder);
	return true;
}
