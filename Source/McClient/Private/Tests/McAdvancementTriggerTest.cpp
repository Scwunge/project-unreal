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
	// A biome entered counts toward Adventuring Time (all of them are needed to make it), and a recipe toward the trim one.
	FMcTriggerEvent Where;
	Where.Trigger = TEXT("minecraft:location");
	Where.Biome = TEXT("minecraft:badlands");
	AMcGameMode::FireTrigger(*Pawn, Where);
	TestTrue(TEXT("badlands entered"), Pawn->Data->Criteria.Contains(TEXT("minecraft:adventure/adventuring_time/minecraft:badlands")));
	TestFalse(TEXT("one biome is not the lot"), Pawn->Data->Advancements.Contains(TEXT("minecraft:adventure/adventuring_time")));
	FMcTriggerEvent Crafted;
	Crafted.Trigger = TEXT("minecraft:recipe_crafted");
	Crafted.Recipe = TEXT("minecraft:rib_armor_trim_smithing_template_smithing_trim");
	AMcGameMode::FireTrigger(*Pawn, Crafted);
	TestTrue(TEXT("recipe crafted"), Pawn->Data->Criteria.Contains(TEXT("minecraft:adventure/trim_with_all_exclusive_armor_patterns/armor_trimmed_minecraft:rib_armor_trim_smithing_template_smithing_trim")));
	// A compass used on a lodestone makes Oh, Lodestone (it is a block and a tool the criterion names), a stick does not.
	FMcTriggerEvent Used;
	Used.Trigger = TEXT("minecraft:item_used_on_block");
	Used.Item = TEXT("minecraft:stick");
	Used.Block = TEXT("minecraft:lodestone");
	AMcGameMode::FireTrigger(*Pawn, Used);
	TestFalse(TEXT("a stick is no compass"), Pawn->Data->Advancements.Contains(TEXT("minecraft:adventure/use_lodestone")));
	Used.Item = TEXT("minecraft:compass");
	AMcGameMode::FireTrigger(*Pawn, Used);
	TestTrue(TEXT("lodestone used"), Pawn->Data->Advancements.Contains(TEXT("minecraft:adventure/use_lodestone")));
	for (const TCHAR* Trigger : { TEXT("minecraft:slept_in_bed"), TEXT("minecraft:enchanted_item"), TEXT("minecraft:villager_trade") })
	{
		FMcTriggerEvent Plain;
		Plain.Trigger = Trigger;
		AMcGameMode::FireTrigger(*Pawn, Plain);
	}
	TestTrue(TEXT("slept"), Pawn->Data->Advancements.Contains(TEXT("minecraft:adventure/sleep_in_bed")));
	TestTrue(TEXT("enchanted"), Pawn->Data->Advancements.Contains(TEXT("minecraft:story/enchant_item")));
	TestTrue(TEXT("traded"), Pawn->Data->Advancements.Contains(TEXT("minecraft:adventure/trade")));
	FMcTriggerEvent Moved;
	Moved.Trigger = TEXT("minecraft:changed_dimension");
	Moved.From = TEXT("minecraft:overworld");
	Moved.To = TEXT("minecraft:the_nether");
	AMcGameMode::FireTrigger(*Pawn, Moved);
	TestTrue(TEXT("nether entered"), Pawn->Data->Advancements.Contains(TEXT("minecraft:story/enter_the_nether")));
	TestFalse(TEXT("not the end"), Pawn->Data->Advancements.Contains(TEXT("minecraft:story/enter_the_end")));
	FMcTriggerEvent Totem;
	Totem.Trigger = TEXT("minecraft:used_totem");
	Totem.Item = TEXT("minecraft:totem_of_undying");
	AMcGameMode::FireTrigger(*Pawn, Totem);
	TestTrue(TEXT("totem"), Pawn->Data->Advancements.Contains(TEXT("minecraft:adventure/totem_of_undying")));
	return true;
}

#endif
