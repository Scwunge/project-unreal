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

	// A held block is about 200 pixels across on a 1280x720 screen at 70 degrees (vertical): not 400.
	{
		const FTransform Block = GetHeldRestTransform(EHeld::Cube);
		const double TanHalf = FMath::Tan(FMath::DegreesToRadians(35.0));
		FBox2D Screen(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local(Corner & 1 ? 50.0 : -50.0, Corner & 2 ? 50.0 : -50.0, Corner & 4 ? 50.0 : -50.0);
			const FVector P = Block.TransformPosition(Local);
			Screen += FVector2D(640.0 + P.Y / P.X / (TanHalf * 16.0 / 9.0) * 640.0, 360.0 - P.Z / P.X / TanHalf * 360.0);
		}
		TestTrue(TEXT("a held block is 150 to 250 pixels wide"), Screen.GetSize().X > 150.0 && Screen.GetSize().X < 250.0);
		TestTrue(TEXT("a held block sits in the right of the screen"), Screen.GetCenter().X > 800.0 && Screen.GetCenter().X < 1000.0);
		TestTrue(TEXT("a held block sits in the lower part of the screen"), Screen.GetCenter().Y > 480.0 && Screen.GetCenter().Y < 640.0);
	}
	return true;
}

#endif
