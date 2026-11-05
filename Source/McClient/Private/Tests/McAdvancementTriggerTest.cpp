#include "McGameMode.h"
#include "McItemRegistry.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcAdvancementTriggerTest, "McClient.McPlayer.AdvancementTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcAdvancementTriggerTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	AMcPlayerPawn* Pawn = NewObject<AMcPlayerPawn>(GetTransientPackage());
	Pawn->Data = NewObject<UMcPlayerDataComponent>(GetTransientPackage());

	// A criterion without conditions is met by any such event (Tame an Animal).
	FMcTriggerEvent Tame;
	Tame.Trigger = TEXT("minecraft:tame_animal");
	Tame.Entity = TEXT("minecraft:wolf");
	AMcGameMode::FireTrigger(*Pawn, Tame);
	TestTrue(TEXT("tamed"), Pawn->Data->Advancements.Contains(TEXT("minecraft:husbandry/tame_an_animal")));

	// A block placed meets the one that asks for it, and not the others (Plant a Seed: wheat, not stone).
	FMcTriggerEvent Placed;
	Placed.Trigger = TEXT("minecraft:placed_block");
	Placed.Block = TEXT("minecraft:stone");
	AMcGameMode::FireTrigger(*Pawn, Placed);
	TestFalse(TEXT("stone is no seed"), Pawn->Data->Advancements.Contains(TEXT("minecraft:husbandry/plant_seed")));
	Placed.Block = TEXT("minecraft:wheat");
	AMcGameMode::FireTrigger(*Pawn, Placed);
	TestTrue(TEXT("wheat planted"), Pawn->Data->Advancements.Contains(TEXT("minecraft:husbandry/plant_seed")));

	// A kill of the wrong mob leaves Monster Hunter alone, the right one makes it.
	FMcTriggerEvent Killed;
	Killed.Trigger = TEXT("minecraft:player_killed_entity");
	Killed.Entity = TEXT("minecraft:cow");
	AMcGameMode::FireTrigger(*Pawn, Killed);
	TestFalse(TEXT("a cow is no monster"), Pawn->Data->Advancements.Contains(TEXT("minecraft:adventure/kill_a_mob")));
	Killed.Entity = TEXT("minecraft:zombie");
	AMcGameMode::FireTrigger(*Pawn, Killed);
	TestTrue(TEXT("a zombie is"), Pawn->Data->Advancements.Contains(TEXT("minecraft:adventure/kill_a_mob")));
	return true;
}

#endif
