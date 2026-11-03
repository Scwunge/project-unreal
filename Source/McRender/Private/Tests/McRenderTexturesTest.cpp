#include "McRenderTextures.h"
#include "McCache.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRenderTexturesTest, "McClient.McRender.LowDepthPalettePng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcRenderTexturesTest::RunTest(const FString& Parameters)
{
	// The shepherd's clothes are a 4 bit palette picture: its hat is 96 opaque pixels, none of them black, and the rest
	// of the hat area is see-through.
	const FString Path = McCache::Asset(TEXT("textures/entity/villager/profession/shepherd.png"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(TEXT("No cache/: skipped"));
		return true;
	}
	TArray<FColor> Pixels;
	int32 Width = 0, Height = 0;
	TestTrue(TEXT("Loads"), McRenderTextures::LoadPngPixels(Path, Pixels, Width, Height));
	TestEqual(TEXT("Width"), Width, 64);
	TestEqual(TEXT("Height"), Height, 64);
	if (Pixels.Num() != 64 * 64)
	{
		return false;
	}
	int32 Opaque = 0, Black = 0;
	for (int32 Y = 0; Y < 16; ++Y)
	{
		for (int32 X = 32; X < 64; ++X)
		{
			const FColor Pixel = Pixels[Y * Width + X];
			Opaque += Pixel.A > 0 ? 1 : 0;
			Black += Pixel.A > 0 && Pixel.R == 0 && Pixel.G == 0 && Pixel.B == 0 ? 1 : 0;
		}
	}
	TestEqual(TEXT("Hat pixels"), Opaque, 96);
	TestEqual(TEXT("Black hat pixels"), Black, 0);
	const FColor Cap = Pixels[10 * Width + 32];
	TestTrue(TEXT("A hat pixel's colour"), Cap.R == 97 && Cap.G == 56 && Cap.B == 45 && Cap.A == 255);
	TestEqual(TEXT("See-through elsewhere"), static_cast<int32>(Pixels[8 * Width + 40].A), 0);

	// None of the villager's pictures has an opaque black pixel (the fisherman's has one), so one that loads with them is
	// wrongly read.
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *McCache::Asset(TEXT("textures/entity/villager")), TEXT("*.png"), true, false);
	TestTrue(TEXT("Found the villager pictures"), Files.Num() > 20);
	for (const FString& File : Files)
	{
		TArray<FColor> Picture;
		int32 W = 0, H = 0;
		if (!McRenderTextures::LoadPngPixels(File, Picture, W, H))
		{
			AddError(FString::Printf(TEXT("%s doesn't load"), *File));
			continue;
		}
		int32 Dark = 0;
		for (const FColor& Pixel : Picture)
		{
			Dark += Pixel.A > 0 && Pixel.R == 0 && Pixel.G == 0 && Pixel.B == 0 ? 1 : 0;
		}
		TestTrue(FString::Printf(TEXT("%s has no black"), *FPaths::GetCleanFilename(File)), Dark <= 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCompositeOverTest, "McClient.McRender.CompositeOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcCompositeOverTest::RunTest(const FString& Parameters)
{
	// A see-through layer over a coloured one leaves the colour (it used to come out black), a solid one covers it, and a
	// half-solid one mixes.
	TArray<FColor> Under = { FColor(200, 100, 50, 255), FColor(200, 100, 50, 255), FColor(200, 100, 50, 255), FColor(0, 0, 0, 0), FColor(10, 20, 30, 0) };
	const TArray<FColor> Over = { FColor(0, 0, 0, 0), FColor(10, 20, 30, 255), FColor(100, 100, 100, 128), FColor(1, 2, 3, 255), FColor(0, 0, 0, 0) };
	McRenderTextures::CompositeOver(Under, Over);
	TestTrue(TEXT("See-through keeps the colour"), Under[0] == FColor(200, 100, 50, 255));
	TestTrue(TEXT("Solid covers"), Under[1] == FColor(10, 20, 30, 255));
	TestTrue(TEXT("Half mixes"), FMath::Abs(static_cast<int32>(Under[2].R) - 150) <= 1 && FMath::Abs(static_cast<int32>(Under[2].G) - 100) <= 1 && Under[2].A == 255);
	TestTrue(TEXT("Solid over nothing"), Under[3] == FColor(1, 2, 3, 255));
	TestEqual(TEXT("Nothing over nothing"), static_cast<int32>(Under[4].A), 0);

	// The badge picture is see-through nearly everywhere: laid over the shepherd's clothes, the hat survives.
	const FString Clothes = McCache::Asset(TEXT("textures/entity/villager/profession/shepherd.png"));
	const FString Badge = McCache::Asset(TEXT("textures/entity/villager/profession_level/stone.png"));
	if (IFileManager::Get().FileExists(*Clothes) && IFileManager::Get().FileExists(*Badge))
	{
		TArray<FColor> Base, Level;
		int32 W = 0, H = 0;
		TestTrue(TEXT("Clothes load"), McRenderTextures::LoadPngPixels(Clothes, Base, W, H));
		TestTrue(TEXT("Badge loads"), McRenderTextures::LoadPngPixels(Badge, Level, W, H));
		const FColor Hat = Base[10 * W + 32];
		McRenderTextures::CompositeOver(Base, Level);
		TestTrue(TEXT("The hat survives the badge"), Base[10 * W + 32] == Hat && Hat.A == 255);
	}
	return true;
}

#endif
