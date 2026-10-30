#include "McBell.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McConduit.h"
#include "McDimensionType.h"
#include "McChestComponent.h"
#include "McEntityModel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcChestModelTest, "McClient.McRender.ChestModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcChestModelTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	const auto Pick = [&](const TCHAR* Text, float& Yaw, FIntVector& Partner, bool bChristmas = false)
	{
		Yaw = -1.0f;
		Partner = FIntVector(9);
		return UMcChestComponent::GetChestModel(Registry.ParseState(Text).Get(0), Yaw, Partner, bChristmas);
	};
	float Yaw;
	FIntVector Partner;
	TestEqual(TEXT("a single chest"), Pick(TEXT("minecraft:chest[facing=north,type=single,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/normal")));
	TestEqual(TEXT("facing north is unturned"), Yaw, 0.0f);
	TestEqual(TEXT("a single chest has no other half"), Partner, FIntVector::ZeroValue);
	Pick(TEXT("minecraft:chest[facing=east,type=single,waterlogged=false]"), Yaw, Partner);
	TestEqual(TEXT("facing east turns 90"), Yaw, 90.0f);
	// The half whose other half is on its +x side (turned with it) wears the _left picture, whichever the type says.
	TestEqual(TEXT("north, right: the other half is east"), Pick(TEXT("minecraft:chest[facing=north,type=right,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/normal_left")));
	TestEqual(TEXT("east of it"), Partner, FIntVector(1, 0, 0));
	TestEqual(TEXT("north, left"), Pick(TEXT("minecraft:chest[facing=north,type=left,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/normal_right")));
	TestEqual(TEXT("south, right: turned, the same picture"), Pick(TEXT("minecraft:chest[facing=south,type=right,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/normal_left")));
	TestEqual(TEXT("a trapped chest"), Pick(TEXT("minecraft:trapped_chest[facing=north,type=single,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/trapped")));
	TestEqual(TEXT("an ender chest"), Pick(TEXT("minecraft:ender_chest[facing=north,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/ender")));
	TestEqual(TEXT("a waxed weathered copper chest"), Pick(TEXT("minecraft:waxed_weathered_copper_chest[facing=north,type=single,waterlogged=false]"), Yaw, Partner),
		FName(TEXT("mc:chest/copper_weathered")));
	TestEqual(TEXT("at Christmas"), Pick(TEXT("minecraft:chest[facing=north,type=single,waterlogged=false]"), Yaw, Partner, true), FName(TEXT("mc:chest/christmas")));
	TestEqual(TEXT("an ender chest isn't Christmassy"), Pick(TEXT("minecraft:ender_chest[facing=north,waterlogged=false]"), Yaw, Partner, true), FName(TEXT("mc:chest/ender")));
	TestTrue(TEXT("stone is no chest"), Pick(TEXT("minecraft:stone"), Yaw, Partner).IsNone());

	for (const TCHAR* Kind : { TEXT("mc:chest/normal"), TEXT("mc:chest/normal_left"), TEXT("mc:chest/copper_oxidized_right"), TEXT("mc:chest/ender") })
	{
		TestNotNull(FString::Printf(TEXT("%s has a model"), Kind), Find(FName(Kind)));
	}
	const FModel* Chest = Find(TEXT("mc:chest/normal"));
	if (!Chest)
	{
		return false;
	}
	FAnim Shut;
	McPlayerModel::FMeshData Data;
	BuildMesh(*Chest, Shut, 0, Data);
	const ::FBox Box(Data.Positions);
	// 14 across, a pixel in from each side, and 14 high; the lock stands a pixel out at the front (north, Unreal -Y).
	TestTrue(TEXT("14 across"), FMath::IsNearlyEqual(Box.Min.X, -43.75, 0.1) && FMath::IsNearlyEqual(Box.Max.X, 43.75, 0.1));
	TestTrue(TEXT("14 high"), FMath::IsNearlyEqual(Box.Min.Z, 0.0, 0.1) && FMath::IsNearlyEqual(Box.Max.Z, 87.5, 0.1));
	TestTrue(TEXT("the lock at the north"), FMath::IsNearlyEqual(Box.Min.Y, -50.0, 0.1) && FMath::IsNearlyEqual(Box.Max.Y, 43.75, 0.1));
	// The top of the lid is the light square of the picture's top row (28 to 42 across), not the dark inside of the lid.
	bool bLightTop = false;
	bool bDarkTop = false;
	for (int32 Index = 0; Index + 3 < Data.Positions.Num(); Index += 4)
	{
		if (Data.Normals[Index].Z > 0.9 && FMath::IsNearlyEqual(Data.Positions[Index].Z, 87.5, 0.1))
		{
			const FVector2D Middle = (Data.UV0[Index] + Data.UV0[Index + 2]) * 0.5 * 64.0;
			bLightTop |= Middle.X > 28.0 && Middle.X < 42.0 && Middle.Y < 14.0;
			bDarkTop |= Middle.X > 14.0 && Middle.X < 28.0 && Middle.Y < 14.0;
		}
	}
	TestTrue(TEXT("the lid's top is the outside of the lid"), bLightTop && !bDarkTop);
	FAnim Open;
	Open.Lid = 1.0f;
	BuildMesh(*Chest, Open, 0, Data);
	const ::FBox Opened(Data.Positions);
	// Swung up about its back edge: it stands 14 (and the lock 1) above the hinge at 9, over the back.
	TestTrue(TEXT("the open lid stands up"), Opened.Max.Z > 140.0);
	TestTrue(TEXT("and leans back, not forward"), Opened.Min.Y > -45.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcShulkerModelTest, "McClient.McRender.ShulkerBoxModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcShulkerModelTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	FQuat Rotation;
	const auto Pick = [&](const TCHAR* Text) { return UMcChestComponent::GetShulkerModel(Registry.ParseState(Text).Get(0), Rotation); };
	TestEqual(TEXT("the plain box"), Pick(TEXT("minecraft:shulker_box[facing=up]")), FName(TEXT("mc:shulker_box/shulker")));
	TestTrue(TEXT("facing up is unturned"), Rotation.Equals(FQuat::Identity));
	TestEqual(TEXT("a light blue box"), Pick(TEXT("minecraft:light_blue_shulker_box[facing=north]")), FName(TEXT("mc:shulker_box/shulker_light_blue")));
	// The lid (Unreal +z) turned to the north is Unreal -y; to the east, +x; down, -z.
	TestTrue(TEXT("facing north, the lid is to the north"), Rotation.RotateVector(FVector::UpVector).Equals(FVector(0, -1, 0), 1e-6));
	Pick(TEXT("minecraft:shulker_box[facing=east]"));
	TestTrue(TEXT("facing east, to the east"), Rotation.RotateVector(FVector::UpVector).Equals(FVector(1, 0, 0), 1e-6));
	Pick(TEXT("minecraft:shulker_box[facing=south]"));
	TestTrue(TEXT("facing south, to the south"), Rotation.RotateVector(FVector::UpVector).Equals(FVector(0, 1, 0), 1e-6));
	Pick(TEXT("minecraft:shulker_box[facing=down]"));
	TestTrue(TEXT("facing down, below"), Rotation.RotateVector(FVector::UpVector).Equals(FVector(0, 0, -1), 1e-6));
	TestTrue(TEXT("a chest is no shulker box"), Pick(TEXT("minecraft:chest[facing=north,type=single,waterlogged=false]")).IsNone());
	float Yaw;
	FIntVector Partner;
	TestTrue(TEXT("nor is a shulker box a chest"), UMcChestComponent::GetChestModel(Registry.ParseState(TEXT("minecraft:shulker_box[facing=up]")).Get(0), Yaw, Partner).IsNone());

	const FModel* Box = Find(TEXT("mc:shulker_box/shulker_red"));
	if (!TestNotNull(TEXT("a red box has a model"), Box))
	{
		return false;
	}
	FAnim Shut;
	McPlayerModel::FMeshData Data;
	BuildMesh(*Box, Shut, 0, Data);
	const ::FBox Bounds(Data.Positions);
	TestTrue(TEXT("shut, it fills the block about its middle"), Bounds.Min.Equals(FVector(-50.0), 0.2) && Bounds.Max.Equals(FVector(50.0), 0.2));
	// The lid's top is the plain square of the picture's top row (16 to 32 across).
	bool bLidTop = false;
	for (int32 Index = 0; Index + 3 < Data.Positions.Num(); Index += 4)
	{
		if (Data.Normals[Index].Z > 0.9 && Data.Positions[Index].Z > 49.0)
		{
			const FVector2D Middle = (Data.UV0[Index] + Data.UV0[Index + 2]) * 0.5 * 64.0;
			bLidTop |= Middle.X > 16.0 && Middle.X < 32.0 && Middle.Y < 16.0;
		}
	}
	TestTrue(TEXT("the lid's top is the top of the lid's picture"), bLidTop);
	FAnim Open;
	Open.Lid = 1.0f;
	BuildMesh(*Box, Open, 0, Data);
	const ::FBox Opened(Data.Positions);
	TestTrue(TEXT("open, the lid is half a block up"), FMath::IsNearlyEqual(Opened.Max.Z, 100.0, 0.2));
	TestTrue(TEXT("and three quarters turned, still square on"), FMath::IsNearlyEqual(Opened.Max.X, 50.0, 0.2) && FMath::IsNearlyEqual(Opened.Max.Y, 50.0, 0.2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBellTest, "McClient.McRender.Bells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcBellTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	const auto Side = [&](const TCHAR* Text, const FVector& Eye) { return McBell::GetStruckSide(Registry.ParseState(Text).Get(0), FIntVector(0, 0, 0), Eye); };
	// A floor bell facing north hangs between posts east and west: struck from the north or south only.
	const TCHAR* Floor = TEXT("minecraft:bell[attachment=floor,facing=north,powered=false]");
	TestEqual(TEXT("floor, from the north"), Side(Floor, FVector(0.5, 1.6, -2.0)), 0);
	TestEqual(TEXT("floor, from the south"), Side(Floor, FVector(0.7, 1.6, 3.0)), 1);
	TestEqual(TEXT("floor, not from the east"), Side(Floor, FVector(3.0, 1.6, 0.5)), INDEX_NONE);
	// A wall bell facing north hangs from a bar running north: struck from the west or east.
	const TCHAR* Wall = TEXT("minecraft:bell[attachment=single_wall,facing=north,powered=false]");
	TestEqual(TEXT("wall, from the west"), Side(Wall, FVector(-2.0, 1.6, 0.5)), 2);
	TestEqual(TEXT("wall, from the east"), Side(Wall, FVector(3.0, 1.6, 0.4)), 3);
	TestEqual(TEXT("wall, not from the north"), Side(Wall, FVector(0.5, 1.6, -2.0)), INDEX_NONE);
	TestEqual(TEXT("a ceiling bell from anywhere"), Side(TEXT("minecraft:bell[attachment=ceiling,facing=east,powered=false]"), FVector(0.5, 1.6, -2.0)), 0);
	TestEqual(TEXT("stone is no bell"), Side(TEXT("minecraft:stone"), FVector(0.5, 1.6, -2.0)), INDEX_NONE);

	TestEqual(TEXT("at rest when struck"), McBell::GetSwing(0.0f), 0.0f);
	TestTrue(TEXT("then away from the blow"), McBell::GetSwing(4.0f) > 0.3f);
	TestTrue(TEXT("back past the middle"), McBell::GetSwing(12.0f) < 0.0f);
	TestTrue(TEXT("dying away"), FMath::Abs(McBell::GetSwing(36.0f)) < McBell::GetSwing(4.0f) * 0.3f);
	TestEqual(TEXT("still once done"), McBell::GetSwing(static_cast<float>(McBell::RingTicks)), 0.0f);

	const FModel* Bell = Find(TEXT("mc:bell"));
	if (!TestNotNull(TEXT("a bell has a model"), Bell))
	{
		return false;
	}
	FAnim Still;
	McPlayerModel::FMeshData Data;
	BuildMesh(*Bell, Still, 0, Data);
	const ::FBox Bounds(Data.Positions);
	// The rim 8 across at 4 to 6 pixels up, the cup up to 13.
	TestTrue(TEXT("8 across"), FMath::IsNearlyEqual(Bounds.Min.X, -25.0, 0.1) && FMath::IsNearlyEqual(Bounds.Max.X, 25.0, 0.1));
	TestTrue(TEXT("4 to 13 up"), FMath::IsNearlyEqual(Bounds.Min.Z, 25.0, 0.1) && FMath::IsNearlyEqual(Bounds.Max.Z, 81.25, 0.1));
	// Struck on the north, its rim swings south (Unreal +y) about the top.
	FAnim Swung;
	Swung.SwingPitch = -McBell::GetSwing(4.0f);
	BuildMesh(*Bell, Swung, 0, Data);
	const ::FBox Away(Data.Positions);
	TestTrue(TEXT("the rim swings south"), Away.Max.Y > 35.0 && Away.Min.Y > -25.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBookTest, "McClient.McRender.Books",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcBookTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	TestTrue(TEXT("unturned axes are no turn"), UMcChestComponent::RotationFromAxes(FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1)).Equals(FQuat::Identity, 1e-6));

	FVector Centre;
	FQuat Rotation;
	TestFalse(TEXT("a lectern without a book shows none"), UMcChestComponent::GetLecternBook(Registry.ParseState(TEXT("minecraft:lectern[facing=north,has_book=false,powered=false]")).Get(0), Centre, Rotation));
	TestFalse(TEXT("nor does stone"), UMcChestComponent::GetLecternBook(Registry.ParseState(TEXT("minecraft:stone")).Get(0), Centre, Rotation));
	const double Sin = FMath::Sin(FMath::DegreesToRadians(22.5));
	const double Cos = FMath::Cos(FMath::DegreesToRadians(22.5));
	if (TestTrue(TEXT("a lectern with a book"), UMcChestComponent::GetLecternBook(Registry.ParseState(TEXT("minecraft:lectern[facing=north,has_book=true,powered=false]")).Get(0), Centre, Rotation)))
	{
		// On the top's middle, a block up and north of the middle, its pages (model +z, Unreal local +y) facing up the
		// way the top is tipped: towards the reader at the north.
		TestTrue(TEXT("on the top"), FMath::IsNearlyEqual(Centre.X, 0.5, 1e-3) && FMath::IsNearlyEqual(Centre.Y, 1.0, 0.01) && Centre.Z > 0.35 && Centre.Z < 0.45);
		TestTrue(TEXT("its pages face up and north"), Rotation.RotateVector(FVector(0, 1, 0)).Equals(FVector(0, -Sin, Cos), 1e-4));
		TestTrue(TEXT("its right half on the reader's right (west)"), Rotation.RotateVector(FVector(1, 0, 0)).Equals(FVector(-1, 0, 0), 1e-4));
	}
	if (TestTrue(TEXT("one facing east"), UMcChestComponent::GetLecternBook(Registry.ParseState(TEXT("minecraft:lectern[facing=east,has_book=true,powered=false]")).Get(0), Centre, Rotation)))
	{
		TestTrue(TEXT("is east of the middle"), Centre.X > 0.55 && FMath::IsNearlyEqual(Centre.Z, 0.5, 1e-3));
		TestTrue(TEXT("its pages face up and east"), Rotation.RotateVector(FVector(0, 1, 0)).Equals(FVector(Sin, 0, Cos), 1e-4));
	}

	const FModel* Book = Find(TEXT("mc:book"));
	if (!TestNotNull(TEXT("a book has a model"), Book))
	{
		return false;
	}
	FAnim Pose;
	McPlayerModel::FMeshData Data;
	BuildMesh(*Book, Pose, 0, Data);
	const ::FBox Shut(Data.Positions);
	// Shut: the covers stand from the spine's edges (x -1 and 1) out to 6 in front (model +z, Unreal +y), 10 high.
	TestTrue(TEXT("shut, 2 thick"), FMath::IsNearlyEqual(Shut.Min.X, -6.25, 0.1) && FMath::IsNearlyEqual(Shut.Max.X, 6.25, 0.1));
	TestTrue(TEXT("and 6 deep"), FMath::IsNearlyEqual(Shut.Min.Y, 0.0, 0.1) && FMath::IsNearlyEqual(Shut.Max.Y, 37.5, 0.1));
	TestTrue(TEXT("and 10 high"), FMath::IsNearlyEqual(Shut.Min.Z, -31.25, 0.1) && FMath::IsNearlyEqual(Shut.Max.Z, 31.25, 0.1));
	Pose.BookOpen = 1.0f;
	BuildMesh(*Book, Pose, 0, Data);
	const ::FBox Open(Data.Positions);
	TestTrue(TEXT("open flat, 14 across"), FMath::IsNearlyEqual(Open.Min.X, -43.75, 0.1) && FMath::IsNearlyEqual(Open.Max.X, 43.75, 0.1));
	TestTrue(TEXT("and only its pages deep"), Open.Max.Y < 8.0);
	Pose.BookFlip[0] = 0.5f;
	BuildMesh(*Book, Pose, 0, Data);
	TestTrue(TEXT("a page half turned stands up off the pages"), ::FBox(Data.Positions).Max.Y > 35.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHeadTest, "McClient.McRender.Heads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcHeadTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	FVector Centre;
	float Yaw = -1.0f;
	const auto Pick = [&](const TCHAR* Text) { return UMcChestComponent::GetHeadModel(Registry.ParseState(Text).Get(0), Centre, Yaw); };
	TestEqual(TEXT("a skeleton skull"), Pick(TEXT("minecraft:skeleton_skull[powered=false,rotation=4]")), FName(TEXT("mc:head/skeleton")));
	TestTrue(TEXT("stands on the floor's middle"), Centre.Equals(FVector(0.5, 0.0, 0.5)));
	// Rotation 0 faces north and 4 east, the same yaw as a wall head facing east (ref_scene_heads).
	TestEqual(TEXT("rotation 4 faces east"), Yaw, 270.0f);
	TestEqual(TEXT("a wither skeleton's on a wall"), Pick(TEXT("minecraft:wither_skeleton_wall_skull[facing=east,powered=false]")), FName(TEXT("mc:head/wither_skeleton")));
	// Facing east out of a wall to its west: set back against it, half way up.
	TestTrue(TEXT("against the wall"), Centre.Equals(FVector(0.25, 0.25, 0.5)));
	TestEqual(TEXT("facing east"), Yaw, 270.0f);
	TestEqual(TEXT("a player head"), Pick(TEXT("minecraft:player_head[powered=false,rotation=0]")), FName(TEXT("mc:head/player")));
	TestEqual(TEXT("a piglin's on a wall"), Pick(TEXT("minecraft:piglin_wall_head[facing=north,powered=false]")), FName(TEXT("mc:head/piglin")));
	TestEqual(TEXT("a dragon head"), Pick(TEXT("minecraft:dragon_head[powered=false,rotation=0]")), FName(TEXT("mc:head/dragon")));
	if (const FModel* Dragon = Find(TEXT("mc:head/dragon")))
	{
		FAnim Still;
		McPlayerModel::FMeshData Mesh;
		BuildMesh(*Dragon, Still, 0, Mesh);
		const ::FBox Big(Mesh.Positions);
		// 12 across at three quarters, its jaws reaching out south past the block.
		TestTrue(TEXT("the dragon's is 12 across"), FMath::IsNearlyEqual(Big.Max.X - Big.Min.X, 75.0, 0.1));
		TestTrue(TEXT("its jaws reach south"), FMath::IsNearlyEqual(Big.Max.Y, 112.5, 0.1));
		// The lower jaw hangs 0.2 radians open from its back top edge (3 up, 6 out): its front bottom dips under the floor.
		TestTrue(TEXT("its jaw hangs open"), FMath::IsNearlyEqual(Big.Min.Z, (3.0 - 3.0 * FMath::Cos(0.2) - 12.0 * FMath::Sin(0.2)) * 6.25, 0.1));
	}
	TestTrue(TEXT("a piston head is no head"), Pick(TEXT("minecraft:piston_head[facing=north,short=false,type=normal]")).IsNone());
	for (const TCHAR* Kind : { TEXT("mc:head/skeleton"), TEXT("mc:head/wither_skeleton"), TEXT("mc:head/zombie"), TEXT("mc:head/creeper"), TEXT("mc:head/player"), TEXT("mc:head/piglin") })
	{
		TestNotNull(FString::Printf(TEXT("%s has a model"), Kind), Find(FName(Kind)));
	}
	const FModel* Skull = Find(TEXT("mc:head/creeper"));
	if (!Skull)
	{
		return false;
	}
	FAnim Pose;
	McPlayerModel::FMeshData Data;
	BuildMesh(*Skull, Pose, 0, Data);
	const ::FBox Bounds(Data.Positions);
	TestTrue(TEXT("a half block cube on the floor"), Bounds.Min.Equals(FVector(-25.0, -25.0, 0.0), 0.1) && Bounds.Max.Equals(FVector(25.0, 25.0, 50.0), 0.1));
	// Its face (the picture's 8..16 across, 8..16 down) is on the south side, Unreal +y.
	bool bFaceSouth = false;
	for (int32 Index = 0; Index + 3 < Data.Positions.Num(); Index += 4)
	{
		const FVector2D Middle = (Data.UV0[Index] + Data.UV0[Index + 2]) * 0.5 * FVector2D(64.0, 32.0);
		bFaceSouth |= Data.Normals[Index].Y > 0.9 && Middle.X > 8.0 && Middle.X < 16.0 && Middle.Y > 8.0 && Middle.Y < 16.0;
	}
	TestTrue(TEXT("its face looks south"), bFaceSouth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPotConduitTest, "McClient.McRender.PotsAndConduits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcPotConduitTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	const FModel* Pot = Find(TEXT("mc:decorated_pot"));
	if (TestNotNull(TEXT("a pot has a model"), Pot))
	{
		FAnim Still;
		McPlayerModel::FMeshData Body, Sides;
		BuildMesh(*Pot, Still, 0, Body);
		BuildMesh(*Pot, Still, 1, Sides);
		const ::FBox Top(Body.Positions);
		// The neck stands 4 above the 16 high body, 8 across; the body is 14 across.
		TestTrue(TEXT("the body and neck reach 20 up"), FMath::IsNearlyEqual(Top.Max.Z, 125.0, 0.1) && FMath::IsNearlyEqual(Top.Min.Z, 0.0, 0.1));
		const ::FBox Round(Sides.Positions);
		TestTrue(TEXT("its sides 14 across all round"), Round.Min.Equals(FVector(-43.75, -43.75, 0.0), 0.1) && Round.Max.Equals(FVector(43.75, 43.75, 100.0), 0.1));
	}
	TestNotNull(TEXT("a conduit's shell"), Find(TEXT("mc:conduit/shell")));
	TestNotNull(TEXT("and its cage"), Find(TEXT("mc:conduit/cage")));

	TestEqual(TEXT("42 frame places"), McConduit::GetFramePlaces().Num(), 42);
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	const FIntVector At(8, 10, 8);
	const FMcStateId Water = Registry.ParseState(TEXT("minecraft:water[level=0]")).Get(0);
	for (int32 X = -1; X <= 1; ++X)
	{
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			for (int32 Z = -1; Z <= 1; ++Z)
			{
				World.SetBlock(At + FIntVector(X, Y, Z), Water);
			}
		}
	}
	World.SetBlock(At, Registry.ParseState(TEXT("minecraft:conduit[waterlogged=true]")).Get(0));
	TestFalse(TEXT("in water with no frame it sleeps"), McConduit::IsActive(World, At));
	const FMcStateId Prismarine = Registry.ParseState(TEXT("minecraft:prismarine")).Get(0);
	const TArray<FIntVector>& Places = McConduit::GetFramePlaces();
	for (int32 Index = 0; Index < McConduit::MinFrame - 1; ++Index)
	{
		World.SetBlock(At + Places[Index], Prismarine);
	}
	TestFalse(TEXT("15 frame blocks aren't enough"), McConduit::IsActive(World, At));
	World.SetBlock(At + Places[McConduit::MinFrame - 1], Registry.ParseState(TEXT("minecraft:sea_lantern")).Get(0));
	TestEqual(TEXT("a sea lantern counts"), McConduit::CountFrame(World, At), 16);
	TestTrue(TEXT("16 wake it"), McConduit::IsActive(World, At));
	World.SetBlock(At + FIntVector(1, 1, 1), Registry.ParseState(TEXT("minecraft:air")).Get(0));
	TestFalse(TEXT("out of water it sleeps"), McConduit::IsActive(World, At));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHullMaskTest, "McClient.McRender.HullMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcHullMaskTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	const FModel* Boat = Find(TEXT("minecraft:boat"));
	const FModel* Raft = Find(TEXT("minecraft:bamboo_raft"));
	if (!TestNotNull(TEXT("a boat"), Boat) || !TestNotNull(TEXT("a raft"), Raft))
	{
		return false;
	}
	TestEqual(TEXT("the boat's mask comes after its picture"), Boat->MaskLayer, Boat->Textures.Num());
	TestEqual(TEXT("a raft has no hull to keep water out of"), Raft->MaskLayer, INDEX_NONE);
	FAnim Still;
	McPlayerModel::FMeshData Data;
	BuildMesh(*Boat, Still, Boat->MaskLayer, Data);
	const ::FBox Mask(Data.Positions);
	// Inside the sides (16 across, 28 long) from the floor up to their tops, 9 pixels up.
	TestTrue(TEXT("between the sides"), FMath::IsNearlyEqual(Mask.Max.X - Mask.Min.X, 100.0, 0.1) && FMath::IsNearlyEqual(Mask.Max.Y - Mask.Min.Y, 175.0, 0.1));
	TestTrue(TEXT("up to the gunwales"), FMath::IsNearlyEqual(Mask.Max.Z, 56.25, 0.1));
	return true;
}

#endif
