#include "McEntityModel.h"
#include "Misc/AutomationTest.h"
#include "McHand.h"
#include "McChunk.h"
#include "McItemRegistry.h"
#include "McBlockRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// The box around a layer of a model in the rest pose, in blocks (the mesh is in Unreal units, 100 to a block, with
	// the game's y as z).
	::FBox LayerBounds(const McEntityModel::FModel& Model, int32 Layer, const McEntityModel::FAnim& Anim = McEntityModel::FAnim())
	{
		McPlayerModel::FMeshData Data;
		McEntityModel::BuildMesh(Model, Anim, Layer, Data);
		::FBox Box(Data.Positions);
		return ::FBox(Box.Min / 100.0, Box.Max / 100.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcEntityModelTest, "McClient.McRender.EntityModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcEntityModelTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;

	struct FExpectation
	{
		const TCHAR* Kind;
		int32 Layers;
		// The widest the drawing may be across, blocks: limbs and legs reach past the hitbox (the wiki's 0.6 by 1.7 for a
		// creeper), a spider's legs well past it. And the hitbox height: the model is nearly as tall.
		double MaxWidth;
		double Height;
		// How far below the feet the drawing may hang (a ghast's tentacles hang 3.6 blocks), and how far past 1 the pictures' UVs may run
		// (a strider's flat bristles have a 72 wide net on a 64 wide picture, as in the game).
		double Below = 0.07;
		double UVSlack = 0.0;
	};
	const FExpectation Expectations[] = {
		{ TEXT("minecraft:creeper"), 1, 0.8, 1.7 },
		{ TEXT("minecraft:spider"), 2, 2.5, 0.9 },
		{ TEXT("minecraft:skeleton"), 2, 1.2, 1.99 },  // its picture and the one for what it holds (a held item is 0.85 across, tilted 35 degrees up to 1.17)
		{ TEXT("minecraft:enderman"), 2, 0.8, 2.9 },
		{ TEXT("minecraft:slime"), 2, 0.55, 0.52 },
		{ TEXT("minecraft:hoglin"), 1, 2.6, 1.95 },
		{ TEXT("minecraft:magma_cube"), 1, 0.6, 0.52 },
		{ TEXT("minecraft:blaze"), 1, 1.6, 1.8 },
		{ TEXT("minecraft:strider"), 1, 2.4, 1.95, 0.07, 0.15 },
		{ TEXT("minecraft:ghast"), 1, 5.3, 4.05, 3.7 },
		{ TEXT("minecraft:endermite"), 1, 0.6, 0.25 },
		{ TEXT("minecraft:shulker"), 1, 1.1, 1.0 },
		{ TEXT("minecraft:phantom"), 1, 3.2, 0.4 },
		{ TEXT("minecraft:guardian"), 1, 3.0, 1.25, 0.3 },
	};
	for (const FExpectation& Expect : Expectations)
	{
		const FModel* Model = Find(FName(Expect.Kind));
		if (!TestNotNull(FString::Printf(TEXT("%s has a model"), Expect.Kind), Model))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s pictures"), Expect.Kind), Model->Textures.Num(), Expect.Layers);
		for (int32 Layer = 0; Layer < Expect.Layers; ++Layer)
		{
			const ::FBox Bounds = LayerBounds(*Model, Layer);
			TestTrue(FString::Printf(TEXT("%s layer %d has a mesh"), Expect.Kind, Layer), Bounds.IsValid != 0);
			// Unreal x is the game's x, y its z, z its y.
			TestTrue(FString::Printf(TEXT("%s layer %d is not too wide (%.3f by %.3f)"), Expect.Kind, Layer, Bounds.GetSize().X, Bounds.GetSize().Y), Bounds.GetSize().X <= Expect.MaxWidth && Bounds.GetSize().Y <= Expect.MaxWidth);
			TestTrue(FString::Printf(TEXT("%s layer %d stays within the hitbox height (%.3f to %.3f)"), Expect.Kind, Layer, Bounds.Min.Z, Bounds.Max.Z), Bounds.Max.Z <= Expect.Height + 0.03 && Bounds.Min.Z >= -Expect.Below);
			// UVs stay on the picture.
			McPlayerModel::FMeshData Data;
			BuildMesh(*Model, FAnim(), Layer, Data);
			bool bOnPicture = true;
			for (const FVector2D& UV : Data.UV0)
			{
				bOnPicture &= UV.X >= 0.0 && UV.X <= 1.0 + Expect.UVSlack && UV.Y >= 0.0 && UV.Y <= 1.0;
			}
			TestTrue(FString::Printf(TEXT("%s layer %d UVs are on the picture"), Expect.Kind, Layer), bOnPicture);
		}
		// Together the layers reach nearly the top of the hitbox.
		double Top = 0.0;
		for (int32 Layer = 0; Layer < Expect.Layers; ++Layer)
		{
			Top = FMath::Max(Top, LayerBounds(*Model, Layer).Max.Z);
		}
		TestTrue(FString::Printf(TEXT("%s reaches the top of its hitbox (%.3f of %.3f)"), Expect.Kind, Top, Expect.Height), Top >= Expect.Height * 0.85);
	}

	// A slime is see-through on its outer shell only; the eyes of a spider and an enderman glow.
	const FModel* Slime = Find(FName("minecraft:slime"));
	if (Slime)
	{
		TestEqual(TEXT("the slime's shell is translucent"), Slime->TranslucentLayers, 2u);
		TestTrue(TEXT("the slime grows with its hitbox"), Slime->BaseWidth > 0.0f);
		const ::FBox Core = LayerBounds(*Slime, 0);
		const ::FBox Shell = LayerBounds(*Slime, 1);
		TestTrue(TEXT("the shell is wider than the core"), Shell.GetSize().X > Core.GetSize().X);
		TestTrue(TEXT("the shell encloses the core"), Shell.IsInside(Core.Min) && Shell.IsInside(Core.Max));
	}
	TestEqual(TEXT("the spider's eyes glow"), Find(FName("minecraft:spider"))->GlowLayers, 2u);
	TestEqual(TEXT("the enderman's eyes glow"), Find(FName("minecraft:enderman"))->GlowLayers, 2u);
	TestEqual(TEXT("a creeper's picture doesn't"), Find(FName("minecraft:creeper"))->GlowLayers, 0u);

	// Walking moves the legs.
	const FModel* Creeper = Find(FName("minecraft:creeper"));
	if (Creeper)
	{
		FAnim Walking;
		Walking.WalkAmount = 1.0f;
		Walking.WalkPos = 2.0f;
		McPlayerModel::FMeshData Still, Moving;
		BuildMesh(*Creeper, FAnim(), 0, Still);
		BuildMesh(*Creeper, Walking, 0, Moving);
		TestEqual(TEXT("the same number of vertices"), Moving.Positions.Num(), Still.Positions.Num());
		bool bMoved = false;
		for (int32 Index = 0; Index < Still.Positions.Num(); ++Index)
		{
			bMoved |= !Still.Positions[Index].Equals(Moving.Positions[Index], 0.01);
		}
		TestTrue(TEXT("walking moves the creeper's legs"), bMoved);
	}
	// An enderman's strides are cut short: its legs swing no further than the limit, so they swing less than the same
	// legs without one.
	const FModel* Enderman = Find(FName("minecraft:enderman"));
	if (Enderman)
	{
		FAnim Walking;
		Walking.WalkAmount = 1.0f;
		Walking.WalkPos = 0.0f;
		FModel Unlimited = *Enderman;
		Unlimited.MaxLimbSwing = 0.0f;
		const ::FBox Limited = LayerBounds(*Enderman, 0, Walking);
		const ::FBox Free = LayerBounds(Unlimited, 0, Walking);
		TestTrue(TEXT("the enderman's strides are shorter than free swinging ones"), Limited.GetSize().Y < Free.GetSize().Y);
		// 30 pixels of leg swung 0.4 radians moves a foot by 30 sin(0.4) = 11.7 pixels, 0.73 of a block, at most.
		TestTrue(TEXT("and no longer than the limit allows"), Limited.GetSize().Y <= 1.7);
	}

	// A creeper's swell: nothing at first, then a wider, slightly taller creeper, more as the fuse burns, and a white
	// flash every other tenth.
	FVector2D Rest = GetSwellScale(0.0f);
	TestEqual(TEXT("rest width"), static_cast<float>(Rest.X), 1.0f);
	TestEqual(TEXT("rest height"), static_cast<float>(Rest.Y), 1.0f);
	const FVector2D Half = GetSwellScale(0.5f);
	TestTrue(TEXT("half way is a little wider"), Half.X > 1.0 && Half.X < 1.05);
	const FVector2D Full = GetSwellScale(1.0f);
	TestTrue(TEXT("at the end 1.4 times as wide"), Full.X > 1.38 && Full.X < 1.41);
	TestTrue(TEXT("and 1.1 times as tall"), Full.Y > 1.09 && Full.Y < 1.11);
	TestTrue(TEXT("past the end it stops"), GetSwellScale(3.0f).Equals(Full, 0.0001));
	TestFalse(TEXT("no flash at the start"), IsSwellFlash(0.05f));
	TestTrue(TEXT("flash in the second tenth"), IsSwellFlash(0.15f));
	TestFalse(TEXT("none in the third"), IsSwellFlash(0.25f));
	TestTrue(TEXT("flash near the end"), IsSwellFlash(0.95f));

	// A slime's squish: at rest it is its own shape; a hop stretches it up, landing flattens it, and both ease off.
	{
		FSquish Squish;
		TestTrue(TEXT("a resting slime is unsquished"), Squish.GetScale(0.0f, 1.0f).Equals(FVector2D(1.0, 1.0), 0.0001));
		Squish.Tick(0.4f);
		const FVector2D Up = Squish.GetScale(1.0f, 1.0f);
		TestTrue(TEXT("a jump stretches it up"), Up.Y > 1.0 && Up.X < 1.0);
		TestTrue(TEXT("its volume is kept across and up"), FMath::IsNearlyEqual(Up.X * Up.Y, 1.0, 0.0001));
		FSquish Big = FSquish();
		Big.Tick(0.4f);
		TestTrue(TEXT("a big slime squishes less"), Big.GetScale(1.0f, 4.0f).Y < Up.Y);
		Squish.Tick(-0.3f);
		Squish.Tick(0.0f);
		Squish.Tick(0.0f);
		TestTrue(TEXT("landing flattens it"), Squish.GetScale(1.0f, 1.0f).Y < 1.0);
		for (int32 Index = 0; Index < 40; ++Index)
		{
			Squish.Tick(0.0f);
		}
		TestTrue(TEXT("and it settles back"), Squish.GetScale(1.0f, 1.0f).Equals(FVector2D(1.0, 1.0), 0.01));
	}

	// The arrow: two crossed planes and a fletching, both sides, 0.9 of a block long, pointing the way it flies.
	{
		McPlayerModel::FMeshData Data;
		BuildArrowMesh(0.0f, 0.0f, Data);
		TestEqual(TEXT("arrow vertices"), Data.Positions.Num(), 24);
		TestEqual(TEXT("arrow triangles"), Data.Triangles.Num(), 36);
		bool bOnPicture = true;
		for (const FVector2D& UV : Data.UV0)
		{
			bOnPicture &= UV.X >= 0.0 && UV.X <= 0.5 && UV.Y >= 0.0 && UV.Y <= 0.32;
		}
		TestTrue(TEXT("arrow UVs are the shaft and the fletching"), bOnPicture);
		const ::FBox South(Data.Positions);
		// Facing south (yaw 0) it lies along the game's z, Unreal y.
		TestTrue(TEXT("a south arrow is about 0.9 long"), South.GetSize().Y > 80.0 && South.GetSize().Y < 100.0);
		TestTrue(TEXT("and thin"), South.GetSize().X < 40.0 && South.GetSize().Z < 40.0);
		BuildArrowMesh(90.0f, 0.0f, Data);
		const ::FBox West(Data.Positions);
		TestTrue(TEXT("yaw 90 lies along x"), West.GetSize().X > 80.0 && West.GetSize().Y < 40.0);
		BuildArrowMesh(0.0f, 90.0f, Data);
		const ::FBox Down(Data.Positions);
		TestTrue(TEXT("pitch 90 points straight down"), Down.GetSize().Z > 80.0 && Down.GetSize().Y < 40.0);
		// The tip of an arrow pitched down is below its tail.
		BuildArrowMesh(0.0f, 45.0f, Data);
		const ::FBox Tipped(Data.Positions);
		TestTrue(TEXT("a diving arrow is taller than it is wide"), Tipped.GetSize().Z > 40.0);
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHorsePoseTest, "McClient.McRender.HorsePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcHorsePoseTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	const FModel* Horse = Find(TEXT("minecraft:horse"));
	if (!TestNotNull(TEXT("The horse has a model"), Horse))
	{
		return false;
	}
	// One box of one part alone in the rest pose; its faces come in the order south, north, east, west, up, down, four
	// vertices each. The mesh is in Unreal units with the game's z as y and its y as z.
	const auto FaceCentre = [&](int32 PartIndex, int32 BoxIndex, int32 Face)
	{
		FModel Alone = *Horse;
		Alone.Parts = { Horse->Parts[PartIndex] };
		Alone.Parts[0].Boxes = { Horse->Parts[PartIndex].Boxes[BoxIndex] };
		Alone.Parts[0].Role = ERole::Static;
		McPlayerModel::FMeshData Data;
		BuildMesh(Alone, FAnim(), 0, Data);
		FVector Sum = FVector::ZeroVector;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Sum += Data.Positions[Face * 4 + Corner];
		}
		return Sum / 4.0;
	};

	// The head (part 2, box 1) lies along z: its nose end (the south face) is further forward and lower than its back end,
	// which is how a neck leaning forward carries it.
	const FVector Nose = FaceCentre(2, 1, 0);
	const FVector Back = FaceCentre(2, 1, 1);
	TestTrue(TEXT("The nose is further forward than the back of the head"), Nose.Y > Back.Y);
	TestTrue(TEXT("The nose is lower than the back of the head"), Nose.Z < Back.Z);
	// The tail hangs from its root (the top face) with its tip (the bottom) lower and further back.
	const FVector Root = FaceCentre(1, 0, 4);
	const FVector Tip = FaceCentre(1, 0, 5);
	TestTrue(TEXT("The tail's tip is lower than its root"), Tip.Z < Root.Z);
	TestTrue(TEXT("The tail's tip is further back than its root"), Tip.Y < Root.Y);

	// The mane runs up the back of the neck: its top is above the neck's top and behind the ears, and its foot is not in
	// front of the neck's front.
	const FVector ManeTop = FaceCentre(2, 3, 4);
	const FVector ManeFoot = FaceCentre(2, 3, 5);
	const FVector NeckTop = FaceCentre(2, 0, 4);
	const FVector NeckFront = FaceCentre(2, 0, 0);
	const FVector Ear = FaceCentre(2, 4, 4);
	TestTrue(TEXT("The mane's top is above the neck's top"), ManeTop.Z > NeckTop.Z - 1.0);
	TestTrue(TEXT("The mane's top is behind the ears"), ManeTop.Y < Ear.Y);
	TestTrue(TEXT("The mane's foot is behind the neck's front"), ManeFoot.Y < NeckFront.Y);
	return true;
}

#endif

// The banner's swing (fitted to the vanilla run banner3): a 100 tick cycle, 0.033 rad either way of 0.058.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBannerSwingTest, "Mc.EntityModel.BannerSwing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcBannerSwingTest::RunTest(const FString&)
{
	float Min = 10.0f, Max = -10.0f;
	for (int32 Tick = 0; Tick < 100; ++Tick)
	{
		const float Tilt = McEntityModel::GetBannerTilt(Tick);
		Min = FMath::Min(Min, Tilt);
		Max = FMath::Max(Max, Tilt);
		TestEqual(TEXT("repeats every 100 ticks"), McEntityModel::GetBannerTilt(Tick + 100), Tilt, 1.0e-4f);
	}
	TestEqual(TEXT("most back"), Max, 0.091f, 2.0e-3f);
	TestEqual(TEXT("most forward"), Min, 0.025f, 2.0e-3f);
	// A tilted flag's foot moves back and its top stays: the lowest vertex of the flag is further from the face.
	McPlayerModel::FMeshData Straight, Swung;
	McEntityModel::BuildBannerMesh(false, true, 0.0f, Straight, 0.0f);
	McEntityModel::BuildBannerMesh(false, true, 0.0f, Swung, 0.1f);
	TestEqual(TEXT("same vertices"), Swung.Positions.Num(), Straight.Positions.Num());
	double LowStraight = 1.0e9, LowSwung = 1.0e9, ZStraight = 0.0, ZSwung = 0.0, TopStraight = -1.0e9, TopSwung = -1.0e9;
	for (int32 Index = 0; Index < Straight.Positions.Num(); ++Index)
	{
		if (Straight.Positions[Index].Z < LowStraight) { LowStraight = Straight.Positions[Index].Z; ZStraight = Straight.Positions[Index].Y; }
		if (Swung.Positions[Index].Z < LowSwung) { LowSwung = Swung.Positions[Index].Z; ZSwung = Swung.Positions[Index].Y; }
		TopStraight = FMath::Max(TopStraight, Straight.Positions[Index].Z);
		TopSwung = FMath::Max(TopSwung, Swung.Positions[Index].Z);
	}
	TestTrue(TEXT("the foot moves (back, along z)"), FMath::Abs(ZSwung - ZStraight) > 0.5);
	TestEqual(TEXT("the top stays (to a hair)"), TopSwung, TopStraight, 2.0);
	return true;
}

// A dropped item (fitted from same-camera shots of the game): a picture stood one pixel thick, spinning 1 radian a second,
// bobbing about 0.096 of a block with a period of pi seconds, and a stack shown as several copies.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDroppedItemTest, "Mc.EntityModel.Dropped", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcDroppedItemTest::RunTest(const FString&)
{
	TArray<FColor> Pixels;
	Pixels.Init(FColor::White, 4);
	McPlayerModel::FMeshData Unit;
	McHand::BuildExtrudedMesh(Pixels, 2, 1.0f, Unit);
	TestTrue(TEXT("the picture has a thickness (faces on the edges)"), Unit.Positions.Num() > 8);

	TestEqual(TEXT("bob repeats every pi seconds"), McEntityModel::GetDroppedHeight(0.4f + PI, 0.0f), McEntityModel::GetDroppedHeight(0.4f, 0.0f), 1.0e-4f);
	float Low = 10.0f, High = -10.0f;
	for (int32 Step = 0; Step < 64; ++Step)
	{
		const float Height = McEntityModel::GetDroppedHeight(Step * 0.05f, 0.0f);
		Low = FMath::Min(Low, Height);
		High = FMath::Max(High, Height);
	}
	TestEqual(TEXT("bob is 0.096 either side"), (High - Low) * 0.5f, 0.096f, 3.0e-3f);

	// Facing along y at age 0 (a width of half a block across y); a quarter turn later, edge on.
	const auto Extent = [](const McPlayerModel::FMeshData& Mesh, int32 Axis)
	{
		double Min = 1.0e9, Max = -1.0e9;
		for (const FVector& Position : Mesh.Positions)
		{
			Min = FMath::Min(Min, Position[Axis]);
			Max = FMath::Max(Max, Position[Axis]);
		}
		return Max - Min;
	};
	McPlayerModel::FMeshData One, Quarter, Stack;
	McEntityModel::BuildDroppedMesh(Unit, 0.0f, 1, 0.5f, 0.0f, One);
	McEntityModel::BuildDroppedMesh(Unit, PI * 0.5f, 1, 0.5f, 0.0f, Quarter);
	TestEqual(TEXT("half a block across"), Extent(One, 1), 0.5 * McCoords::UnitsPerBlock, 0.5);
	TestEqual(TEXT("one pixel thick at half size"), Extent(One, 0), 0.5 / 16.0 * McCoords::UnitsPerBlock, 0.5);
	TestEqual(TEXT("a quarter turn (pi/2 s) puts the width along x"), Extent(Quarter, 0), 0.5 * McCoords::UnitsPerBlock, 0.5);
	TestEqual(TEXT("and the thickness along y"), Extent(Quarter, 1), 0.5 / 16.0 * McCoords::UnitsPerBlock, 0.5);

	McEntityModel::BuildDroppedMesh(Unit, 0.0f, 64, 0.5f, 0.0f, Stack);
	TestEqual(TEXT("64 show five copies"), Stack.Positions.Num(), One.Positions.Num() * 5);
	TestEqual(TEXT("with their triangles"), Stack.Triangles.Num(), One.Triangles.Num() * 5);
	TestTrue(TEXT("a stack is wider than one"), Extent(Stack, 1) > Extent(One, 1));
	TestTrue(TEXT("and taller"), Extent(Stack, 2) > Extent(One, 2));
	return true;
}

// Nothing a player can drop may be invisible: every block item gives a mesh (a cube, its baked model, or the flat picture).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDroppedBlockItemsTest, "Mc.EntityModel.DroppedBlockItems", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcDroppedBlockItemsTest::RunTest(const FString&)
{
	if (!FMcItemRegistry::Get().EnsureLoaded() || !FMcBlockRegistry::Get().EnsureLoaded())
	{
		AddError(TEXT("registries did not load"));
		return false;
	}
	int32 Checked = 0, Models = 0, Empty = 0;
	for (const FMcItemDef& Item : FMcItemRegistry::Get().GetAll())
	{
		if (Item.Name == FName(TEXT("minecraft:air")) || !FMcBlockRegistry::Get().FindDefaultState(Item.Name).IsSet())
		{
			continue;
		}
		McEntityModel::FDroppedUnit Unit;
		const bool bBuilt = McEntityModel::BuildDroppedUnit(Item.Name, Unit);
		++Checked;
		Models += Unit.bModel ? 1 : 0;
		if (!bBuilt || Unit.Sides.Positions.IsEmpty() || Unit.Sides.Triangles.IsEmpty())
		{
			++Empty;
			if (Empty <= 10)
			{
				AddError(FString::Printf(TEXT("%s drops as nothing"), *Item.Name.ToString()));
			}
		}
	}
	TestTrue(TEXT("block items were checked"), Checked > 100);
	TestTrue(TEXT("some are drawn as their model"), Models > 20);
	AddInfo(FString::Printf(TEXT("%d block items, %d as models, %d empty"), Checked, Models, Empty));
	return true;
}
