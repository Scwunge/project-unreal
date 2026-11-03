#include "McResourcePacks.h"
#include "McCache.h"
#include "McLang.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
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
		TestEqual(TEXT("pack_format above 64 without min_format and max_format is unknown to the game"), McPacks::GetCompatibility(Probe), 2);
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

// Atlas sources of every atlas become pictures the game finds like a pack's own: "paletted_permutations" recolours a texture
// by swapping the colours of a key palette for another palette's, "unstitch" cuts a sprite from a sheet, "filter" hides.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackAtlasSpritesTest, "McClient.McCore.PackAtlasSprites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackAtlasSpritesTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackAtlasSpritesTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString Assets = Root / TEXT("assets/minecraft");
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97,\"description\":\"atlas sprites\"}}"), *(Root / TEXT("pack.mcmeta")));
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const auto WritePng = [&](const FString& Name, int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		if (Png.IsValid() && Png->SetRaw(Pixels.GetData(), Pixels.Num() * 4, Width, Height, ERGBFormat::BGRA, 8))
		{
			const TArray64<uint8> Data = Png->GetCompressed();
			FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Data.GetData(), Data.Num()), *(Assets / TEXT("textures") / Name));
		}
	};
	// A 2 x 1 texture in the two key colours (one transparent texel); the key palette; a palette to turn it into.
	WritePng(TEXT("trims/zz_base.png"), 3, 1, { FColor(10, 10, 10, 255), FColor(20, 20, 20, 255), FColor(0, 0, 0, 0) });
	WritePng(TEXT("trims/zz_key.png"), 2, 1, { FColor(10, 10, 10, 255), FColor(20, 20, 20, 255) });
	WritePng(TEXT("trims/zz_red.png"), 2, 1, { FColor(200, 0, 0, 255), FColor(100, 0, 0, 255) });
	FFileHelper::SaveStringToFile(TEXT("{\"sources\":[{\"type\":\"minecraft:paletted_permutations\",\"textures\":[\"minecraft:trims/zz_base\"],"
		"\"palette_key\":\"minecraft:trims/zz_key\",\"permutations\":{\"red\":\"minecraft:trims/zz_red\"},\"separator\":\"_\"},"
		"{\"type\":\"minecraft:filter\",\"path\":\"block/dirt\"}]}"), *(Assets / TEXT("atlases/blocks.json")));

	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));
	FString Path;
	if (TestTrue(TEXT("the recoloured sprite is found"), McPacks::FindAtlasSprite(TEXT("textures/trims/zz_base_red.png"), Path)))
	{
		TArray<uint8> File;
		const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (TestTrue(TEXT("it can be read"), FFileHelper::LoadFileToArray(File, *Path) && Png.IsValid() && Png->SetCompressed(File.GetData(), File.Num()) && Png->GetRaw(ERGBFormat::BGRA, 8, Raw) && Raw.Num() == 12))
		{
			TestEqual(TEXT("first colour swapped"), FColor(Raw[2], Raw[1], Raw[0], Raw[3]), FColor(200, 0, 0, 255));
			TestEqual(TEXT("second colour swapped"), FColor(Raw[6], Raw[5], Raw[4], Raw[7]), FColor(100, 0, 0, 255));
			TestEqual(TEXT("transparent stays transparent"), static_cast<int32>(Raw[11]), 0);
		}
	}
	TestTrue(TEXT("the atlas sprite counts as the pack's"), McPacks::IsFromPack(TEXT("textures/trims/zz_base_red.png")));
	TestTrue(TEXT("the filter hides the game's dirt"), McPacks::IsAtlasFiltered(TEXT("textures/block/dirt.png")));
	TestFalse(TEXT("but not stone"), McPacks::IsAtlasFiltered(TEXT("textures/block/stone.png")));

	McPacks::SetStack(MakeShared<FMcPackStack>());
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// A picture's .png.mcmeta "texture" section: blur and clamp, each only when it names them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTextureMetaTest, "McClient.McCore.TextureMeta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcTextureMetaTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McTextureMetaTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const auto Meta = [&](const TCHAR* Name, const TCHAR* Json)
	{
		if (Json)
		{
			FFileHelper::SaveStringToFile(Json, *(Root / Name + TEXT(".mcmeta")));
		}
		return McCache::ReadTextureMeta(Root / Name);
	};
	McCache::FTextureMeta Blur = Meta(TEXT("a.png"), TEXT("{\"texture\":{\"blur\":true}}"));
	TestTrue(TEXT("blur"), Blur.bBlur);
	TestFalse(TEXT("clamp not named"), Blur.bHasClamp);
	McCache::FTextureMeta Clamp = Meta(TEXT("b.png"), TEXT("{\"texture\":{\"clamp\":false}}"));
	TestFalse(TEXT("no blur"), Clamp.bBlur);
	TestTrue(TEXT("clamp named"), Clamp.bHasClamp);
	TestFalse(TEXT("as false"), Clamp.bClamp);
	McCache::FTextureMeta Other = Meta(TEXT("c.png"), TEXT("{\"animation\":{\"frametime\":2}}"));
	TestFalse(TEXT("a mcmeta without the section"), Other.bBlur || Other.bHasClamp);
	McCache::FTextureMeta None = Meta(TEXT("d.png"), nullptr);
	TestFalse(TEXT("no mcmeta"), None.bBlur || None.bHasClamp);
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// Files dropped on the window: packs are told from other files, and copied into the resourcepacks folder without
// overwriting a pack of that name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackDropTest, "McClient.McCore.PackDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackDropTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackDropTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString Source = Root / TEXT("from"), Target = Root / TEXT("resourcepacks");
	FFileHelper::SaveStringToFile(TEXT("zip"), *(Source / TEXT("a.zip")));
	FFileHelper::SaveStringToFile(TEXT("hi"), *(Source / TEXT("notes.txt")));
	FFileHelper::SaveStringToFile(TEXT("{}"), *(Source / TEXT("dir_pack/pack.mcmeta")));
	FFileHelper::SaveStringToFile(TEXT("x"), *(Source / TEXT("dir_pack/assets/minecraft/a.txt")));
	FFileHelper::SaveStringToFile(TEXT("x"), *(Source / TEXT("plain_dir/a.txt")));

	const McPacks::FDrop Drop = McPacks::SortDropped({ Source / TEXT("a.zip"), Source / TEXT("notes.txt"), Source / TEXT("dir_pack"), Source / TEXT("plain_dir"), Source / TEXT("missing.zip") });
	TestEqual(TEXT("packs"), Drop.Packs.Num(), 2);
	TestTrue(TEXT("names"), Drop.Names.Contains(TEXT("a.zip")) && Drop.Names.Contains(TEXT("dir_pack")));
	TestEqual(TEXT("others"), Drop.Others.Num(), 3);

	TArray<FString> Failed;
	TArray<FString> Copied = McPacks::CopyDropped(Drop, Failed, Target);
	TestEqual(TEXT("copied"), Copied.Num(), 2);
	TestEqual(TEXT("none failed"), Failed.Num(), 0);
	TestTrue(TEXT("the zip is there"), IFileManager::Get().FileExists(*(Target / TEXT("a.zip"))));
	TestTrue(TEXT("the folder came with its files"), IFileManager::Get().FileExists(*(Target / TEXT("dir_pack/assets/minecraft/a.txt"))));

	Copied = McPacks::CopyDropped(Drop, Failed, Target);
	TestEqual(TEXT("a second drop copies nothing"), Copied.Num(), 0);
	TestEqual(TEXT("and fails both"), Failed.Num(), 2);
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// A villager picture's .png.mcmeta "villager" section: how far the profession or type hides the hat layer. The model reads
// McCache::VillagerHat; here it is checked on loose files and through the pack stack, from a folder pack and a zip.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVillagerHatTest, "McClient.McCore.VillagerHat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcVillagerHatTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McVillagerHatTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const auto Hat = [&](const TCHAR* Name, const TCHAR* Json)
	{
		if (Json)
		{
			FFileHelper::SaveStringToFile(Json, *(Root / Name + TEXT(".mcmeta")));
		}
		return McCache::ReadVillagerHat(Root / Name);
	};
	TestTrue(TEXT("partial"), Hat(TEXT("a.png"), TEXT("{\"villager\":{\"hat\":\"partial\"}}")) == McCache::EVillagerHat::Partial);
	TestTrue(TEXT("full"), Hat(TEXT("b.png"), TEXT("{\"villager\":{\"hat\":\"full\"}}")) == McCache::EVillagerHat::Full);
	TestTrue(TEXT("none, said"), Hat(TEXT("c.png"), TEXT("{\"villager\":{\"hat\":\"none\"}}")) == McCache::EVillagerHat::None);
	TestTrue(TEXT("a value that isn't one"), Hat(TEXT("d.png"), TEXT("{\"villager\":{\"hat\":\"huge\"}}")) == McCache::EVillagerHat::None);
	TestTrue(TEXT("no villager section"), Hat(TEXT("e.png"), TEXT("{\"texture\":{\"blur\":true}}")) == McCache::EVillagerHat::None);
	TestTrue(TEXT("no mcmeta"), Hat(TEXT("f.png"), nullptr) == McCache::EVillagerHat::None);

	// Through the stack: a profession from a folder pack, a type from a zip (its .mcmeta is taken out beside the picture).
	const FString Profession = TEXT("assets/minecraft/textures/entity/villager/profession/zz_farmer.png");
	const FString Type = TEXT("assets/minecraft/textures/entity/villager/type/zz_plains.png");
	const FString FolderDir = Root / TEXT("folder");
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97}}"), *(FolderDir / TEXT("pack.mcmeta")));
	FFileHelper::SaveStringToFile(TEXT("png"), *(FolderDir / Profession));
	FFileHelper::SaveStringToFile(TEXT("{\"villager\":{\"hat\":\"partial\"}}"), *(FolderDir / Profession + TEXT(".mcmeta")));
	const FString ZipPath = Root / TEXT("types.zip");
	FFileHelper::SaveArrayToFile(MakeZip({
		{ TEXT("pack.mcmeta"), Bytes(TEXT("{\"pack\":{\"pack_format\":97}}")), false },
		{ Type, Bytes(TEXT("png")), false },
		{ Type + TEXT(".mcmeta"), Bytes(TEXT("{\"villager\":{\"hat\":\"full\"}}")), true } }), *ZipPath);
	FString Error;
	const TSharedPtr<FMcResourcePack> Folder = FMcResourcePack::Open(FolderDir, &Error);
	const TSharedPtr<FMcResourcePack> Zip = FMcResourcePack::Open(ZipPath, &Error);
	if (!TestTrue(TEXT("the packs open: ") + Error, Folder.IsValid() && Zip.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Folder, Zip })));
	TestTrue(TEXT("a profession's hat from a folder pack"), McCache::VillagerHat(TEXT("profession"), TEXT("minecraft:zz_farmer")) == McCache::EVillagerHat::Partial);
	TestTrue(TEXT("a type's hat from a zip"), McCache::VillagerHat(TEXT("type"), TEXT("zz_plains")) == McCache::EVillagerHat::Full);
	TestTrue(TEXT("a picture the packs lack: none"), McCache::VillagerHat(TEXT("profession"), TEXT("zz_nobody")) == McCache::EVillagerHat::None);
	McPacks::SetStack(MakeShared<FMcPackStack>());
	TestTrue(TEXT("without the packs: none"), McCache::VillagerHat(TEXT("profession"), TEXT("zz_farmer")) == McCache::EVillagerHat::None);
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// lang/en_us.json of the packs is merged key by key over the game's, lowest pack first; a key no pack gives stays the game's.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackLangTest, "McClient.McCore.PackLang",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackLangTest::RunTest(const FString& Parameters)
{
	McPacks::SetStack(MakeShared<FMcPackStack>());
	const FString Survival = McLang::Get(TEXT("gameMode.survival"));
	const FString Creative = McLang::Get(TEXT("gameMode.creative"));
	const FString Adventure = McLang::Get(TEXT("gameMode.adventure"));
	if (!TestNotEqual(TEXT("the game has gameMode.survival"), Survival, FString(TEXT("gameMode.survival"))))
	{
		return true;
	}

	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackLangTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString LowDir = Root / TEXT("low");
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97}}"), *(LowDir / TEXT("pack.mcmeta")));
	FFileHelper::SaveStringToFile(TEXT("{\"gameMode.survival\":\"Low Survival\",\"gameMode.creative\":\"Low Creative\",\"zz.lang.new\":\"from low\"}"),
		*(LowDir / TEXT("assets/minecraft/lang/en_us.json")));
	const FString ZipPath = Root / TEXT("high.zip");
	FFileHelper::SaveArrayToFile(MakeZip({
		{ TEXT("pack.mcmeta"), Bytes(TEXT("{\"pack\":{\"pack_format\":97}}")), false },
		{ TEXT("assets/minecraft/lang/en_us.json"), Bytes(TEXT("{\"gameMode.creative\":\"High Creative \\u00e9\",\"zz.lang.new\":\"from high\",\"zz.lang.pattern\":\"%2$s then %1$s\"}")), true } }), *ZipPath);
	FString Error;
	const TSharedPtr<FMcResourcePack> Low = FMcResourcePack::Open(LowDir, &Error);
	const TSharedPtr<FMcResourcePack> High = FMcResourcePack::Open(ZipPath, &Error);
	if (!TestTrue(TEXT("the packs open: ") + Error, Low.IsValid() && High.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Low, High })));
	TestEqual(TEXT("a key only the lower pack gives"), McLang::Get(TEXT("gameMode.survival")), FString(TEXT("Low Survival")));
	TestEqual(TEXT("a key both give: the higher pack's, escapes decoded"), McLang::Get(TEXT("gameMode.creative")), FString(TEXT("High Creative \u00e9")));
	TestEqual(TEXT("a new key in both: the higher"), McLang::Get(TEXT("zz.lang.new")), FString(TEXT("from high")));
	TestEqual(TEXT("a key no pack gives is the game's"), McLang::Get(TEXT("gameMode.adventure")), Adventure);
	TestEqual(TEXT("a pack's pattern is formatted"), McLang::Format(TEXT("zz.lang.pattern"), { TEXT("a"), TEXT("b") }), FString(TEXT("b then a")));
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ High, Low })));
	TestEqual(TEXT("the order decides"), McLang::Get(TEXT("gameMode.creative")), FString(TEXT("Low Creative")));
	McPacks::SetStack(MakeShared<FMcPackStack>());
	TestEqual(TEXT("without the packs the game's text is back"), McLang::Get(TEXT("gameMode.survival")), Survival);
	TestEqual(TEXT("and the new key reads as itself"), McLang::Get(TEXT("zz.lang.new")), FString(TEXT("zz.lang.new")));
	TestEqual(TEXT("creative"), McLang::Get(TEXT("gameMode.creative")), Creative);
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// The chosen language's file goes over en_us, key by key, and a pack's lang file of that language over both; a pack's
// "language" entry in pack.mcmeta adds a language to the picker.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLanguageTest, "McClient.McCore.Language",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcLanguageTest::RunTest(const FString& Parameters)
{
	McPacks::SetStack(MakeShared<FMcPackStack>());
	const FString Before = McLang::GetLanguage();
	McLang::SetLanguage(TEXT("en_us"));
	const FString English = McLang::Get(TEXT("gui.done"));
	if (!TestNotEqual(TEXT("the game has gui.done"), English, FString(TEXT("gui.done"))))
	{
		return true;
	}

	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McLanguageTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97},\"language\":{\"zz_zz\":{\"name\":\"Zedzed\",\"region\":\"Zedland\",\"bidirectional\":true}}}"),
		*(Root / TEXT("pack.mcmeta")));
	FFileHelper::SaveStringToFile(TEXT("{\"gui.done\":\"Zed done\",\"zz.only\":\"zed only\"}"), *(Root / TEXT("assets/minecraft/lang/zz_zz.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"zz.only\":\"english only\"}"), *(Root / TEXT("assets/minecraft/lang/en_us.json")));
	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));

	bool bListed = false;
	for (const McLang::FLanguage& Language : McLang::GetLanguages())
	{
		if (Language.Code == TEXT("zz_zz"))
		{
			bListed = true;
			TestEqual(TEXT("label"), Language.GetLabel(), FString(TEXT("Zedzed (Zedland)")));
			TestTrue(TEXT("bidirectional"), Language.bBidirectional);
		}
	}
	TestTrue(TEXT("the pack's language is offered"), bListed);

	TestEqual(TEXT("english: the pack's en_us over the game's"), McLang::Get(TEXT("zz.only")), FString(TEXT("english only")));
	McLang::SetLanguage(TEXT("zz_zz"));
	TestEqual(TEXT("the chosen language's key"), McLang::Get(TEXT("gui.done")), FString(TEXT("Zed done")));
	TestEqual(TEXT("the language file over en_us"), McLang::Get(TEXT("zz.only")), FString(TEXT("zed only")));
	TestEqual(TEXT("a key the language lacks falls back to english"), McLang::Get(TEXT("gameMode.survival")), McLang::Get(TEXT("gameMode.survival")));
	TestNotEqual(TEXT("and is not the key"), McLang::Get(TEXT("gameMode.survival")), FString(TEXT("gameMode.survival")));
	McLang::SetLanguage(TEXT("en_us"));
	TestEqual(TEXT("back to english"), McLang::Get(TEXT("gui.done")), English);
	McLang::SetLanguage(Before.IsEmpty() ? TEXT("en_us") : Before);
	McPacks::SetStack(MakeShared<FMcPackStack>());
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackBuiltInTest, "McClient.McCore.PackBuiltIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackBuiltInTest::RunTest(const FString& Parameters)
{
	// The packs the game ships (High Contrast, Programmer Art) are offered by the ids the game gives them, and a selection
	// that names one comes back under that id, not under the file name the cache keeps.
	const TArray<FString> Names = McPacks::GetBuiltInNames();
	if (Names.IsEmpty())
	{
		AddWarning(TEXT("no built-in packs in the cache (run tools/fetch_cache.py)"));
		return true;
	}
	TestTrue(TEXT("High Contrast is there"), Names.Contains(TEXT("high_contrast")));
	TestTrue(TEXT("Programmer Art is there"), Names.Contains(TEXT("programmer_art")));
	const TArray<McPacks::FFolderEntry> Entries = McPacks::ScanFolder();
	int32 NumBuiltIn = 0;
	for (const McPacks::FFolderEntry& Entry : Entries)
	{
		if (Entry.bBuiltIn)
		{
			++NumBuiltIn;
			TestTrue(Entry.Name + TEXT(" opens: ") + Entry.Error, Entry.Pack.IsValid());
			if (Entry.Pack.IsValid())
			{
				TestEqual(Entry.Name + TEXT(" is named by its id"), Entry.Pack->GetInfo().Name, Entry.Name);
				TestEqual(Entry.Name + TEXT(" is made for this version"), McPacks::GetCompatibility(Entry.Pack->GetInfo()), 0);
				TestTrue(Entry.Name + TEXT(" has a description"), !Entry.Pack->GetInfo().Description.IsEmpty());
			}
		}
	}
	TestEqual(TEXT("each built-in pack is listed once"), NumBuiltIn, Names.Num());

	const TArray<FString> Before = McPacks::GetSelectedNames();
	McPacks::SetPacks({ TEXT("programmer_art") });
	TestEqual(TEXT("a selection of a built-in pack keeps its id"), McPacks::GetSelectedNames(), TArray<FString>({ TEXT("programmer_art") }));
	McPacks::SetPacks(Before);
	return true;
}

#endif
