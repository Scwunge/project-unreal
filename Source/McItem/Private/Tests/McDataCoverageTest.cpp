// Every recipe and item tag the jar ships is read by the loaders, so nothing is silently dropped.

#include "McCache.h"
#include "McItemRegistry.h"
#include "McRecipe.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// The ids ("crafting/dyes/red", "tools/axe") of the json files under cache's data/minecraft/<Folder>.
	TArray<FString> ListDataIds(const FString& Folder)
	{
		FString Root = McCache::Data(Folder);
		FPaths::NormalizeFilename(Root);
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.json"), true, false);
		TArray<FString> Ids;
		for (FString File : Files)
		{
			FPaths::NormalizeFilename(File);
			Ids.Add(FPaths::ChangeExtension(File.RightChop(Root.Len() + 1), TEXT("")));
		}
		Ids.Sort();
		return Ids;
	}

	void ReportList(FAutomationTestBase& Test, const TCHAR* What, const TArray<FString>& Bad)
	{
		if (Bad.IsEmpty())
		{
			return;
		}
		for (const FString& Id : Bad)
		{
			UE_LOG(LogTemp, Display, TEXT("DataCoverage: %s: %s"), What, *Id);
		}
		FString Head;
		for (int32 Index = 0; Index < FMath::Min(Bad.Num(), 40); ++Index)
		{
			Head += TEXT(" ") + Bad[Index];
		}
		Test.AddError(FString::Printf(TEXT("%d %s:%s%s"), Bad.Num(), What, *Head, Bad.Num() > 40 ? TEXT(" ...") : TEXT("")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataCoverageRecipesTest, "McClient.McItem.DataCoverageRecipes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataCoverageRecipesTest::RunTest(const FString& Parameters)
{
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	if (!TestTrue(TEXT("recipes load"), Book.EnsureLoaded()))
	{
		return false;
	}
	const TArray<FString> Ids = ListDataIds(TEXT("recipe"));
	TArray<FString> Missing;
	TMap<EMcRecipeType, int32> ByType;
	for (const FString& Id : Ids)
	{
		if (!Book.Find(FName(*McCache::Namespaced(Id))))
		{
			Missing.Add(Id);
		}
	}
	for (const FMcRecipe& Recipe : Book.GetAll())
	{
		++ByType.FindOrAdd(Recipe.Type);
	}
	for (const TPair<EMcRecipeType, int32>& Pair : ByType)
	{
		UE_LOG(LogTemp, Display, TEXT("DataCoverage: recipe type %d: %d"), static_cast<int32>(Pair.Key), Pair.Value);
	}
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: recipes: %d files, %d loaded, %d skipped"), Ids.Num(), Book.Num(), Book.NumSkipped());
	AddInfo(FString::Printf(TEXT("%d recipe files, %d loaded, %d skipped"), Ids.Num(), Book.Num(), Book.NumSkipped()));
	TestTrue(TEXT("there are recipe files"), Ids.Num() > 1000);
	TestEqual(TEXT("loaded + skipped covers every file"), Book.Num() + Book.NumSkipped(), Ids.Num());
	ReportList(*this, TEXT("recipe files that did not load (unknown type or malformed)"), Missing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataCoverageItemTagsTest, "McClient.McItem.DataCoverageTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataCoverageItemTagsTest::RunTest(const FString& Parameters)
{
	FMcItemRegistry& Items = FMcItemRegistry::Get();
	if (!TestTrue(TEXT("items load"), Items.EnsureLoaded()))
	{
		return false;
	}
	const TArray<FString> Ids = ListDataIds(TEXT("tags/item"));
	TArray<FString> Missing;
	int32 Empty = 0;
	for (const FString& Id : Ids)
	{
		const TSet<FName>* Members = Items.GetTag(FName(*McCache::Namespaced(Id)));
		if (!Members)
		{
			Missing.Add(Id);
		}
		else if (Members->IsEmpty())
		{
			++Empty;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: item tags: %d files, %d missing, %d empty"), Ids.Num(), Missing.Num(), Empty);
	AddInfo(FString::Printf(TEXT("%d item tag files, %d empty once resolved"), Ids.Num(), Empty));
	TestTrue(TEXT("there are item tag files"), Ids.Num() > 100);
	ReportList(*this, TEXT("item tags that did not load"), Missing);

	const TArray<FString> EnchantmentIds = ListDataIds(TEXT("tags/enchantment"));
	TArray<FString> MissingEnchantment;
	for (const FString& Id : EnchantmentIds)
	{
		if (!Items.GetEnchantmentTag(FName(*McCache::Namespaced(Id))))
		{
			MissingEnchantment.Add(Id);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: enchantment tags: %d files, %d missing"), EnchantmentIds.Num(), MissingEnchantment.Num());
	ReportList(*this, TEXT("enchantment tags that did not load"), MissingEnchantment);
	return true;
}

#endif
