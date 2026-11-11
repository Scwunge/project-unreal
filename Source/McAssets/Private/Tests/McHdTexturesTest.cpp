// HD Textures' pure parts: the scaler, the class picker and the normal and specular maps.

#include "McHdTextures.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	McHd::FPicture MakeSolid(int32 Size, const FColor& Colour)
	{
		McHd::FPicture Picture;
		Picture.Width = Picture.Height = Size;
		Picture.Pixels.Init(Colour, Size * Size);
		return Picture;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHdScalerTest, "McClient.McAssets.HdScaler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcHdScalerTest::RunTest(const FString& Parameters)
{
	using namespace McHd;
	// A flat picture stays flat, at twice the size.
	const FPicture Flat = Upscale2x(MakeSolid(4, FColor(10, 120, 200, 255)), true);
	TestEqual(TEXT("width doubles"), Flat.Width, 8);
	TestEqual(TEXT("height doubles"), Flat.Height, 8);
	bool bAllSame = true;
	for (const FColor& Pixel : Flat.Pixels)
	{
		bAllSame &= Pixel == FColor(10, 120, 200, 255);
	}
	TestTrue(TEXT("a flat colour stays flat"), bAllSame);

	// A hard vertical edge stays hard: black left, white right, no grey in between.
	FPicture Edge;
	Edge.Width = Edge.Height = 8;
	Edge.Pixels.SetNumUninitialized(64);
	for (int32 Y = 0; Y < 8; ++Y)
	{
		for (int32 X = 0; X < 8; ++X)
		{
			Edge.Pixels[Y * 8 + X] = X < 4 ? FColor(0, 0, 0, 255) : FColor(255, 255, 255, 255);
		}
	}
	const FPicture EdgeBig = Upscale2x(Edge, false);
	bool bHard = true;
	for (const FColor& Pixel : EdgeBig.Pixels)
	{
		bHard &= Pixel.R == 0 || Pixel.R == 255;
	}
	TestTrue(TEXT("a hard edge does not blur"), bHard);

	// A gradient becomes smoother: the largest step between neighbours falls.
	FPicture Ramp;
	Ramp.Width = Ramp.Height = 8;
	Ramp.Pixels.SetNumUninitialized(64);
	for (int32 Y = 0; Y < 8; ++Y)
	{
		for (int32 X = 0; X < 8; ++X)
		{
			const uint8 Value = static_cast<uint8>(X * 30);
			Ramp.Pixels[Y * 8 + X] = FColor(Value, Value, Value, 255);
		}
	}
	const FPicture RampBig = Upscale2x(Ramp, false);
	int32 BiggestStep = 0;
	for (int32 X = 1; X < RampBig.Width; ++X)
	{
		BiggestStep = FMath::Max(BiggestStep, FMath::Abs(static_cast<int32>(RampBig.Pixels[X].R) - RampBig.Pixels[X - 1].R));
	}
	TestTrue(*FString::Printf(TEXT("a gradient is smoothed (step %d, was 30)"), BiggestStep), BiggestStep < 30);

	// A diagonal corner is rounded: in this 3x3, a lone dark pixel's neighbours up and left agree, so the corner of the
	// light centre pixel facing them takes their colour rather than staying centre.
	FPicture Corner;
	Corner.Width = Corner.Height = 3;
	const FColor Dark(0, 0, 0, 255), Light(255, 255, 255, 255);
	Corner.Pixels = { Light, Dark, Light,
	                  Dark, Light, Light,
	                  Light, Light, Light };
	const FPicture CornerBig = Upscale2x(Corner, false);
	// The centre pixel is (1,1): its top-left sub-pixel is (2,2) in the doubled picture.
	TestTrue(TEXT("the corner between two dark neighbours is darker than the centre"), CornerBig.Pixels[2 * 6 + 2].R < 255);

	// Transparency survives.
	const FPicture Clear = Upscale2x(MakeSolid(4, FColor(0, 0, 0, 0)), true);
	TestEqual(TEXT("alpha stays zero"), static_cast<int32>(Clear.Pixels[5].A), 0);

	// ScaleTo: a power of two uses the doubling, and shrinking uses the box filter.
	const FPicture Up = ScaleTo(MakeSolid(16, FColor(1, 2, 3, 255)), 64, 64, true);
	TestTrue(TEXT("16 to 64"), Up.Width == 64 && Up.Height == 64 && Up.Pixels.Num() == 4096);
	const FPicture Down = ScaleTo(MakeSolid(64, FColor(1, 2, 3, 255)), 16, 16, true);
	TestTrue(TEXT("64 to 16"), Down.Width == 16 && Down.Pixels[0] == FColor(1, 2, 3, 255));
	const FPicture Odd = ScaleTo(MakeSolid(16, FColor(9, 9, 9, 255)), 40, 40, true);
	TestTrue(TEXT("a size that isn't a power of two"), Odd.Width == 40 && Odd.Pixels[100] == FColor(9, 9, 9, 255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHdClassTest, "McClient.McAssets.HdClasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcHdClassTest::RunTest(const FString& Parameters)
{
	using namespace McHd;
	TestEqual(TEXT("stone"), PickClass(TEXT("minecraft:block/stone")), EMaterialClass::Stone);
	TestEqual(TEXT("cobblestone"), PickClass(TEXT("minecraft:block/cobblestone")), EMaterialClass::Stone);
	TestEqual(TEXT("log end"), PickClass(TEXT("minecraft:block/oak_log_top")), EMaterialClass::Wood);
	TestEqual(TEXT("planks"), PickClass(TEXT("minecraft:block/birch_planks")), EMaterialClass::Wood);
	TestEqual(TEXT("leaves"), PickClass(TEXT("minecraft:block/oak_leaves")), EMaterialClass::Leaves);
	TestEqual(TEXT("wool"), PickClass(TEXT("minecraft:block/white_wool")), EMaterialClass::Wool);
	TestEqual(TEXT("ore"), PickClass(TEXT("minecraft:block/diamond_ore")), EMaterialClass::Ore);
	TestEqual(TEXT("deepslate ore"), PickClass(TEXT("minecraft:block/deepslate_iron_ore")), EMaterialClass::Ore);
	TestEqual(TEXT("metal block"), PickClass(TEXT("minecraft:block/iron_block")), EMaterialClass::Metal);
	TestEqual(TEXT("glass"), PickClass(TEXT("minecraft:block/glass")), EMaterialClass::Glass);
	TestEqual(TEXT("stained glass"), PickClass(TEXT("minecraft:block/red_stained_glass")), EMaterialClass::Glass);
	TestEqual(TEXT("a plant"), PickClass(TEXT("minecraft:block/short_grass")), EMaterialClass::Plant);
	TestEqual(TEXT("grass block is ground"), PickClass(TEXT("minecraft:block/grass_block_top")), EMaterialClass::Soil);
	TestEqual(TEXT("dirt"), PickClass(TEXT("minecraft:block/dirt")), EMaterialClass::Soil);
	TestEqual(TEXT("sand"), PickClass(TEXT("minecraft:block/sand")), EMaterialClass::Soil);
	TestEqual(TEXT("sandstone is stone"), PickClass(TEXT("minecraft:block/sandstone_top")), EMaterialClass::Stone);
	TestEqual(TEXT("a gem block"), PickClass(TEXT("minecraft:block/diamond_block")), EMaterialClass::Gem);
	TestEqual(TEXT("a mob"), PickClass(TEXT("entity/zombie/zombie")), EMaterialClass::Fabric);
	TestEqual(TEXT("something unknown"), PickClass(TEXT("minecraft:block/mystery_thing")), EMaterialClass::Generic);

	// Tags win over names, and a face suffix is looked past.
	FClassTags Tags;
	Tags.Add(FName(TEXT("mystery_thing")), EMaterialClass::Metal);
	TestEqual(TEXT("a tag decides"), PickClass(TEXT("minecraft:block/mystery_thing"), &Tags), EMaterialClass::Metal);
	TestEqual(TEXT("through a face suffix"), PickClass(TEXT("minecraft:block/mystery_thing_top"), &Tags), EMaterialClass::Metal);

	TestTrue(TEXT("glowstone gives light"), IsEmissiveTexture(TEXT("minecraft:block/glowstone")));
	TestFalse(TEXT("stone does not"), IsEmissiveTexture(TEXT("minecraft:block/stone")));
	TestFalse(TEXT("an unlit lamp does not"), IsEmissiveTexture(TEXT("minecraft:block/redstone_lamp")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHdMapsTest, "McClient.McAssets.HdMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcHdMapsTest::RunTest(const FString& Parameters)
{
	using namespace McHd;
	// Normals from a height field: flat is straight up, a ramp tilts away from the high side.
	TArray<float> Flat;
	Flat.Init(0.5f, 16);
	TArray<FColor> Normal;
	NormalFromHeight(Flat, 4, 4, 1.0f, Normal);
	TestTrue(TEXT("flat is x 128 y 128"), FMath::Abs(Normal[5].R - 128) <= 1 && FMath::Abs(Normal[5].G - 128) <= 1);
	// A hill in the middle of 8x8: the slope on its right side faces right (x above 128).
	TArray<float> Hill;
	Hill.Init(0.0f, 64);
	for (int32 Y = 2; Y < 6; ++Y)
	{
		for (int32 X = 2; X < 6; ++X)
		{
			Hill[Y * 8 + X] = 1.0f;
		}
	}
	NormalFromHeight(Hill, 8, 8, 1.0f, Normal);
	TestTrue(TEXT("right of the hill faces right"), Normal[3 * 8 + 6].R > 128);
	TestTrue(TEXT("left of the hill faces left"), Normal[3 * 8 + 1].R < 128);
	// LabPBR is DirectX: green rises DOWN the picture.
	TestTrue(TEXT("below the hill faces down the picture (green above 128, DirectX)"), Normal[6 * 8 + 3].G > 128);
	TestTrue(TEXT("above it faces up (green below 128)"), Normal[1 * 8 + 3].G < 128);
	TestEqual(TEXT("height goes to alpha"), static_cast<int32>(Normal[3 * 8 + 3].A), 255);

	// Generating a stone: 16 -> 64, with all three pictures the same size, deterministic, with detail.
	FPicture Stone;
	Stone.Width = Stone.Height = 16;
	Stone.Pixels.Init(FColor(125, 125, 125, 255), 256);
	const FMaps A = Generate(TEXT("minecraft:block/stone"), Stone, 4, EMaterialClass::Stone, false, true);
	const FMaps B = Generate(TEXT("minecraft:block/stone"), Stone, 4, EMaterialClass::Stone, false, true);
	TestTrue(TEXT("albedo is 64x64"), A.Albedo.Width == 64 && A.Albedo.Height == 64 && A.Albedo.Pixels.Num() == 4096);
	TestTrue(TEXT("normal and specular too"), A.Normal.Pixels.Num() == 4096 && A.Specular.Pixels.Num() == 4096);
	TestTrue(TEXT("the same every time"), A.Albedo.Pixels == B.Albedo.Pixels && A.Normal.Pixels == B.Normal.Pixels);
	int32 MinR = 255, MaxR = 0, BumpyNormals = 0;
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		MinR = FMath::Min<int32>(MinR, A.Albedo.Pixels[Index].R);
		MaxR = FMath::Max<int32>(MaxR, A.Albedo.Pixels[Index].R);
		BumpyNormals += FMath::Abs(A.Normal.Pixels[Index].R - 128) > 4 ? 1 : 0;
		if (A.Specular.Pixels[Index].A != 255)
		{
			AddError(TEXT("a stone is not emissive"));
			break;
		}
	}
	TestTrue(*FString::Printf(TEXT("the detail varies a flat grey (%d to %d)"), MinR, MaxR), MaxR - MinR > 10);
	TestTrue(TEXT("and bumps the normals"), BumpyNormals > 200);

	// Different textures get different detail.
	const FMaps Other = Generate(TEXT("minecraft:block/andesite"), Stone, 4, EMaterialClass::Stone, false, true);
	TestTrue(TEXT("another id, another pattern"), Other.Albedo.Pixels != A.Albedo.Pixels);

	// Glass stays clear and shiny; metal is metal; an emissive texture gets a mask; wool is rough.
	FPicture Pane;
	Pane.Width = Pane.Height = 16;
	Pane.Pixels.Init(FColor(200, 220, 255, 60), 256);
	const FMaps Glass = Generate(TEXT("minecraft:block/glass"), Pane, 4, EMaterialClass::Glass, false, true);
	TestEqual(TEXT("glass keeps its alpha"), static_cast<int32>(Glass.Albedo.Pixels[100].A), 60);
	TestTrue(TEXT("glass is smooth"), Glass.Specular.Pixels[100].R > 200);
	const FMaps Iron = Generate(TEXT("minecraft:block/iron_block"), Stone, 4, EMaterialClass::Metal, false, true);
	TestTrue(TEXT("metal is metal (F0 over 229)"), Iron.Specular.Pixels[100].G >= 230);
	const FMaps Wool = Generate(TEXT("minecraft:block/white_wool"), Stone, 4, EMaterialClass::Wool, false, true);
	TestTrue(TEXT("wool is rough"), Wool.Specular.Pixels[100].R < 40);
	FPicture Bright;
	Bright.Width = Bright.Height = 16;
	Bright.Pixels.Init(FColor(250, 230, 150, 255), 256);
	const FMaps Lamp = Generate(TEXT("minecraft:block/glowstone"), Bright, 4, EMaterialClass::Generic, true, true);
	TestTrue(TEXT("a bright emissive texture glows"), Lamp.Specular.Pixels[100].A < 100);

	// An ore's gems (pixels unlike the stone) are shinier than the stone around them.
	FPicture Ore = Stone;
	for (int32 Y = 6; Y < 10; ++Y)
	{
		for (int32 X = 6; X < 10; ++X)
		{
			Ore.Pixels[Y * 16 + X] = FColor(40, 220, 230, 255);
		}
	}
	const FMaps Diamond = Generate(TEXT("minecraft:block/diamond_ore"), Ore, 4, EMaterialClass::Ore, false, true);
	const int32 Centre = 32 * 64 + 32;
	const int32 Corner = 4 * 64 + 4;
	TestTrue(TEXT("gems shine, stone doesn't"), Diamond.Specular.Pixels[Centre].R > Diamond.Specular.Pixels[Corner].R + 80);

	// A size that is not square (a skin) works; the weave detail is gentle.
	FPicture Skin;
	Skin.Width = 64;
	Skin.Height = 32;
	Skin.Pixels.Init(FColor(180, 140, 110, 255), 64 * 32);
	const FMaps Fabric = Generate(TEXT("entity/player/steve"), Skin, 2, EMaterialClass::Fabric, false, false);
	TestTrue(TEXT("a skin doubles in both directions"), Fabric.Albedo.Width == 128 && Fabric.Albedo.Height == 64 && Fabric.Normal.Pixels.Num() == 128 * 64);
	return true;
}

#endif
