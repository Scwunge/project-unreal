#include "McDataPacks.h"
#include "McItemRegistry.h"
#include "McRecipe.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// A data pack's recipes and item tags reach the recipe book and the item registry on /reload: a recipe of a new namespace
// is added, one of the game's id is replaced, a tag gains a value; and all of it goes when the pack does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataPackRecipeTest, "McClient.McItem.DataPackRecipes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataPackRecipeTest::RunTest(const FString& Parameters)
{
	if (!FMcRecipeBook::Get().EnsureLoaded() || !FMcItemRegistry::Get().EnsureLoaded())
	{
		AddError(TEXT("the game's recipes or items are not in the cache"));
		return false;
	}
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McTestDataPackRecipes"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"description\":\"t\",\"min_format\":121,\"max_format\":121}}"), *(Root / TEXT("pack.mcmeta")));
	FFileHelper::SaveStringToFile(TEXT("{\"type\":\"minecraft:crafting_shapeless\",\"ingredients\":[\"minecraft:dirt\"],\"result\":{\"id\":\"minecraft:diamond\",\"count\":3}}"),
		*(Root / TEXT("data/zz/recipe/dirt_diamond.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"type\":\"minecraft:crafting_shapeless\",\"ingredients\":[\"minecraft:dirt\"],\"result\":{\"id\":\"minecraft:emerald\",\"count\":2}}"),
		*(Root / TEXT("data/minecraft/recipe/stick.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"values\":[\"minecraft:diamond\"]}"), *(Root / TEXT("data/minecraft/tags/item/planks.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"values\":[\"minecraft:dirt\"]}"), *(Root / TEXT("data/zz/tags/item/stuff.json")));

	const FMcRecipe* Before = FMcRecipeBook::Get().Find(FName("minecraft:stick"));
	TestNotNull(TEXT("the game has a stick recipe"), Before);
	TestNull(TEXT("and none for the pack's"), FMcRecipeBook::Get().Find(FName("zz:dirt_diamond")));
	const FName Planks("minecraft:planks");
	const bool bPlanksHadDiamond = FMcItemRegistry::Get().HasTag(FName("minecraft:diamond"), Planks);

	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestNotNull(TEXT("the pack opens"), Pack.Get()))
	{
		return false;
	}
	McData::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>{ Pack }));
	McData::Reload();

	const FMcRecipe* Added = FMcRecipeBook::Get().Find(FName("zz:dirt_diamond"));
	if (TestNotNull(TEXT("the pack's recipe is in the book"), Added))
	{
		TestEqual(TEXT("it gives three diamonds"), Added->Result.Count, 3);
	}
	const FMcRecipe* Replaced = FMcRecipeBook::Get().Find(FName("minecraft:stick"));
	if (TestNotNull(TEXT("the stick recipe is still there"), Replaced))
	{
		TestEqual(TEXT("and the pack's one replaced it"), Replaced->Result.Item, FName("minecraft:emerald"));
	}
	TestFalse(TEXT("planks did not have diamond"), bPlanksHadDiamond);
	TestTrue(TEXT("the pack added diamond to planks"), FMcItemRegistry::Get().HasTag(FName("minecraft:diamond"), Planks));
	TestTrue(TEXT("and kept oak planks"), FMcItemRegistry::Get().HasTag(FName("minecraft:oak_planks"), Planks));
	TestTrue(TEXT("a tag of the pack's namespace"), FMcItemRegistry::Get().HasTag(FName("minecraft:dirt"), FName("zz:stuff")));

	McData::SetStack(MakeShared<FMcPackStack>());
	McData::Reload();
	TestNull(TEXT("the pack's recipe goes with it"), FMcRecipeBook::Get().Find(FName("zz:dirt_diamond")));
	const FMcRecipe* Back = FMcRecipeBook::Get().Find(FName("minecraft:stick"));
	if (TestNotNull(TEXT("the game's stick is back"), Back))
	{
		TestEqual(TEXT("with its sticks"), Back->Result.Item, FName("minecraft:stick"));
	}
	TestFalse(TEXT("planks lost the diamond"), FMcItemRegistry::Get().HasTag(FName("minecraft:diamond"), Planks));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
