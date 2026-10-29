#include "McPlayerModel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace McPlayerModel;

namespace
{
	FIntRect MakeRect(int32 X0, int32 Y0, int32 X1, int32 Y1)
	{
		return FIntRect(X0, Y0, X1, Y1);
	}

	FString Describe(const FIntRect& Rect)
	{
		return FString::Printf(TEXT("(%d,%d)-(%d,%d)"), Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y);
	}

	// The skin's cross for an 8x8x8 head, the picture everyone knows: top and bottom above, then right side,
	// front, left side and back along the row below.
	void CheckRect(FAutomationTestBase& Test, const TCHAR* What, const FIntRect& Actual, const FIntRect& Expected)
	{
		Test.TestTrue(FString::Printf(TEXT("%s is %s, not %s"), What, *Describe(Actual), *Describe(Expected)), Actual == Expected);
	}

	FBox Bounds(const FMeshData& Data)
	{
		return FBox(Data.Positions);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPlayerModelUVTest, "McClient.McRender.PlayerModelUV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcPlayerModelUVTest::RunTest(const FString& Parameters)
{
	// The head's net, from the classic 64x64 skin.
	const FPartDef& Head = GetPartDef(EMcPlayerPart::Head);
	CheckRect(*this, TEXT("head top"), GetFaceRect(Head.BaseUV, Head.Size, EBoxFace::Up), MakeRect(8, 0, 16, 8));
	CheckRect(*this, TEXT("head bottom"), GetFaceRect(Head.BaseUV, Head.Size, EBoxFace::Down), MakeRect(16, 0, 24, 8));
	CheckRect(*this, TEXT("head right side"), GetFaceRect(Head.BaseUV, Head.Size, EBoxFace::West), MakeRect(0, 8, 8, 16));
	CheckRect(*this, TEXT("head front"), GetFaceRect(Head.BaseUV, Head.Size, EBoxFace::South), MakeRect(8, 8, 16, 16));
	CheckRect(*this, TEXT("head left side"), GetFaceRect(Head.BaseUV, Head.Size, EBoxFace::East), MakeRect(16, 8, 24, 16));
	CheckRect(*this, TEXT("head back"), GetFaceRect(Head.BaseUV, Head.Size, EBoxFace::North), MakeRect(24, 8, 32, 16));
	CheckRect(*this, TEXT("hat front"), GetFaceRect(Head.OuterUV, Head.Size, EBoxFace::South), MakeRect(40, 8, 48, 16));

	// The body is 8 wide, 12 tall and 4 deep, starting at (16, 16).
	const FPartDef& Body = GetPartDef(EMcPlayerPart::Body);
	CheckRect(*this, TEXT("body top"), GetFaceRect(Body.BaseUV, Body.Size, EBoxFace::Up), MakeRect(20, 16, 28, 20));
	CheckRect(*this, TEXT("body front"), GetFaceRect(Body.BaseUV, Body.Size, EBoxFace::South), MakeRect(20, 20, 28, 32));
	CheckRect(*this, TEXT("body right side"), GetFaceRect(Body.BaseUV, Body.Size, EBoxFace::West), MakeRect(16, 20, 20, 32));
	CheckRect(*this, TEXT("body back"), GetFaceRect(Body.BaseUV, Body.Size, EBoxFace::North), MakeRect(32, 20, 40, 32));
	CheckRect(*this, TEXT("jacket front"), GetFaceRect(Body.OuterUV, Body.Size, EBoxFace::South), MakeRect(20, 36, 28, 48));

	// Each limb's net, base and outer layer, where the skin keeps them.
	struct { EMcPlayerPart Part; FIntPoint Base; FIntPoint Outer; } Limbs[] = {
		{ EMcPlayerPart::RightArm, FIntPoint(40, 16), FIntPoint(40, 32) },
		{ EMcPlayerPart::LeftArm, FIntPoint(32, 48), FIntPoint(48, 48) },
		{ EMcPlayerPart::RightLeg, FIntPoint(0, 16), FIntPoint(0, 32) },
		{ EMcPlayerPart::LeftLeg, FIntPoint(16, 48), FIntPoint(0, 48) },
	};
	for (const auto& Limb : Limbs)
	{
		const FPartDef& Def = GetPartDef(Limb.Part);
		TestEqual(TEXT("limb base net"), Def.BaseUV, Limb.Base);
		TestEqual(TEXT("limb outer net"), Def.OuterUV, Limb.Outer);
		TestEqual(TEXT("limb size"), Def.Size, FIntVector(4, 12, 4));
		// Right arm front, e.g. (44, 20)-(48, 32).
		CheckRect(*this, TEXT("limb front"), GetFaceRect(Def.BaseUV, Def.Size, EBoxFace::South), MakeRect(Def.BaseUV.X + 4, Def.BaseUV.Y + 4, Def.BaseUV.X + 8, Def.BaseUV.Y + 16));
	}

	// Everything stays on the skin, and the nets of the six parts and their outer layers don't overlap.
	TArray<FIntRect> Nets;
	for (int32 Index = 0; Index < McNumPlayerParts; ++Index)
	{
		const FPartDef& Def = GetPartDef(static_cast<EMcPlayerPart>(Index));
		for (const FIntPoint& Origin : { Def.BaseUV, Def.OuterUV })
		{
			const FIntRect Net(Origin.X, Origin.Y, Origin.X + Def.Size.Z * 2 + Def.Size.X * 2, Origin.Y + Def.Size.Z + Def.Size.Y);
			TestTrue(TEXT("net on the skin"), Net.Min.X >= 0 && Net.Min.Y >= 0 && Net.Max.X <= SkinSize && Net.Max.Y <= SkinSize);
			for (const FIntRect& Other : Nets)
			{
				TestFalse(TEXT("nets overlap"), Net.Min.X < Other.Max.X && Other.Min.X < Net.Max.X && Net.Min.Y < Other.Max.Y && Other.Min.Y < Net.Max.Y);
			}
			Nets.Add(Net);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPlayerModelShapeTest, "McClient.McRender.PlayerModelShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcPlayerModelShapeTest::RunTest(const FString& Parameters)
{
	FMeshData Standing;
	BuildMesh(ComputePose(FAnim()), Standing);

	// Twelve boxes: six parts, each with its outer layer, and six faces of two triangles on each.
	TestEqual(TEXT("vertices"), Standing.Positions.Num(), 12 * 6 * 4);
	TestEqual(TEXT("triangle indices"), Standing.Triangles.Num(), 12 * 6 * 6);
	TestEqual(TEXT("normals"), Standing.Normals.Num(), Standing.Positions.Num());
	TestEqual(TEXT("uvs"), Standing.UV0.Num(), Standing.Positions.Num());
	for (const FVector2D& UV : Standing.UV0)
	{
		TestTrue(TEXT("uv on the skin"), UV.X >= 0.0 && UV.X <= 1.0 && UV.Y >= 0.0 && UV.Y <= 1.0);
	}

	// 32 pixels tall at 0.9375 of a block's 16: 1.875 blocks to the top of the head, plus the hat's half pixel; the
	// outer layer of the body parts pokes out a quarter pixel at the feet and sides. Unreal units are 100 to the block.
	const double Pixel = ModelScale / 16.0 * 100.0;
	const FBox Box = Bounds(Standing);
	TestTrue(TEXT("top of the hat"), FMath::IsNearlyEqual(Box.Max.Z, 32.5 * Pixel, 1e-3));
	TestTrue(TEXT("sole of the pants"), FMath::IsNearlyEqual(Box.Min.Z, -0.25 * Pixel, 1e-3));
	// Arms 4 wide beside an 8 wide body: 16 pixels across at the shoulders, 8 deep for the head; the sleeves add half a pixel.
	TestTrue(TEXT("width"), FMath::IsNearlyEqual(Box.Max.X - Box.Min.X, 16.5 * Pixel, 1e-3));
	TestTrue(TEXT("depth"), FMath::IsNearlyEqual(Box.Max.Y - Box.Min.Y, 9.0 * Pixel, 1e-3));

	// Each part's outer layer (the second 24 vertices of its 48) is its own box grown by the same margin on every side,
	// so it wraps the part: a limb's overlay can't be turned, moved or sized differently from the limb.
	for (int32 Part = 0; Part < 6; ++Part)
	{
		FBox BaseBox(ForceInit), OuterBox(ForceInit);
		for (int32 Index = 0; Index < 24; ++Index)
		{
			BaseBox += Standing.Positions[Part * 48 + Index];
			OuterBox += Standing.Positions[Part * 48 + 24 + Index];
		}
		const double Margin = (Part == 0 ? 0.5 : 0.25) * Pixel;
		TestTrue(*FString::Printf(TEXT("the outer layer of part %d wraps it"), Part),
			FMath::IsNearlyEqual(BaseBox.Min.X - OuterBox.Min.X, Margin, 1e-3) && FMath::IsNearlyEqual(OuterBox.Max.X - BaseBox.Max.X, Margin, 1e-3)
			&& FMath::IsNearlyEqual(BaseBox.Min.Y - OuterBox.Min.Y, Margin, 1e-3) && FMath::IsNearlyEqual(OuterBox.Max.Y - BaseBox.Max.Y, Margin, 1e-3)
			&& FMath::IsNearlyEqual(BaseBox.Min.Z - OuterBox.Min.Z, Margin, 1e-3) && FMath::IsNearlyEqual(OuterBox.Max.Z - BaseBox.Max.Z, Margin, 1e-3));
	}

	// The head alone (the first 48 vertices): 8 pixels tall on top of the body.
	FBox HeadBox(ForceInit);
	for (int32 Index = 0; Index < 24; ++Index)
	{
		HeadBox += Standing.Positions[Index];
	}
	TestTrue(TEXT("head is 8 pixels"), FMath::IsNearlyEqual(HeadBox.Max.Z - HeadBox.Min.Z, 8.0 * Pixel, 1e-3));
	TestTrue(TEXT("head sits at 24 pixels"), FMath::IsNearlyEqual(HeadBox.Min.Z, 24.0 * Pixel, 1e-3));

	// The pivots: shoulders at 22 pixels, 5 out from the middle; hips 1.9 out and 12 up.
	TestEqual(TEXT("right arm pivot"), GetPartDef(EMcPlayerPart::RightArm).Pivot, FVector(-5.0, 22.0, 0.0));
	TestEqual(TEXT("left arm pivot"), GetPartDef(EMcPlayerPart::LeftArm).Pivot, FVector(5.0, 22.0, 0.0));
	TestEqual(TEXT("right leg pivot"), GetPartDef(EMcPlayerPart::RightLeg).Pivot, FVector(-1.9, 12.0, 0.0));
	TestEqual(TEXT("left leg pivot"), GetPartDef(EMcPlayerPart::LeftLeg).Pivot, FVector(1.9, 12.0, 0.0));
	TestEqual(TEXT("head pivot"), GetPartDef(EMcPlayerPart::Head).Pivot, FVector(0.0, 24.0, 0.0));

	// The player's front faces south (Minecraft +z, Unreal +Y) when the yaw is 0 and west (-x) at 90.
	TestTrue(TEXT("faces south"), Standing.Normals[0].Equals(FVector(0.0, 1.0, 0.0), 1e-4));
	FAnim West;
	West.BodyYaw = 90.0f;
	West.HeadYaw = 90.0f;
	FMeshData Turned;
	BuildMesh(ComputePose(West), Turned);
	TestTrue(TEXT("faces west"), Turned.Normals[0].Equals(FVector(-1.0, 0.0, 0.0), 1e-4));

	// Looking down tips the head forward; its front goes down.
	FAnim Down;
	Down.HeadPitch = 90.0f;
	FMeshData Looking;
	BuildMesh(ComputePose(Down), Looking);
	TestTrue(TEXT("head front looks down"), Looking.Normals[0].Equals(FVector(0.0, 0.0, -1.0), 1e-4));
	TestTrue(TEXT("body didn't move"), Looking.Positions[24 * 2].Equals(Standing.Positions[24 * 2], 1e-3));

	// Sneaking drops the head and tips the body.
	FAnim Sneak;
	Sneak.bSneaking = true;
	const FPose Crouching = ComputePose(Sneak);
	TestTrue(TEXT("sneaking head is lower"), Crouching.Parts[0].Pivot.Y < 24.0 - 4.0);
	TestTrue(TEXT("sneaking tips the body"), FMath::IsNearlyEqual(Crouching.Parts[static_cast<int32>(EMcPlayerPart::Body)].PitchRad, 0.5f));
	TestTrue(TEXT("sneaking steps the legs back"), Crouching.Parts[static_cast<int32>(EMcPlayerPart::RightLeg)].Pivot.Z < -3.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPlayerModelAnimTest, "McClient.McRender.PlayerModelAnim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcPlayerModelAnimTest::RunTest(const FString& Parameters)
{
	// Standing still, nothing swings.
	TestEqual(TEXT("still leg"), GetLegSwing(3.0f, 0.0f, true), 0.0f);
	TestEqual(TEXT("still arm"), GetArmSwing(3.0f, 0.0f, true), 0.0f);

	// At the start of a stride at full speed, the right leg is 1.4 radians back, the left as far forward, and the
	// arms swing the other way, the right arm forward with the left leg.
	TestTrue(TEXT("right leg"), FMath::IsNearlyEqual(GetLegSwing(0.0f, 1.0f, true), 1.4f, 1e-5f));
	TestTrue(TEXT("left leg"), FMath::IsNearlyEqual(GetLegSwing(0.0f, 1.0f, false), -1.4f, 1e-5f));
	TestTrue(TEXT("right arm"), FMath::IsNearlyEqual(GetArmSwing(0.0f, 1.0f, true), -1.0f, 1e-5f));
	TestTrue(TEXT("left arm"), FMath::IsNearlyEqual(GetArmSwing(0.0f, 1.0f, false), 1.0f, 1e-5f));

	// The swing is proportional to the amount and runs at 0.6662 radians a step.
	TestTrue(TEXT("half speed"), FMath::IsNearlyEqual(GetLegSwing(0.0f, 0.5f, true), 0.7f, 1e-5f));
	TestTrue(TEXT("mid stride"), FMath::IsNearlyEqual(GetLegSwing(3.0f, 1.0f, true), FMath::Cos(3.0f * 0.6662f) * 1.4f, 1e-5f));
	TestTrue(TEXT("legs cross"), FMath::IsNearlyEqual(GetLegSwing(PI / 2.0f / 0.6662f, 1.0f, true), 0.0f, 1e-4f));

	// The pose carries the swing to the limbs.
	FAnim Walking;
	Walking.WalkAmount = 1.0f;
	const FPose Pose = ComputePose(Walking);
	TestTrue(TEXT("pose right leg"), FMath::IsNearlyEqual(Pose.Parts[static_cast<int32>(EMcPlayerPart::RightLeg)].PitchRad, 1.4f, 1e-5f));
	TestTrue(TEXT("pose left arm"), FMath::IsNearlyEqual(Pose.Parts[static_cast<int32>(EMcPlayerPart::LeftArm)].PitchRad, 1.0f, 1e-5f));

	// The head turns relative to the body, the other way round to the Minecraft yaw (which grows clockwise).
	FAnim Turn;
	Turn.BodyYaw = 10.0f;
	Turn.HeadYaw = 40.0f;
	TestTrue(TEXT("head turn"), FMath::IsNearlyEqual(ComputePose(Turn).Parts[0].YawRad, -FMath::DegreesToRadians(30.0f), 1e-5f));
	Turn.BodyYaw = 170.0f;
	Turn.HeadYaw = -170.0f;
	TestTrue(TEXT("head turn across the seam"), FMath::IsNearlyEqual(ComputePose(Turn).Parts[0].YawRad, -FMath::DegreesToRadians(20.0f), 1e-5f));

	// The body stays put while the head turns up to 50 degrees, then follows.
	TestEqual(TEXT("small turn"), TurnBody(0.0f, 30.0f, false), 0.0f);
	TestEqual(TEXT("limit"), TurnBody(0.0f, 50.0f, false), 0.0f);
	TestTrue(TEXT("beyond the limit"), FMath::IsNearlyEqual(TurnBody(0.0f, 80.0f, false), 30.0f, 1e-4f));
	TestTrue(TEXT("beyond the limit the other way"), FMath::IsNearlyEqual(TurnBody(0.0f, -80.0f, false), -30.0f, 1e-4f));
	TestTrue(TEXT("across the seam"), FMath::IsNearlyEqual(TurnBody(170.0f, -150.0f, false), 170.0f, 1e-4f));
	TestTrue(TEXT("moving turns the body"), TurnBody(0.0f, 30.0f, true) > 5.0f);

	// A player running at 0.2 blocks a tick (sprint speed is faster) swings ever further until the speed settles.
	FAnimState State;
	State.Tick(FVector(0.0, 0.0, 0.0), 0.0f, 0.0f, false);
	TestEqual(TEXT("starts still"), State.Current.WalkAmount, 0.0f);
	for (int32 Tick = 1; Tick <= 30; ++Tick)
	{
		State.Tick(FVector(0.0, 0.0, Tick * 0.2), 0.0f, 0.0f, false);
	}
	TestTrue(TEXT("amount settles at speed * 4"), FMath::IsNearlyEqual(State.Current.WalkAmount, 0.8f, 1e-3f));
	TestTrue(TEXT("position advances"), State.Current.WalkPos > 15.0f);
	for (int32 Tick = 0; Tick < 40; ++Tick)
	{
		State.Tick(FVector(0.0, 0.0, 6.0), 0.0f, 0.0f, false);
	}
	TestTrue(TEXT("stops swinging"), State.Current.WalkAmount < 0.001f);

	// A jump across the world isn't a run.
	State.Tick(FVector(500.0, 0.0, 6.0), 0.0f, 0.0f, false);
	TestTrue(TEXT("teleport"), State.Current.WalkAmount < 0.001f);

	// A runner at full tilt tops out at an amount of 1, however fast.
	FAnimState Fast;
	Fast.Tick(FVector::ZeroVector, 0.0f, 0.0f, false);
	for (int32 Tick = 1; Tick <= 40; ++Tick)
	{
		Fast.Tick(FVector(0.0, 0.0, Tick * 0.5), 0.0f, 0.0f, false);
	}
	TestTrue(TEXT("amount tops out"), FMath::IsNearlyEqual(Fast.Current.WalkAmount, 1.0f, 1e-3f));
	return true;
}

#endif
