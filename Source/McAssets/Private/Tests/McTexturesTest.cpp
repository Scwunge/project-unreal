#include "McBiomeColors.h"
#include "McBlockTextures.h"
#include "Engine/Texture2DArray.h"
#include "Misc/AutomationTest.h"

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
	TestEqual(TEXT("layer 0 plus one per loaded texture"), Textures.NumLayers(), Stats.Loaded + 1);
	TestEqual(TEXT("every id is loaded or listed as a fallback"), Stats.Loaded + Stats.Fallbacks.Num(), Stats.Ids);

	UTexture2DArray* Array = Textures.GetTextureArray();
	if (TestNotNull(TEXT("the texture array exists"), Array))
	{
		TestEqual(TEXT("array slices"), Array->GetArraySize(), Textures.NumLayers());
		TestEqual(TEXT("array width"), Array->GetSizeX(), 16);
		TestEqual(TEXT("array mips"), Array->GetNumMips(), FMcBlockTextures::NumMips);
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

#endif
