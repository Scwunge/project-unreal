#include "McCraftingMenu.h"
#include "McGameMode.h"
#include "McItemRegistry.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "McPlayerSave.h"
#include "McRecipe.h"
#include "McWorldSave.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRecipeBookTest, "McClient.McPlayer.RecipeBook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcRecipeBookTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()) || !TestTrue(TEXT("recipes load"), FMcRecipeBook::Get().EnsureLoaded()))
	{
		return false;
	}
	AMcPlayerPawn* Pawn = NewObject<AMcPlayerPawn>(GetTransientPackage());
	Pawn->Data = NewObject<UMcPlayerDataComponent>(GetTransientPackage());
	const FName Stick(TEXT("minecraft:stick"));
	const FName Planks(TEXT("minecraft:oak_planks"));

	// A new player knows nothing.
	TestEqual(TEXT("a fresh book is empty"), Pawn->Data->Recipes.Num(), 0);

	// Giving a recipe puts it in the book, to be shown, and meets the advancement that waits on it (the game's data).
	TestEqual(TEXT("one given"), AMcGameMode::UnlockRecipes(*Pawn, { Stick }, true), 1);
	TestTrue(TEXT("in the book"), Pawn->Data->Recipes.Contains(Stick));
	TestTrue(TEXT("to be shown"), Pawn->Data->RecipesToDisplay.Contains(Stick));
	TestTrue(TEXT("its advancement is made"), Pawn->Data->Advancements.Contains(TEXT("minecraft:recipes/misc/stick")));
	TestEqual(TEXT("again: nothing new"), AMcGameMode::UnlockRecipes(*Pawn, { Stick }, true), 0);

	// Taking one back changes the book only: the advancement stays made.
	TestEqual(TEXT("one taken"), AMcGameMode::UnlockRecipes(*Pawn, { Stick }, false), 1);
	TestFalse(TEXT("out of the book"), Pawn->Data->Recipes.Contains(Stick));
	TestFalse(TEXT("and not to be shown"), Pawn->Data->RecipesToDisplay.Contains(Stick));
	TestTrue(TEXT("the advancement stays"), Pawn->Data->Advancements.Contains(TEXT("minecraft:recipes/misc/stick")));
	TestEqual(TEXT("again: nothing to take"), AMcGameMode::UnlockRecipes(*Pawn, { Stick }, false), 0);

	// The save keeps the book and what is still to be shown.
	AMcGameMode::UnlockRecipes(*Pawn, { Stick, Planks }, true);
	Pawn->Data->RecipesToDisplay.Remove(Planks);
	FMcPlayerData Saved;
	McPlayerSave::Capture(*Pawn->Data, Saved);
	UMcPlayerDataComponent* Again = NewObject<UMcPlayerDataComponent>(GetTransientPackage());
	TestTrue(TEXT("it applies"), McPlayerSave::Apply(Saved, *Again));
	TestEqual(TEXT("both recipes come back"), Again->Recipes.Num(), 2);
	TestTrue(TEXT("the shown one is shown"), Again->RecipesToDisplay.Contains(Stick) && !Again->RecipesToDisplay.Contains(Planks));

	// Limited crafting: only recipes in the book give a result.
	FMcCraftingMenu Menu(2, MakeShared<FMcContainer>(36));
	Menu.GetGrid()->Set(0, FMcItemStack(Planks, 1));
	Menu.GetGrid()->Set(2, FMcItemStack(Planks, 1));
	Menu.MayCraft = [Pawn](FName Recipe) { return Pawn->Data->Recipes.Contains(Recipe); };
	Menu.Refresh();
	TestTrue(TEXT("the stick recipe is found"), Menu.GetRecipe() != nullptr);
	Pawn->Data->Recipes.Reset();
	Menu.Refresh();
	TestTrue(TEXT("not crafted without the recipe"), Menu.GetRecipe() == nullptr);
	return true;
}

#endif
