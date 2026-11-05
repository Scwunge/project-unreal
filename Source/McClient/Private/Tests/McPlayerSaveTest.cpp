#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "McItemRegistry.h"
#include "McPlayerData.h"
#include "McPlayerSave.h"
#include "McSurvival.h"
#include "McWorldSave.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPlayerSaveTest, "McClient.McPlayer.SaveInventoryAndStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPlayerSaveTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	UMcPlayerDataComponent* Before = NewObject<UMcPlayerDataComponent>(GetTransientPackage());
	Before->Stats.Health = 13.5f;
	Before->Stats.Food = 7;
	Before->Stats.Saturation = 2.5f;
	Before->Stats.Exhaustion = 1.25f;
	Before->Stats.Air = 120;
	Before->Stats.XpLevel = 17;
	Before->Stats.XpProgress = 0.25f;
	Before->SelectedSlot = 4;
	Before->AddStat(TEXT("custom:jump"), 12);
	Before->AddStat(TEXT("mined:minecraft:stone"), 3);
	Before->Inventory->Set(0, FMcItemStack(TEXT("minecraft:diamond_pickaxe"), 1));
	Before->Inventory->Set(8, FMcItemStack(TEXT("minecraft:cobblestone"), 64));
	Before->Inventory->Set(20, FMcItemStack(TEXT("minecraft:bread"), 3));
	FMcItemStack Damaged(TEXT("minecraft:iron_sword"), 1);
	Damaged.Set(TEXT("minecraft:damage"), MakeShared<FJsonValueNumber>(42));
	Before->Inventory->Set(35, Damaged);
	Before->Armor->Set(0, FMcItemStack(TEXT("minecraft:iron_helmet"), 1));
	Before->Armor->Set(3, FMcItemStack(TEXT("minecraft:iron_boots"), 1));
	Before->Offhand->Set(0, FMcItemStack(TEXT("minecraft:torch"), 12));
	Before->EnderChest->Set(0, FMcItemStack(TEXT("minecraft:diamond"), 5));
	Before->EnderChest->Set(26, FMcItemStack(TEXT("minecraft:stick"), 7));

	FMcPlayerData Saved;
	McPlayerSave::Capture(*Before, Saved);
	TestTrue(TEXT("has survival state"), Saved.bHasSurvival);

	// Through the file.
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Tests") / (TEXT("McPlayerSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!TestTrue(TEXT("saves"), McWorldSave::SavePlayer(Dir, TEXT("alex"), Saved)))
	{
		return false;
	}
	FMcPlayerData Loaded;
	const bool bLoaded = McWorldSave::LoadPlayer(Dir, TEXT("alex"), Loaded);
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	if (!TestTrue(TEXT("loads"), bLoaded))
	{
		return false;
	}
	TestTrue(TEXT("the file reads back as it was written"), Loaded == Saved);

	UMcPlayerDataComponent* After = NewObject<UMcPlayerDataComponent>(GetTransientPackage());
	TestTrue(TEXT("applies"), McPlayerSave::Apply(Loaded, *After));
	TestEqual(TEXT("health"), After->Stats.Health, 13.5f);
	TestEqual(TEXT("food"), After->Stats.Food, 7);
	TestEqual(TEXT("saturation"), After->Stats.Saturation, 2.5f);
	TestEqual(TEXT("exhaustion"), After->Stats.Exhaustion, 1.25f);
	TestEqual(TEXT("air"), After->Stats.Air, 120);
	TestEqual(TEXT("level"), After->Stats.XpLevel, 17);
	TestEqual(TEXT("progress"), After->Stats.XpProgress, 0.25f);
	TestEqual(TEXT("selected slot"), After->SelectedSlot, 4);
	TestEqual(TEXT("jumps counted"), After->GetStat(TEXT("custom:jump")), static_cast<int64>(12));
	TestEqual(TEXT("blocks mined counted"), After->GetStat(TEXT("mined:minecraft:stone")), static_cast<int64>(3));
	TestEqual(TEXT("pickaxe"), After->Inventory->Get(0).Item, FName(TEXT("minecraft:diamond_pickaxe")));
	TestEqual(TEXT("cobblestone"), After->Inventory->Get(8).Count, 64);
	TestEqual(TEXT("bread"), After->Inventory->Get(20).Count, 3);
	TestTrue(TEXT("components survive"), After->Inventory->Get(35) == Damaged);
	TestEqual(TEXT("helmet on the head"), After->Armor->Get(0).Item, FName(TEXT("minecraft:iron_helmet")));
	TestEqual(TEXT("boots on the feet"), After->Armor->Get(3).Item, FName(TEXT("minecraft:iron_boots")));
	TestTrue(TEXT("nothing on the chest"), After->Armor->Get(1).IsEmpty());
	TestEqual(TEXT("torches in the off hand"), After->Offhand->Get(0).Count, 12);
	TestEqual(TEXT("diamonds in the ender chest"), After->EnderChest->Get(0).Count, 5);
	TestEqual(TEXT("sticks at its end"), After->EnderChest->Get(26).Count, 7);
	TestTrue(TEXT("and not in the inventory"), After->Inventory->Get(0).Item != FName(TEXT("minecraft:diamond")));
	TestEqual(TEXT("armour points follow the armour"), After->Stats.Armor, 4);

	// A file from before survival has no state and leaves a new player alone.
	FMcPlayerData Old;
	UMcPlayerDataComponent* Fresh = NewObject<UMcPlayerDataComponent>(GetTransientPackage());
	TestFalse(TEXT("an old file applies nothing"), McPlayerSave::Apply(Old, *Fresh));

	// A player saved dead comes back alive.
	FMcPlayerData Dead = Saved;
	Dead.Stats.Add(TEXT("health"), 0.0);
	UMcPlayerDataComponent* Revived = NewObject<UMcPlayerDataComponent>(GetTransientPackage());
	McPlayerSave::Apply(Dead, *Revived);
	TestEqual(TEXT("full health"), Revived->Stats.Health, Revived->Stats.MaxHealth);
	return true;
}

#endif
