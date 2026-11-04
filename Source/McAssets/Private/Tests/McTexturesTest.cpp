#include "McBiomeColors.h"
#include "McBlockModels.h"
#include "McBlockTextures.h"
#include "McResourcePacks.h"
#include "McTextureTiles.h"
#include "Engine/Texture2DArray.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTexturesTest, "McClient.McAssets.Textures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcTexturesTest::RunTest(const FString& Parameters)
{
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	if (!TestTrue(TEXT("textures load from cache/"), Textures.EnsureLoaded()))
	{
		return false;
	}

	const FMcTextureStats& Stats = Textures.GetStats();
	AddInfo(FString::Printf(TEXT("%d texture ids, %d loaded, %d layers, %d animated, %d cutout, %d translucent, %d fell back, %d not 16x16"),
		Stats.Ids, Stats.Loaded, Stats.Layers, Stats.Animated, Stats.Cutout, Stats.Translucent, Stats.Fallbacks.Num(), Stats.Resized.Num()));
	for (const FString& Resized : Stats.Resized)
	{
		AddInfo(TEXT("scaled down: ") + Resized);
	}
	// heavy_core.json names its texture "all" without the '#', so it refers to a file that doesn't exist (the
	// game shows the missing texture there too). Any other fallback is a real failure.
	for (const FName Fallback : Stats.Fallbacks)
	{
		if (Fallback == FName(TEXT("minecraft:all")))
		{
			AddInfo(TEXT("expected fallback: minecraft:all (heavy_core.json has \"texture\": \"all\")"));
		}
		else
		{
			AddError(FString::Printf(TEXT("%s fell back to the missing-texture layer"), *Fallback.ToString()));
		}
	}
	TestEqual(TEXT("layer 0 plus one per loaded texture, and the flowing pictures' sides"), Textures.NumLayers(), Stats.Loaded + 1 + Stats.SideLayers);
	TestEqual(TEXT("water and lava both have a side layer"), Stats.SideLayers, 2);
	TestEqual(TEXT("every id is loaded or listed as a fallback"), Stats.Loaded + Stats.Fallbacks.Num(), Stats.Ids);

	UTexture2DArray* Array = Textures.GetTextureArray();
	if (TestNotNull(TEXT("the texture array exists"), Array))
	{
		TestEqual(TEXT("array slices"), Array->GetArraySize(), Textures.NumLayers());
		TestEqual(TEXT("array width is the texture size"), Array->GetSizeX(), Textures.GetSize());
		TestEqual(TEXT("array mips are a full chain"), Array->GetNumMips(), Textures.GetNumMips());
		if (!McPacks::HasPacks())
		{
			TestEqual(TEXT("vanilla array is 16 wide"), Textures.GetSize(), 16);
			TestEqual(TEXT("vanilla array has 5 mips"), Array->GetNumMips(), FMcBlockTextures::NumMips);
			TestEqual(TEXT("nothing from a pack"), Textures.GetStats().FromPacks, 0);
		}
	}

	TestEqual(TEXT("missingno is layer 0"), Textures.GetLayer(TEXT("minecraft:missingno")), 0);
	TestEqual(TEXT("unknown ids are layer 0"), Textures.GetLayer(TEXT("minecraft:block/not_a_texture")), 0);

	const int32 Stone = Textures.GetLayer(TEXT("minecraft:block/stone"));
	if (TestTrue(TEXT("stone has its own layer"), Stone > 0))
	{
		TestFalse(TEXT("stone has no cutout pixels"), Textures.HasCutoutPixels(Stone));
		TestFalse(TEXT("stone has no translucent pixels"), Textures.HasTranslucentPixels(Stone));
		TestFalse(TEXT("stone isn't animated"), Textures.IsAnimated(Stone));
	}

	const int32 Glass = Textures.GetLayer(TEXT("minecraft:block/glass"));
	if (TestTrue(TEXT("glass has its own layer"), Glass > 0))
	{
		TestTrue(TEXT("glass has cutout pixels"), Textures.HasCutoutPixels(Glass));
	}

	const int32 Water = Textures.GetLayer(TEXT("minecraft:block/water_still"));
	if (TestTrue(TEXT("water_still has its own layer"), Water > 0) && TestTrue(TEXT("water_still is animated"), Textures.IsAnimated(Water)))
	{
		TestTrue(TEXT("water_still has translucent pixels"), Textures.HasTranslucentPixels(Water));

		// water_still.png.mcmeta says frametime 2 and lists no frames: the picture moves on every second tick.
		Textures.Tick(0.0);
		const int32 FirstStep = Textures.GetAnimationStep(Water);
		const TArray<FColor> FirstPixels(Textures.GetPixels(Water));
		Textures.Tick(1.0);
		TestEqual(TEXT("water_still holds its frame for frametime ticks"), Textures.GetAnimationStep(Water), FirstStep);
		Textures.Tick(2.0);
		TestNotEqual(TEXT("water_still moves on after frametime ticks"), Textures.GetAnimationStep(Water), FirstStep);
		TestTrue(TEXT("the layer's pixels changed with the frame"), FirstPixels != TArray<FColor>(Textures.GetPixels(Water)));
		Textures.Tick(0.0);
	}
	// No block model names the flowing pictures, but falling and running water and lava are drawn with them.
	for (const TCHAR* Flow : { TEXT("minecraft:block/water_flow"), TEXT("minecraft:block/lava_flow") })
	{
		const int32 Layer = Textures.GetLayer(Flow);
		TestTrue(FString::Printf(TEXT("%s has its own layer, and moves"), Flow), Layer > 0 && Textures.IsAnimated(Layer));
		// A side shows the top left quarter of the 32-pixel frames, unscaled: its own layer, moving with them.
		const int32 Side = Textures.GetLayer(FName(FString(Flow) + TEXT("#side")));
		TestTrue(FString::Printf(TEXT("%s#side has its own layer, and moves"), Flow), Side > 0 && Side != Layer && Textures.IsAnimated(Side));
	}

	// Colormaps and biomes.
	FMcBiomeColorTable& Biomes = FMcBiomeColorTable::Get();
	if (TestTrue(TEXT("biome colours load from cache/"), Biomes.EnsureLoaded()))
	{
		TestEqual(TEXT("biome count"), Biomes.NumBiomes(), 67);
		const FMcBiomeColors* Plains = Biomes.Find(TEXT("minecraft:plains"));
		if (TestNotNull(TEXT("plains exists"), Plains))
		{
			TestEqual(TEXT("plains temperature"), Plains->Temperature, 0.8f);
			TestEqual(TEXT("plains downfall"), Plains->Downfall, 0.4f);
			TestEqual(TEXT("plains grass is the colormap's"), Plains->Grass, Biomes.GetGrassColormap().Sample(0.8f, 0.4f));
			TestEqual(TEXT("plains foliage is the colormap's"), Plains->Foliage, Biomes.GetFoliageColormap().Sample(0.8f, 0.4f));
			TestEqual(TEXT("plains water"), Plains->Water, FColor(0x3F, 0x76, 0xE4, 255));
		}

		// The corners of the grass colormap: hot and dry at the top left is yellow-brown, cold is the bottom right.
		const FMcColormap& Grass = Biomes.GetGrassColormap();
		TestEqual(TEXT("the lookup clamps"), Grass.Sample(5.f, 5.f), Grass.Sample(1.f, 1.f));
		TestEqual(TEXT("downfall is scaled by temperature"), Grass.Sample(0.f, 1.f), Grass.Sample(0.f, 0.f));

		const FMcBiomeColors* Swamp = Biomes.Find(TEXT("swamp"));
		if (TestNotNull(TEXT("swamp exists"), Swamp))
		{
			TestEqual(TEXT("swamp is flagged"), Swamp->GrassModifier, EMcGrassModifier::Swamp);
			TestEqual(TEXT("swamp water"), Swamp->Water, FColor(0x61, 0x7B, 0x64, 255));
			TestEqual(TEXT("swamp foliage"), Swamp->Foliage, FColor(0x6A, 0x70, 0x39, 255));
		}
		const FMcBiomeColors* DarkForest = Biomes.Find(TEXT("dark_forest"));
		if (TestNotNull(TEXT("dark_forest exists"), DarkForest))
		{
			TestEqual(TEXT("dark forest is flagged"), DarkForest->GrassModifier, EMcGrassModifier::DarkForest);
			TestNotEqual(TEXT("dark forest grass is darkened"), DarkForest->Grass, Grass.Sample(DarkForest->Temperature, DarkForest->Downfall));
		}
	}
	return true;
}

