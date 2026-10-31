#include "McResourcePacks.h"
#include "McCache.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FZipFile
	{
		FString Name;
		TArray<uint8> Data;
		bool bDeflate = false;
	};

	void Put16(TArray<uint8>& Out, uint32 Value) { Out.Add(Value & 255); Out.Add((Value >> 8) & 255); }
	void Put32(TArray<uint8>& Out, uint32 Value) { Put16(Out, Value & 0xFFFF); Put16(Out, Value >> 16); }

	TArray<uint8> Bytes(const FString& Text)
	{
		FTCHARToUTF8 Utf8(*Text);
		return TArray<uint8>(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	// A zip file's bytes: stored or deflated entries, a central directory, an end record.
	TArray<uint8> MakeZip(const TArray<FZipFile>& Files)
	{
		TArray<uint8> Out;
		TArray<uint8> Directory;
		for (const FZipFile& File : Files)
		{
			TArray<uint8> Packed = File.Data;
			if (File.bDeflate)
			{
				// The engine's zlib output is a 2 byte header, the deflate stream and a 4 byte checksum; a zip keeps only the stream.
				TArray<uint8> Zlib;
				Zlib.SetNumUninitialized(FCompression::CompressMemoryBound(NAME_Zlib, File.Data.Num()));
				int32 Size = Zlib.Num();
				FCompression::CompressMemory(NAME_Zlib, Zlib.GetData(), Size, File.Data.GetData(), File.Data.Num());
				Packed = TArray<uint8>(Zlib.GetData() + 2, Size - 6);
			}
			FTCHARToUTF8 Name(*File.Name);
			const uint32 Crc = FCrc::MemCrc32(File.Data.GetData(), File.Data.Num());
			const uint32 Offset = Out.Num();

			Put32(Out, 0x04034b50); Put16(Out, 20); Put16(Out, 0); Put16(Out, File.bDeflate ? 8 : 0); Put32(Out, 0);
			Put32(Out, Crc); Put32(Out, Packed.Num()); Put32(Out, File.Data.Num()); Put16(Out, Name.Length()); Put16(Out, 0);
			Out.Append(reinterpret_cast<const uint8*>(Name.Get()), Name.Length());
			Out.Append(Packed);

			Put32(Directory, 0x02014b50); Put16(Directory, 20); Put16(Directory, 20); Put16(Directory, 0); Put16(Directory, File.bDeflate ? 8 : 0);
			Put32(Directory, 0); Put32(Directory, Crc); Put32(Directory, Packed.Num()); Put32(Directory, File.Data.Num());
			Put16(Directory, Name.Length()); Put16(Directory, 0); Put16(Directory, 0); Put16(Directory, 0); Put16(Directory, 0);
			Put32(Directory, 0); Put32(Directory, Offset);
			Directory.Append(reinterpret_cast<const uint8*>(Name.Get()), Name.Length());
		}
		const uint32 DirectoryOffset = Out.Num();
		Out.Append(Directory);
		Put32(Out, 0x06054b50); Put16(Out, 0); Put16(Out, 0); Put16(Out, Files.Num()); Put16(Out, Files.Num());
		Put32(Out, Directory.Num()); Put32(Out, DirectoryOffset); Put16(Out, 0);
		return Out;
	}

	const FString StonePng = TEXT("assets/minecraft/textures/block/stone.png");
	const FString DirtPng = TEXT("assets/minecraft/textures/block/dirt.png");
	const FString OnlyInA = TEXT("assets/minecraft/textures/block/only_a.png");

	void WriteFile(const FString& Path, const FString& Text)
	{
		FFileHelper::SaveArrayToFile(Bytes(Text), *Path);
	}

	FString ReadText(const TArray<uint8>& Data)
	{
		return FString(Data.Num(), UTF8_TO_TCHAR(reinterpret_cast<const char*>(Data.GetData())));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcResourcePacksTest, "McClient.McCore.ResourcePacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcResourcePacksTest::RunTest(const FString& Parameters)
{
	// pack.mcmeta: formats, the three shapes of supported_formats, a text component description, the filter.
	{
		FMcPackInfo Info;
		TestTrue(TEXT("parses"), FMcResourcePack::ParseMcmeta(
			TEXT("{\"pack\":{\"pack_format\":22,\"description\":\"§6Faithful §r1024x\",\"supported_formats\":{\"min_inclusive\":5,\"max_inclusive\":28}}}"), Info));
		TestEqual(TEXT("pack_format"), Info.PackFormat, 22);
		TestEqual(TEXT("min"), Info.MinFormat, 5);
		TestEqual(TEXT("max"), Info.MaxFormat, 28);
		TestEqual(TEXT("colour codes removed"), Info.Description, FString(TEXT("Faithful 1024x")));

		TestEqual(TEXT("the range 5 to 28 is older than the game"), McPacks::GetCompatibility(Info), -1);
		FMcPackInfo Probe;
		Probe.MinFormat = 90;
		Probe.MaxFormat = 100;
		TestEqual(TEXT("a range with the game in it"), McPacks::GetCompatibility(Probe), 0);
		Probe.MinFormat = 98;
		TestEqual(TEXT("a range above the game"), McPacks::GetCompatibility(Probe), 1);
		Probe = FMcPackInfo();
		TestEqual(TEXT("no format at all"), McPacks::GetCompatibility(Probe), 2);
		Probe.PackFormat = 97;
		TestEqual(TEXT("pack_format alone, equal"), McPacks::GetCompatibility(Probe), 0);
		Probe.PackFormat = 15;
		TestEqual(TEXT("pack_format alone, older"), McPacks::GetCompatibility(Probe), -1);

		TestTrue(TEXT("array range"), FMcResourcePack::ParseMcmeta(TEXT("{\"pack\":{\"pack_format\":3,\"description\":\"\",\"supported_formats\":[4,9]}}"), Info));
		TestEqual(TEXT("array min"), Info.MinFormat, 4);
		TestEqual(TEXT("array max"), Info.MaxFormat, 9);
		TestTrue(TEXT("number range"), FMcResourcePack::ParseMcmeta(TEXT("{\"pack\":{\"pack_format\":6,\"supported_formats\":6}}"), Info));
		TestEqual(TEXT("single number min"), Info.MinFormat, 6);
		TestEqual(TEXT("single number max"), Info.MaxFormat, 6);

		TestTrue(TEXT("min_format and max_format as [major, minor]"), FMcResourcePack::ParseMcmeta(
			TEXT("{\"pack\":{\"min_format\":[75,1],\"max_format\":80,\"description\":[{\"text\":\"A\",\"extra\":[{\"text\":\"B\"},\"C\"]},\"D\"]}}"), Info));
		TestEqual(TEXT("array format takes the major"), Info.MinFormat, 75);
		TestEqual(TEXT("max_format"), Info.MaxFormat, 80);
		TestEqual(TEXT("pack_format missing"), Info.PackFormat, 0);
		TestEqual(TEXT("component array"), Info.Description, FString(TEXT("ABCD")));

		TestTrue(TEXT("filter"), FMcResourcePack::ParseMcmeta(
			TEXT("{\"pack\":{\"pack_format\":1},\"filter\":{\"block\":[{\"namespace\":\"minecraft\",\"path\":\"textures/block/dirt.*\"}]}}"), Info));
		if (TestEqual(TEXT("one block rule"), Info.Blocks.Num(), 1))
		{
			TestEqual(TEXT("rule path"), Info.Blocks[0].Path, FString(TEXT("textures/block/dirt.*")));
		}
		TestFalse(TEXT("no pack section"), FMcResourcePack::ParseMcmeta(TEXT("{\"filter\":{}}"), Info));
		TestFalse(TEXT("not json"), FMcResourcePack::ParseMcmeta(TEXT("pack_format = 3"), Info));
	}

	// The launch argument and the size choice.
	{
		TestTrue(TEXT("list"), McPacks::ParseList(TEXT(" a.zip, b ,,\"c d\" ")) == TArray<FString>({ TEXT("a.zip"), TEXT("b"), TEXT("c d") }));
		TestEqual(TEXT("empty list"), McPacks::ParseList(TEXT("")).Num(), 0);

		const int64 Mb = 1024 * 1024;
		TestEqual(TEXT("1000 layers of 16 fit anywhere"), McPacks::ChooseTextureSize(16, 1000, 512, 1536 * Mb), 16);
		TestEqual(TEXT("rounds a pack's 24 up"), McPacks::ChooseTextureSize(24, 100, 512, 1536 * Mb), 32);
		TestEqual(TEXT("limited by the max size option"), McPacks::ChooseTextureSize(1024, 100, 256, 1536 * Mb), 256);
		TestEqual(TEXT("1024 with 100 layers: 5.6 MB each, 560 MB"), McPacks::ChooseTextureSize(1024, 100, 1024, 1536 * Mb), 1024);
		TestEqual(TEXT("1024 with 1000 layers drops to what fits"), McPacks::ChooseTextureSize(1024, 1000, 2048, 1536 * Mb), 512);
		TestEqual(TEXT("a tiny budget stops at 16"), McPacks::ChooseTextureSize(1024, 1000, 2048, 1 * Mb), 16);
	}

	// Two packs and the stack: a folder (A) under a zip (B), then the other way.
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString FolderA = Root / TEXT("a");
	WriteFile(FolderA / TEXT("pack.mcmeta"), TEXT("{\"pack\":{\"pack_format\":1,\"description\":\"pack a\"}}"));
	WriteFile(FolderA / StonePng, TEXT("stone from A"));
	WriteFile(FolderA / OnlyInA, TEXT("only A"));
	WriteFile(FolderA / DirtPng, TEXT("dirt from A"));

	FString Compressible;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		Compressible += TEXT("stone from B, a long and repetitive text so that deflate has something to do. ");
	}
	const FString ZipB = Root / TEXT("b.zip");
	FFileHelper::SaveArrayToFile(MakeZip({
		{ TEXT("pack.mcmeta"), Bytes(TEXT("{\"pack\":{\"pack_format\":2,\"description\":\"pack b\"}}")), false },
		{ TEXT("pack.png"), Bytes(TEXT("png")), false },
		{ StonePng, Bytes(Compressible), true },
		{ StonePng + TEXT(".mcmeta"), Bytes(TEXT("{\"animation\":{}}")), true },
		{ TEXT("assets/minecraft/textures/block/"), {}, false } }), *ZipB);

	FString Error;
	const TSharedPtr<FMcResourcePack> A = FMcResourcePack::Open(FolderA, &Error);
	const TSharedPtr<FMcResourcePack> B = FMcResourcePack::Open(ZipB, &Error);
	if (!TestTrue(TEXT("folder pack opens"), A.IsValid()) || !TestTrue(FString::Printf(TEXT("zip pack opens: %s"), *Error), B.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("folder description"), A->GetInfo().Description, FString(TEXT("pack a")));
	TestFalse(TEXT("folder is not a zip"), A->GetInfo().bZip);
	TestTrue(TEXT("zip is a zip"), B->GetInfo().bZip);
	TestEqual(TEXT("zip pack_format"), B->GetInfo().PackFormat, 2);
	TestTrue(TEXT("zip has an icon"), B->GetInfo().bHasIcon);
	TestFalse(TEXT("folder has no icon"), A->GetInfo().bHasIcon);
	TestEqual(TEXT("the folder entry isn't a file"), B->NumZipEntries(), 4);
	TestTrue(TEXT("has"), B->Has(StonePng));
	TestFalse(TEXT("hasn't"), B->Has(DirtPng));

	TArray<uint8> Data;
	TestTrue(TEXT("reads a deflated entry"), B->Read(StonePng, Data));
	TestEqual(TEXT("deflated entry content"), ReadText(Data), Compressible);
	TestTrue(TEXT("reads a stored entry"), B->Read(TEXT("pack.png"), Data));
	TestEqual(TEXT("stored entry content"), ReadText(Data), FString(TEXT("png")));
	TestFalse(TEXT("a missing entry"), B->Read(DirtPng, Data));
	TestFalse(TEXT("a pack that isn't there"), FMcResourcePack::Open(Root / TEXT("nope.zip"), &Error).IsValid());
	WriteFile(Root / TEXT("junk.zip"), TEXT("this is not a zip"));
	TestFalse(TEXT("junk isn't a zip"), FMcResourcePack::Open(Root / TEXT("junk.zip"), &Error).IsValid());
	IFileManager::Get().MakeDirectory(*(Root / TEXT("empty")), true);
	TestFalse(TEXT("a folder without pack.mcmeta"), FMcResourcePack::Open(Root / TEXT("empty"), &Error).IsValid());

	{
		const FMcPackStack Stack({ A, B });  // B on top
		TestEqual(TEXT("the later pack wins a shared file"), Stack.Find(StonePng), 1);
		TestEqual(TEXT("a file only in the lower pack"), Stack.Find(OnlyInA), 0);
		TestEqual(TEXT("a file in neither falls to vanilla"), Stack.Find(TEXT("assets/minecraft/textures/block/grass_block_top.png")), INDEX_NONE);
		int32 From = INDEX_NONE;
		TestTrue(TEXT("read"), Stack.Read(StonePng, Data, &From));
		TestEqual(TEXT("read from the top"), From, 1);
		TestEqual(TEXT("read content"), ReadText(Data), Compressible);
		TestEqual(TEXT("a file only in A is read from A"), Stack.Read(DirtPng, Data, &From) ? From : -1, 0);
	}
	{
		const FMcPackStack Stack({ B, A });  // A on top
		TestEqual(TEXT("the order decides"), Stack.Find(StonePng), 1);
		TestTrue(TEXT("read the other way"), Stack.Read(StonePng, Data));
		TestEqual(TEXT("A's file now"), ReadText(Data), FString(TEXT("stone from A")));
	}

	// A filter in a pack above hides the files below it, and vanilla's.
	const FString FolderC = Root / TEXT("c");
	WriteFile(FolderC / TEXT("pack.mcmeta"),
		TEXT("{\"pack\":{\"pack_format\":1},\"filter\":{\"block\":[{\"namespace\":\"minecraft\",\"path\":\"textures/block/(dirt|only_a)\\\\.png\"}]}}"));
	WriteFile(FolderC / StonePng, TEXT("stone from C"));
	const TSharedPtr<FMcResourcePack> C = FMcResourcePack::Open(FolderC, &Error);
	if (TestTrue(TEXT("filtering pack opens"), C.IsValid()))
	{
		const FMcPackStack Stack({ A, B, C });
		TestEqual(TEXT("blocked file in A is hidden"), Stack.Find(DirtPng), INDEX_NONE);
		TestEqual(TEXT("blocked file only in A"), Stack.Find(OnlyInA), INDEX_NONE);
		TestTrue(TEXT("vanilla's copy hidden too"), Stack.IsVanillaHidden(DirtPng));
		TestEqual(TEXT("an unblocked file is still found"), Stack.Find(StonePng), 2);
		TestFalse(TEXT("vanilla stone is not hidden"), Stack.IsVanillaHidden(StonePng));
		const FMcPackStack Below({ C, A });
		TestEqual(TEXT("the filter only hides what is below it"), Below.Find(DirtPng), 1);
	}

	// McCache::Asset follows the stack: the zip's file is taken out with its .mcmeta; the folder's file is used where it is.
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ A, B })));
	const FString StoneRelative = TEXT("textures/block/stone.png");
	const FString Resolved = McCache::Asset(StoneRelative);
	TestTrue(TEXT("a zip's file is taken out of it"), Resolved.Contains(TEXT("McPackCache")));
	TestTrue(TEXT("the file is there"), FFileHelper::LoadFileToArray(Data, *Resolved) && ReadText(Data) == Compressible);
	TestTrue(TEXT("its .mcmeta beside it"), IFileManager::Get().FileExists(*(Resolved + TEXT(".mcmeta"))));
	TestEqual(TEXT("a folder pack's file is used in place"), McCache::Asset(TEXT("textures/block/only_a.png")), FolderA / OnlyInA);
	TestEqual(TEXT("vanilla for the rest"), McCache::Asset(TEXT("textures/block/grass_block_top.png")), McCache::Root() / TEXT("client/assets/minecraft/textures/block/grass_block_top.png"));
	TestTrue(TEXT("IsFromPack"), McPacks::IsFromPack(StoneRelative) && !McPacks::IsFromPack(TEXT("textures/block/grass_block_top.png")));
	TestFalse(TEXT("signature names the packs"), McPacks::GetSignature().IsEmpty());

	// ReadAll gives every pack's copy, lowest first (for lang and sounds.json, which are merged).
	{
		TArray<TArray<uint8>> All;
		McPacks::ReadAll(TEXT("textures/block/stone.png"), All);
		if (TestEqual(TEXT("both packs have stone"), All.Num(), 2))
		{
			TestEqual(TEXT("the lower pack first"), ReadText(All[0]), FString(TEXT("stone from A")));
		}
	}

	// Overlays: a directory of the pack replaces its files for the formats it names. This game's format is inside the
	// first entry's range, outside the second's; a later entry wins, so only the first is looked at for x.txt.
	{
		const FString FolderD = Root / TEXT("d");
		WriteFile(FolderD / TEXT("pack.mcmeta"),
			TEXT("{\"pack\":{\"pack_format\":97},\"overlays\":{\"entries\":["
				"{\"formats\":[90,100],\"directory\":\"new\"},"
				"{\"formats\":{\"min_inclusive\":1,\"max_inclusive\":5},\"directory\":\"old\"},"
				"{\"min_format\":200,\"directory\":\"future\"}]}}"));
		const FString X = TEXT("assets/minecraft/x.txt");
		WriteFile(FolderD / X, TEXT("base"));
		WriteFile(FolderD / TEXT("new") / X, TEXT("new overlay"));
		WriteFile(FolderD / TEXT("old") / X, TEXT("old overlay"));
		WriteFile(FolderD / TEXT("future") / X, TEXT("future overlay"));
		WriteFile(FolderD / TEXT("assets/minecraft/y.txt"), TEXT("only base"));
		const TSharedPtr<FMcResourcePack> D = FMcResourcePack::Open(FolderD, &Error);
		if (TestTrue(TEXT("overlay pack opens"), D.IsValid()))
		{
			TestEqual(TEXT("three overlay entries read"), D->GetInfo().Overlays.Num(), 3);
			TestTrue(TEXT("read x"), D->Read(X, Data));
			TestEqual(TEXT("the overlay for this format wins"), ReadText(Data), FString(TEXT("new overlay")));
			TestTrue(TEXT("a file only in the base is still read"), D->Read(TEXT("assets/minecraft/y.txt"), Data) && ReadText(Data) == TEXT("only base"));
			TestEqual(TEXT("a folder pack's path is the overlay's"), D->GetFolderFile(X), FolderD / TEXT("new") / X);
		}
	}

	// No packs: the cache paths exactly.
	McPacks::SetStack(MakeShared<FMcPackStack>());
	TestEqual(TEXT("no packs, no change"), McCache::Asset(StoneRelative), McCache::Root() / TEXT("client/assets/minecraft/textures/block/stone.png"));
	TestFalse(TEXT("no packs, nothing from a pack"), McPacks::HasPacks());

	// The picker's side: a folder in the packs folder is listed, and the chosen packs are remembered in order.
	{
		const FString Name = TEXT("zz_mc_test_pack");
		const FString PackDir = McPacks::GetFolder() / Name;
		FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":1,\"description\":\"for the picker test\"}}"), *(PackDir / TEXT("pack.mcmeta")));
		FFileHelper::SaveStringToFile(TEXT("not a pack"), *(McPacks::GetFolder() / TEXT("zz_mc_test_broken") / TEXT("readme.txt")));

		// A zip that isn't one, a truncated zip: listed with the reason, no crash.
		FFileHelper::SaveStringToFile(TEXT("PK this is not really a zip"), *(McPacks::GetFolder() / TEXT("zz_mc_test_garbage.zip")));
		const TArray<McPacks::FFolderEntry> Entries = McPacks::ScanFolder();
		const McPacks::FFolderEntry* Found = Entries.FindByPredicate([&Name](const McPacks::FFolderEntry& Entry) { return Entry.Name == Name; });
		if (TestNotNull(TEXT("the folder pack is listed"), Found))
		{
			TestTrue(TEXT("it opens"), Found->Pack.IsValid() && Found->Error.IsEmpty());
			if (Found->Pack.IsValid())
			{
				TestEqual(TEXT("its description"), Found->Pack->GetInfo().Description, FString(TEXT("for the picker test")));
			}
		}
		const McPacks::FFolderEntry* Broken = Entries.FindByPredicate([](const McPacks::FFolderEntry& Entry) { return Entry.Name == TEXT("zz_mc_test_broken"); });
		if (TestNotNull(TEXT("a folder without pack.mcmeta is listed too"), Broken))
		{
			TestTrue(TEXT("with the reason it can't be used"), !Broken->Pack.IsValid() && !Broken->Error.IsEmpty());
		}

		{
			const McPacks::FFolderEntry* Garbage = Entries.FindByPredicate([](const McPacks::FFolderEntry& Entry) { return Entry.Name == TEXT("zz_mc_test_garbage.zip"); });
			if (TestNotNull(TEXT("a broken zip is listed"), Garbage))
			{
				TestTrue(TEXT("with its reason and no pack"), !Garbage->Pack.IsValid() && !Garbage->Error.IsEmpty());
			}
			McPacks::SetPacks({ TEXT("zz_mc_test_garbage.zip"), TEXT("zz_mc_missing_pack") });
			TestFalse(TEXT("unusable packs are skipped"), McPacks::HasPacks());
			TestEqual(TEXT("so vanilla's file is used"), McCache::Asset(TEXT("textures/block/stone.png")), McCache::Root() / TEXT("client/assets/minecraft/textures/block/stone.png"));
		}

		const TArray<FString> Before = McPacks::GetSelectedNames();
		McPacks::ApplySelection({ Name });
		TestTrue(TEXT("the choice is in use"), McPacks::GetSelectedNames() == TArray<FString>({ Name }));
		TestTrue(TEXT("and has packs"), McPacks::HasPacks());
		FString Saved;
		TestTrue(TEXT("remembered in Saved"), FFileHelper::LoadFileToString(Saved, *(FPaths::ProjectSavedDir() / TEXT("McResourcePacks.json"))) && Saved.Contains(Name));
		McPacks::ApplySelection(Before);
		McPacks::SetStack(MakeShared<FMcPackStack>());

		IFileManager::Get().Delete(*(McPacks::GetFolder() / TEXT("zz_mc_test_garbage.zip")));
		IFileManager::Get().DeleteDirectory(*PackDir, false, true);
		IFileManager::Get().DeleteDirectory(*(McPacks::GetFolder() / TEXT("zz_mc_test_broken")), false, true);
	}

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
