#include "McHand.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHandPoseTest, "McClient.McRender.HandPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcHandPoseTest::RunTest(const FString& Parameters)
{
	using namespace McHand;
	const FVector2D NoBob = FVector2D::ZeroVector;

	// At rest the fingertips are in front of the camera, to the right and below the middle, but past the near plane.
	const FTransform Rest = GetArmTransform(0.0f, 0.0f, NoBob);
	const FVector Tip = Rest.TransformPosition(GetTipInArm());
	TestTrue(TEXT("the hand is ahead of the camera"), Tip.X > 10.0);
	TestTrue(TEXT("the hand is on the right"), Tip.Y > 5.0);
	TestTrue(TEXT("the hand is below the middle"), Tip.Z < -5.0);
	// The whole arm, shoulder end included, stays past the 10 unit near plane or the picture would be cut.
	TestTrue(TEXT("the shoulder end is not behind the camera"), Rest.GetLocation().X > -20.0);
	TestTrue(TEXT("the arm is scaled down"), FMath::IsNearlyEqual(Rest.GetScale3D().X, Scale, 1e-6));

	// The fingertips are down the arm's -z axis, so the arm points the way its rotation turns -z.
	const FVector Pointing = Rest.GetRotation().RotateVector(FVector(0, 0, -1));
	TestTrue(TEXT("the arm points forward"), Pointing.X > 0.4);
	TestTrue(TEXT("the arm points up from the hand"), Pointing.Z > 0.2);

	// The stroke is 0 at the ends of a swing and 1 a quarter of the way through.
	TestTrue(TEXT("no stroke at the start"), FMath::IsNearlyZero(GetSwingStroke(0.0f)));
	TestTrue(TEXT("no stroke at the end"), FMath::IsNearlyZero(GetSwingStroke(1.0f), 1e-4f));
	TestTrue(TEXT("full stroke a quarter in"), FMath::IsNearlyEqual(GetSwingStroke(0.25f), 1.0f, 1e-4f));

	// A thrown item is in place on the click, out of sight 1 to 3 ticks later and back in place by tick 6 (the reference client's shots).
	TestTrue(TEXT("throw dip starts at nothing"), FMath::IsNearlyZero(GetThrowDip(0.0f)));
	TestTrue(TEXT("throw dip is mostly down by tick 1"), GetThrowDip(0.05f) > 0.8f);
	TestTrue(TEXT("throw dip is full at tick 3"), GetThrowDip(0.15f) > 0.99f);
	TestTrue(TEXT("throw dip is over by tick 6"), FMath::IsNearlyZero(GetThrowDip(0.3f), 1e-4f));

	// Eating: nothing moves with the item down, it is raised and turned with it up, and it bobs only until four fifths of the meal.
	TestTrue(TEXT("a lowered food is not moved"), GetEatTransform(0.0f, 0.5f, 1.6f).GetTranslation().IsNearlyZero());
	TestTrue(TEXT("a raised food moves toward the middle and up"), GetEatTransform(1.0f, 0.4f, 1.6f).GetTranslation().Y < 0.0 && GetEatTransform(1.0f, 0.4f, 1.6f).GetTranslation().Z > 0.0);
	TestTrue(TEXT("it bobs while chewed"), GetEatTransform(1.0f, 0.0f, 1.6f).GetTranslation().Z > GetEatTransform(1.0f, 0.1f, 1.6f).GetTranslation().Z);
	TestTrue(TEXT("and holds still at the end"), FMath::IsNearlyEqual(GetEatTransform(1.0f, 1.4f, 1.6f).GetTranslation().Z, GetEatTransform(1.0f, 1.5f, 1.6f).GetTranslation().Z));

	// A swing punches the hand forward, across to the left and down.
	const FVector Struck = GetArmTransform(1.0f, 0.0f, NoBob).TransformPosition(GetTipInArm());
	TestTrue(TEXT("a swing goes forward"), Struck.X > Tip.X);
	TestTrue(TEXT("a swing goes left"), Struck.Y < Tip.Y);
	TestTrue(TEXT("a swing goes down"), Struck.Z < Tip.Z);

	// Changing what is held lowers the hand out of view.
	const FVector Lowered = GetArmTransform(0.0f, 1.0f, NoBob).TransformPosition(GetTipInArm());
	TestTrue(TEXT("an equip drops the hand"), Lowered.Z < Tip.Z - 10.0);

	// Walking bobs it, and standing still does not.
	TestTrue(TEXT("no bob standing"), GetBob(0.37f, 0.0f).IsNearlyZero());
	TestTrue(TEXT("a bob walking"), !GetBob(0.37f, 1.0f).IsNearlyZero());
	TestTrue(TEXT("the bob never lifts the hand"), GetBob(0.0f, 1.0f).Y <= 0.0 && GetBob(0.5f, 1.0f).Y <= 1e-6);

	// The arm has twelve faces: two boxes.
	McPlayerModel::FMeshData Arm;
	BuildArmMesh(Arm);
	TestEqual(TEXT("the arm and sleeve are 12 faces, each seen from both sides"), Arm.Triangles.Num(), 12 * 6 * 2);
	// Its fingertips are at GetTipInArm: the lowest corner of the box.
	TestTrue(TEXT("the fingertip end is at the tip"), FMath::IsNearlyEqual(FBox(Arm.Positions).Min.Z, GetTipInArm().Z - 0.25 * McPlayerModel::ModelScale * 100.0 / 16.0, 0.01));

	// A flat item has a picture on each of its layers' two sides; a cube six faces split over sides and top.
	McPlayerModel::FMeshData Flat, Sides, Top;
	BuildFlatMesh(1.0f, Flat);
	TestEqual(TEXT("a flat item is three layers, two sides each"), Flat.Triangles.Num(), 3 * 2 * 6);
	// A picture of one opaque pixel stands as a one-pixel-thick slab: front, back and four edges.
	{
		TArray<FColor> Pixels;
		Pixels.Init(FColor(0, 0, 0, 0), 16);
		Pixels[5] = FColor::White;
		McPlayerModel::FMeshData Slab;
		BuildExtrudedMesh(Pixels, 4, 1.0f, Slab);
		TestEqual(TEXT("one pixel is six faces, each seen from both sides"), Slab.Triangles.Num(), 6 * 2 * 6);
		Pixels[6] = FColor::White;
		BuildExtrudedMesh(Pixels, 4, 1.0f, Slab);
		TestEqual(TEXT("two neighbours hide the edges between them"), Slab.Triangles.Num(), (2 + 6) * 2 * 6);
		const FBox Bounds(Slab.Positions);
		TestTrue(TEXT("a pixel thick"), FMath::IsNearlyEqual(Bounds.Max.X - Bounds.Min.X, 100.0 / 16.0, 0.01));
	}
	// The hand display comes from the model in the cache, parents followed: a rod has its own, a stick the handheld one.
	{
		const FHeldDisplay Rod = ReadHeldDisplay(TEXT("minecraft:fishing_rod"));
		TestTrue(TEXT("a rod's own hand rotation and translation"), Rod.Rotation.Equals(FVector(0, 90, 25)) && Rod.Translation.Equals(FVector(0, 1.6, 0.8)));
		const FHeldDisplay Stick = ReadHeldDisplay(TEXT("minecraft:stick"));
		TestTrue(TEXT("a stick has the handheld display"), Stick.Rotation.Equals(FVector(0, -90, 25)) && FMath::IsNearlyEqual(Stick.Scale, 0.68));
		const FHeldPictures Idle = FindHeldPictures(TEXT("minecraft:fishing_rod"), false);
		const FHeldPictures Cast = FindHeldPictures(TEXT("minecraft:fishing_rod"), true);
		TestTrue(TEXT("a cast rod shows the cast picture"), Idle.Side.EndsWith(TEXT("fishing_rod.png")) && Cast.Side.EndsWith(TEXT("fishing_rod_cast.png")));
	}
	// A drawn bow's arrow (the picture's up-left diagonal) points forward and its limbs (the other diagonal) up.
	{
		const double R = FMath::Sqrt(0.5);
		const FVector Arrow = GetBowStandRotation().RotateVector(FVector(0, -R, R));
		const FVector Limbs = GetBowStandRotation().RotateVector(FVector(0, R, R));
		TestTrue(TEXT("the arrow points forward"), Arrow.Equals(FVector(1, 0, 0), 0.001));
		TestTrue(TEXT("the limbs stand up"), Limbs.Equals(FVector(0, 0, 1), 0.001));
	}
	BuildCubeSidesMesh(FColor::White, 1.0f, Sides);
	BuildCubeTopMesh(FColor::White, 1.0f, Top);
	TestEqual(TEXT("a cube's four sides"), Sides.Triangles.Num(), 4 * 2 * 6);
	TestEqual(TEXT("a cube's top"), Top.Triangles.Num(), 1 * 2 * 6);

	// The held thing sits at the hand: past the camera, near the fingertips.
	for (const EHeld Kind : { EHeld::Flat, EHeld::Cube })
	{
		const FVector Place = GetHeldRestTransform(Kind).GetLocation();
		TestTrue(TEXT("held thing in front of the camera"), Place.X > 10.0);
		TestTrue(TEXT("held thing near the fingertips"), FVector::Dist(Place, Tip) < 40.0);
	}

	// The held block's top face, on a 1280x720 screen at 70 degrees (vertical): its left, far and right corners are where
	// the reference client's held stone has them (measured from its shot, within a few pixels); the near corner is off the screen.
	{
		const FTransform Block = GetHeldRestTransform(EHeld::Cube);
		const double TanHalf = FMath::Tan(FMath::DegreesToRadians(35.0));
		TArray<FVector2D> Corners;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector P = Block.TransformPosition(FVector(Corner & 1 ? 50.0 : -50.0, Corner & 2 ? 50.0 : -50.0, 50.0));
			Corners.Add(FVector2D(640.0 + P.Y / P.X / (TanHalf * 16.0 / 9.0) * 640.0, 360.0 - P.Z / P.X / TanHalf * 360.0));
		}
		// The near corner is off the screen; each of the three others has one of ours within a few pixels.
		auto NearestTo = [&Corners](const FVector2D& Wanted)
		{
			double Best = TNumericLimits<double>::Max();
			for (const FVector2D& C : Corners)
			{
				Best = FMath::Min(Best, FVector2D::Distance(C, Wanted));
			}
			return Best;
		};
		TestTrue(TEXT("a held block's left corner"), NearestTo(FVector2D(840, 592)) < 8.0);
		TestTrue(TEXT("a held block's far corner"), NearestTo(FVector2D(930, 525)) < 8.0);
		TestTrue(TEXT("a held block's right corner"), NearestTo(FVector2D(1245, 590)) < 8.0);
	}
	return true;
}

#endif