// The wiki's animation rules, read from a pack's .png.mcmeta: a frames list with per-frame times, frametime, the
// interpolation between frames. The pack overrides stone with two solid frames (frame 0 red, frame 1 blue) played as
// blue for 4 ticks then red for 2, blended while it plays.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackAnimationTest, "McClient.McAssets.PackAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackAnimationTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackAnimationTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString Dir = Root / TEXT("assets/minecraft/textures/block");
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97,\"description\":\"animation test\"}}"), *(Root / TEXT("pack.mcmeta")));

	TArray<FColor> Pixels;
	Pixels.SetNum(16 * 32);
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		Pixels[Index] = Index < 16 * 16 ? FColor(200, 0, 0, 255) : FColor(0, 0, 200, 255);
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!TestTrue(TEXT("a picture to write"), Png.IsValid() && Png->SetRaw(Pixels.GetData(), Pixels.Num() * 4, 16, 32, ERGBFormat::BGRA, 8)))
	{
		return false;
	}
	const TArray64<uint8> Bytes = Png->GetCompressed();
	FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()), *(Dir / TEXT("stone.png")));
	FFileHelper::SaveStringToFile(TEXT("{\"animation\":{\"frametime\":2,\"interpolate\":true,\"frames\":[{\"index\":1,\"time\":4},0]}}"), *(Dir / TEXT("stone.png.mcmeta")));

	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	Textures.Unload();
	if (TestTrue(TEXT("textures load with the pack"), Textures.EnsureLoaded()))
	{
		const int32 Stone = Textures.GetLayer(TEXT("minecraft:block/stone"));
		if (TestTrue(TEXT("stone is animated by the pack"), Stone > 0 && Textures.IsAnimated(Stone)))
		{
			Textures.Tick(0.0);
			TestEqual(TEXT("the list starts with frame 1 (blue)"), Textures.GetPixels(Stone)[0], FColor(0, 0, 200, 255));
			Textures.Tick(3.0);
			TestEqual(TEXT("the first frame holds for its own time, 4 ticks, not frametime"), Textures.GetAnimationStep(Stone), 0);
			Textures.Tick(4.0);
			TestEqual(TEXT("then the second step"), Textures.GetAnimationStep(Stone), 1);
			Textures.Tick(5.0);
			// One tick into the 2 tick red step, blended towards the first listed frame (blue) again: halfway.
			const FColor Mid = Textures.GetPixels(Stone)[0];
			TestTrue(TEXT("interpolated halfway between red and blue"), FMath::Abs(Mid.R - 100) <= 2 && FMath::Abs(Mid.B - 100) <= 2);
			Textures.Tick(6.0);
			TestEqual(TEXT("the list repeats"), Textures.GetAnimationStep(Stone), 0);
		}
	}

	// Back to the plain game.
	McPacks::SetStack(MakeShared<FMcPackStack>());
	Textures.Unload();
	Textures.EnsureLoaded();
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// A pack's oak sign picture (32 pixels, as the game's) is cut into 2 by 2 tiles; its LabPBR map stone_n.png must be cut the same way, so
// each tile has its own part of the normal map (they used to be dropped for tiles, so HD Textures got no bump from packs).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackTileMapsTest, "McClient.McAssets.PackTileMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackTileMapsTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackTileMapsTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString Dir = Root / TEXT("assets/minecraft/textures/block");
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97,\"description\":\"tile maps test\"}}"), *(Root / TEXT("pack.mcmeta")));

	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	auto Write = [&](const TCHAR* Name, TFunctionRef<FColor(int32, int32)> Colour) -> bool
	{
		TArray<FColor> Pixels;
		Pixels.SetNum(32 * 32);
		for (int32 Y = 0; Y < 32; ++Y)
		{
			for (int32 X = 0; X < 32; ++X)
			{
				Pixels[Y * 32 + X] = Colour(X, Y);
			}
		}
		const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Pixels.GetData(), Pixels.Num() * 4, 32, 32, ERGBFormat::BGRA, 8))
		{
			return false;
		}
		const TArray64<uint8> Bytes = Png->GetCompressed();
		return FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()), *(Dir / Name));
	};
	// The picture is grey; the normal map's red is 200 in the top right tile (16..31, 0..15) and 50 elsewhere.
	if (!TestTrue(TEXT("the pictures are written"), Write(TEXT("oak_sign.png"), [](int32, int32) { return FColor(120, 120, 120, 255); })
		&& Write(TEXT("oak_sign_n.png"), [](int32 X, int32 Y) { return FColor(X >= 16 && Y < 16 ? 200 : 50, 128, 255, 255); })
		&& Write(TEXT("stone.png"), [](int32, int32) { return FColor(120, 120, 120, 255); })))
	{
		return false;
	}

	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	const bool bWasPbr = McPacks::GetUsePbr();
	McPacks::SetUsePbr(true);
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	Textures.Unload();
	if (TestTrue(TEXT("textures load with the pack"), Textures.EnsureLoaded()))
	{
		// The pack's stone is 32 pixels but the game's is 16, so it is scaled to a layer, not cut into tiles (a 1024-pixel
		// pack would otherwise bake 4096 quads a face); the 32-pixel sign stays 2 by 2.
		TestEqual(TEXT("a pack's bigger stone is not tiled"), McTextureTiles::GetGrid(FName("minecraft:block/stone")), FIntPoint(1, 1));
		TestEqual(TEXT("the sign is still two tiles by two"), McTextureTiles::GetGrid(FName("minecraft:block/oak_sign")), FIntPoint(2, 2));
		const int32 TopRight = Textures.GetLayer(McTextureTiles::MakeTileId(FName("minecraft:block/oak_sign"), FIntPoint(1, 0), FIntPoint(2, 2)));
		const int32 TopLeft = Textures.GetLayer(McTextureTiles::MakeTileId(FName("minecraft:block/oak_sign"), FIntPoint(0, 0), FIntPoint(2, 2)));
		if (TestTrue(TEXT("the sign picture is cut into tiles"), TopRight > 0 && TopLeft > 0 && TopRight != TopLeft)
			&& TestTrue(TEXT("both tiles have a normal map"), Textures.GetNormalPixels(TopRight).Num() > 0 && Textures.GetNormalPixels(TopLeft).Num() > 0))
		{
			TestEqual(TEXT("the top right tile has its part of the map"), Textures.GetNormalPixels(TopRight)[0].R, static_cast<uint8>(200));
			TestEqual(TEXT("the top left tile has its own"), Textures.GetNormalPixels(TopLeft)[0].R, static_cast<uint8>(50));
		}
	}

	McPacks::SetUsePbr(bWasPbr);
	McPacks::SetStack(MakeShared<FMcPackStack>());
	Textures.Unload();
	Textures.EnsureLoaded();
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// A pack's textures/colormap/grass.png and foliage.png replace the game's, and the biome colours worked out from them
// follow (plains has no colour of its own). The pack's colormaps are solid, so every lookup gives their colour.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackColormapTest, "McClient.McAssets.PackColormap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackColormapTest::RunTest(const FString& Parameters)
{
	McPacks::SetStack(MakeShared<FMcPackStack>());
	FMcBiomeColorTable& Biomes = FMcBiomeColorTable::Get();
	if (!TestTrue(TEXT("biome colours load"), Biomes.EnsureLoaded()))
	{
		return false;
	}
	const FColor GameGrass = Biomes.Get(TEXT("plains")).Grass;
	const FColor GameDryFoliage = Biomes.GetDryFoliageColormap().Sample(0.8f, 0.4f);

	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackColormapTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString Dir = Root / TEXT("assets/minecraft/textures/colormap");
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97,\"description\":\"colormap test\"}}"), *(Root / TEXT("pack.mcmeta")));
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const auto WriteSolid = [&](const TCHAR* Name, const FColor& Colour)
	{
		TArray<FColor> Pixels;
		Pixels.Init(Colour, 256 * 256);
		const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Pixels.GetData(), Pixels.Num() * 4, 256, 256, ERGBFormat::BGRA, 8))
		{
			return false;
		}
		const TArray64<uint8> Bytes = Png->GetCompressed();
		return FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()), *(Dir / Name));
	};
	const FColor PackGrass(10, 200, 30, 255), PackFoliage(200, 20, 60, 255);
	if (!TestTrue(TEXT("the colormaps are written"), WriteSolid(TEXT("grass.png"), PackGrass) && WriteSolid(TEXT("foliage.png"), PackFoliage)))
	{
		return false;
	}
	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));
	if (TestTrue(TEXT("reloaded with the pack"), Biomes.EnsureLoaded()))
	{
		TestEqual(TEXT("the grass colormap is the pack's"), Biomes.GetGrassColormap().Sample(0.1f, 0.9f), PackGrass);
		TestEqual(TEXT("the foliage colormap is the pack's"), Biomes.GetFoliageColormap().Sample(0.9f, 0.1f), PackFoliage);
		TestEqual(TEXT("plains grass follows the pack"), Biomes.Get(TEXT("plains")).Grass, PackGrass);
		TestEqual(TEXT("plains foliage follows the pack"), Biomes.Get(TEXT("plains")).Foliage, PackFoliage);
		TestEqual(TEXT("the dry foliage map the pack lacks stays the game's"), Biomes.GetDryFoliageColormap().Sample(0.8f, 0.4f), GameDryFoliage);
		TestNotEqual(TEXT("it differs from the game's"), PackGrass, GameGrass);
	}

	McPacks::SetStack(MakeShared<FMcPackStack>());
	if (TestTrue(TEXT("reloaded without it"), Biomes.EnsureLoaded()))
	{
		TestEqual(TEXT("plains grass is the game's again"), Biomes.Get(TEXT("plains")).Grass, GameGrass);
	}
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// A pack's atlases/blocks.json: an unstitch source cuts a sprite out of a sheet, and a model of the pack uses it. The
// sheet is 32 x 16, red on the left and blue on the right; the sprite zz_cut is the right half (x 1 of 2).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackAtlasTest, "McClient.McAssets.PackAtlas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackAtlasTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackAtlasTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString Assets = Root / TEXT("assets/minecraft");
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97,\"description\":\"atlas test\"}}"), *(Root / TEXT("pack.mcmeta")));
	FFileHelper::SaveStringToFile(TEXT("{\"sources\":[{\"type\":\"minecraft:unstitch\",\"resource\":\"minecraft:block/zz_sheet\",\"divisor_x\":2,\"divisor_y\":1,\"regions\":[{\"sprite\":\"minecraft:block/zz_cut\",\"x\":1,\"y\":0,\"width\":1,\"height\":1}]}]}"), *(Assets / TEXT("atlases/blocks.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"parent\":\"minecraft:block/cube_all\",\"textures\":{\"all\":\"minecraft:block/zz_cut\"}}"), *(Assets / TEXT("models/block/stone.json")));

	TArray<FColor> Pixels;
	Pixels.SetNum(32 * 16);
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		Pixels[Index] = (Index % 32) < 16 ? FColor(200, 0, 0, 255) : FColor(0, 0, 200, 255);
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!TestTrue(TEXT("a picture to write"), Png.IsValid() && Png->SetRaw(Pixels.GetData(), Pixels.Num() * 4, 32, 16, ERGBFormat::BGRA, 8)))
	{
		return false;
	}
	const TArray64<uint8> Bytes = Png->GetCompressed();
	FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()), *(Assets / TEXT("textures/block/zz_sheet.png")));

	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));
	FMcBlockModels::Get().Unload();
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	Textures.Unload();
	if (TestTrue(TEXT("textures load with the pack"), Textures.EnsureLoaded()))
	{
		const int32 Cut = Textures.GetLayer(TEXT("minecraft:block/zz_cut"));
		if (TestTrue(TEXT("the cut sprite has a layer"), Cut > 0))
		{
			TestEqual(TEXT("the sprite is the right half of the sheet"), Textures.GetPixels(Cut)[0], FColor(0, 0, 200, 255));
		}
	}

	McPacks::SetStack(MakeShared<FMcPackStack>());
	FMcBlockModels::Get().Unload();
	Textures.Unload();
	Textures.EnsureLoaded();
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
