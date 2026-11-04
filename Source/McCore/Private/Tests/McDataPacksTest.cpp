#include "McDataPacks.h"
#include "McCache.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void WriteTestFile(const FString& Root, const FString& Relative, const FString& Text)
	{
		FFileHelper::SaveStringToFile(Text, *(Root / Relative));
	}
}

// A folder data pack over the game's data: its files of an id replace the game's, other namespaces are added, tags add up
// unless they say "replace", and McCache::Data sees the replacement.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataPackTest, "McClient.McCore.DataPacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataPackTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McTestDataPack"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	WriteTestFile(Root, TEXT("pack.mcmeta"), TEXT("{\"pack\":{\"description\":\"t\",\"min_format\":121,\"max_format\":121}}"));
	WriteTestFile(Root, TEXT("data/zz/recipe/a.json"), TEXT("{\"type\":\"minecraft:crafting_shapeless\"}"));
	WriteTestFile(Root, TEXT("data/minecraft/recipe/stick.json"), TEXT("{\"type\":\"minecraft:crafting_shapeless\",\"marker\":1}"));
	WriteTestFile(Root, TEXT("data/minecraft/tags/item/planks.json"), TEXT("{\"values\":[\"minecraft:diamond\"]}"));
	WriteTestFile(Root, TEXT("data/minecraft/tags/item/logs.json"), TEXT("{\"replace\":true,\"values\":[\"minecraft:emerald\",{\"id\":\"minecraft:gold_ingot\",\"required\":false}]}"));
	WriteTestFile(Root, TEXT("data/zz/function/f.mcfunction"), TEXT("say hi"));
	WriteTestFile(Root, TEXT("data/zz/tags/block/soft.json"), TEXT("{\"values\":[\"minecraft:dirt\",\"#minecraft:logs\"]}"));

	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestNotNull(TEXT("the pack opens"), Pack.Get()))
	{
		return false;
	}
	McData::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>{ Pack }));

	const TArray<McData::FFile> Recipes = McData::List(TEXT("recipe"));
	const McData::FFile* Stick = Recipes.FindByPredicate([](const McData::FFile& File) { return File.Id == TEXT("minecraft:stick"); });
	const McData::FFile* Own = Recipes.FindByPredicate([](const McData::FFile& File) { return File.Id == TEXT("zz:a"); });
	if (TestNotNull(TEXT("the game's stick recipe is listed"), Stick) && TestNotNull(TEXT("the pack's own recipe is listed"), Own))
	{
		TestEqual(TEXT("the pack's stick follows the game's"), Stick->Paths.Num(), 2);
		TestEqual(TEXT("a recipe of the pack's namespace has the one file"), Own->Paths.Num(), 1);
	}

	// Tags: planks gains the pack's value; logs is replaced.
	const TArray<McData::FFile> Tags = McData::List(TEXT("tags/item"));
	const McData::FFile* Planks = Tags.FindByPredicate([](const McData::FFile& File) { return File.Id == TEXT("minecraft:planks"); });
	const McData::FFile* Logs = Tags.FindByPredicate([](const McData::FFile& File) { return File.Id == TEXT("minecraft:logs"); });
	if (TestNotNull(TEXT("planks"), Planks) && TestNotNull(TEXT("logs"), Logs))
	{
		const TArray<FString> PlankValues = McData::ReadTagValues(Planks->Paths);
		TestTrue(TEXT("planks keeps the game's values"), PlankValues.Contains(TEXT("minecraft:oak_planks")));
		TestEqual(TEXT("and gains the pack's"), PlankValues.Last(), FString(TEXT("minecraft:diamond")));
		const TArray<FString> LogValues = McData::ReadTagValues(Logs->Paths);
		TestEqual(TEXT("logs is only the pack's two"), LogValues.Num(), 2);
		TestTrue(TEXT("an object entry gives its id"), LogValues.Contains(TEXT("minecraft:gold_ingot")));
	}

	FString Path;
	TestTrue(TEXT("a pack file resolves"), McData::TryResolve(TEXT("zz:f"), TEXT("function"), TEXT(".mcfunction"), Path));
	TestEqual(TEXT("to the pack's file"), Path.Replace(TEXT("\\"), TEXT("/")).EndsWith(TEXT("data/zz/function/f.mcfunction")), true);
	TestTrue(TEXT("a namespace only the pack has: found"), !McData::PathOf(TEXT("zz:f"), TEXT("function"), TEXT(".mcfunction")).IsEmpty());
	TestTrue(TEXT("a pack-less namespace's file: not found"), McData::PathOf(TEXT("qq:f"), TEXT("function"), TEXT(".mcfunction")).IsEmpty());
	TestTrue(TEXT("McCache::Data gives the pack's stick recipe"), McCache::Data(TEXT("recipe/stick.json")).Replace(TEXT("\\"), TEXT("/")).Contains(TEXT("McTestDataPack")));
	TestFalse(TEXT("a game file the pack lacks stays the game's"), McCache::Data(TEXT("recipe/torch.json")).Contains(TEXT("McTestDataPack")));

	// A block tag of the pack's namespace is read on /reload, and goes again when the pack does.
	if (FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded())
	{
		McData::Reload();
		const int32 Soft = FMcBlockTags::Get().Find(TEXT("zz:soft"));
		if (TestTrue(TEXT("the pack's block tag exists"), Soft != INDEX_NONE))
		{
			const FMcBlock* Dirt = FMcBlockRegistry::Get().FindBlock(FName("minecraft:dirt"));
			const FMcBlock* Oak = FMcBlockRegistry::Get().FindBlock(FName("minecraft:oak_log"));
			TestTrue(TEXT("dirt is in it"), Dirt && FMcBlockTags::Get().HasBlock(Soft, Dirt->Index));
			TestTrue(TEXT("and the game's logs tag it names comes in"), Oak && FMcBlockTags::Get().HasBlock(Soft, Oak->Index));
		}
	}

	McData::SetStack(MakeShared<FMcPackStack>());
	if (FMcBlockTags::Get().IsLoaded())
	{
		McData::Reload();
	}
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
