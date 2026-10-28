// Every block tag the jar ships is read by the tag loader, and the tag folders nothing reads are counted.

#include "McBlockTags.h"
#include "McCache.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataCoverageBlockTagsTest, "McClient.McCore.DataCoverageTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataCoverageBlockTagsTest::RunTest(const FString& Parameters)
{
	FMcBlockTags& Tags = FMcBlockTags::Get();
	if (!TestTrue(TEXT("block tags load"), Tags.EnsureLoaded()))
	{
		return false;
	}
	FString Root = McCache::Data(TEXT("tags/block"));
	FPaths::NormalizeFilename(Root);
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.json"), true, false);
	TArray<FString> Missing;
	for (FString File : Files)
	{
		FPaths::NormalizeFilename(File);
		const FString Id = FPaths::ChangeExtension(File.RightChop(Root.Len() + 1), TEXT(""));
		if (Tags.Find(Id) == INDEX_NONE)
		{
			Missing.Add(Id);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: block tags: %d files, %d loaded, %d missing"), Files.Num(), Tags.NumTags(), Missing.Num());
	AddInfo(FString::Printf(TEXT("%d block tag files, %d tags loaded"), Files.Num(), Tags.NumTags()));
	TestTrue(TEXT("there are block tag files"), Files.Num() > 100);
	TestEqual(TEXT("every file is a loaded tag"), Tags.NumTags(), Files.Num());
	for (const FString& Id : Missing)
	{
		UE_LOG(LogTemp, Display, TEXT("DataCoverage: block tag not loaded: %s"), *Id);
		AddError(FString::Printf(TEXT("block tag %s did not load"), *Id));
	}

	// Other kinds of tag have no loader in the project (some are read inline: instrument, worldgen/biome).
	TArray<FString> Folders;
	IFileManager::Get().FindFilesRecursive(Folders, *McCache::Data(TEXT("tags")), TEXT("*.json"), true, false);
	TMap<FString, int32> ByKind;
	FString TagsRoot = McCache::Data(TEXT("tags"));
	FPaths::NormalizeFilename(TagsRoot);
	for (FString File : Folders)
	{
		FPaths::NormalizeFilename(File);
		const FString Relative = File.RightChop(TagsRoot.Len() + 1);
		int32 Slash = INDEX_NONE;
		Relative.FindChar(TEXT('/'), Slash);
		FString Kind = Relative.Left(Slash);
		if (Kind == TEXT("worldgen"))
		{
			Kind = Relative.Left(Relative.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Slash + 1));
		}
		++ByKind.FindOrAdd(Kind);
	}
	for (const TPair<FString, int32>& Pair : ByKind)
	{
		UE_LOG(LogTemp, Display, TEXT("DataCoverage: tag kind %s: %d files"), *Pair.Key, Pair.Value);
	}
	return true;
}

#endif
