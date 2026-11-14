#include "McEntityModel.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "McRenderMode.h"
#include "McCache.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "McDecor.h"
#include "McItemFrames.h"
#include "McChunk.h"
#include "McRender.h"
#include "McRenderTextures.h"
#include "McHand.h"
#include "McSkyComponent.h"
#include "Engine/Texture2DArray.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Kismet/GameplayStatics.h"
#include "ConvexVolume.h"
#include "McPerf.h"

static TAutoConsoleVariable<bool> CVarMcModelsSkipUnseen(TEXT("mc.Models.SkipUnseen"), true,
	TEXT("Mobs the camera has not drawn in the last quarter second keep their last mesh instead of building it again each frame."));

namespace
{
	// Whether a mob that is off screen may still have its shadow on screen: its bounds, stretched away from the light as
	// far as a shadow of its height reaches (32 blocks at most), against the camera's frustum (widened 10 degrees, made
	// once a frame). The engine counts only the main view as drawing a mesh, not the shadow passes, so without this a
	// mob walking just off screen would leave a still shadow in view.
	bool ShadowMayBeSeen(const UWorld* World, const FBoxSphereBounds& Bounds, const FVector& Towards)
	{
		static uint64 Frame = MAX_uint64;
		static const UWorld* FrameWorld = nullptr;
		static FConvexVolume Frustum;
		static bool bHaveFrustum = false;
		if (Frame != GFrameCounter || FrameWorld != World)
		{
			Frame = GFrameCounter;
			FrameWorld = World;
			bHaveFrustum = false;
			const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			if (Controller && Controller->PlayerCameraManager)
			{
				FMinimalViewInfo View = Controller->PlayerCameraManager->GetCameraCacheView();
				int32 SizeX = 0, SizeY = 0;
				Controller->GetViewportSize(SizeX, SizeY);
				if (SizeX > 0 && SizeY > 0)
				{
					View.AspectRatio = static_cast<float>(SizeX) / SizeY;
					View.bConstrainAspectRatio = true;
				}
				View.FOV = FMath::Min(View.FOV + 10.0f, 170.0f);
				FMatrix ViewMatrix, Projection, ViewProjection;
				UGameplayStatics::GetViewProjectionMatrix(View, ViewMatrix, Projection, ViewProjection);
				GetViewFrustumBounds(Frustum, ViewProjection, false);
				bHaveFrustum = true;
			}
		}
		if (!bHaveFrustum)
		{
			return true;
		}
		const FBox Box = Bounds.GetBox();
		const double Height = FMath::Max(Box.GetSize().Z, 1.0);
		const double Reach = FMath::Min(Height / FMath::Max(Towards.Z, 0.15), 32.0 * McCoords::UnitsPerBlock);
		const FBox Swept = Box + Box.ShiftBy(-Towards * Reach);
		return Frustum.IntersectBox(Swept.GetCenter(), Swept.GetExtent());
	}
}

namespace McEntityModel
{
	namespace
	{
		using McPlayerModel::EBoxFace;

		FBox Box(int32 U, int32 V, float X, float Y, float Z, int32 W, int32 H, int32 D, float Inflate = 0.0f, bool bMirror = false)
		{
			FBox Result;
			Result.UV = FIntPoint(U, V);
			Result.Min = FVector(X, Y, Z);
			Result.Size = FIntVector(W, H, D);
			Result.Inflate = Inflate;
			Result.bMirror = bMirror;
			return Result;
		}

		FPart Part(ERole Role, FVector Pivot, std::initializer_list<FBox> Boxes, float Pitch = 0.0f, float Yaw = 0.0f, float Roll = 0.0f, int32 Index = 0, int32 Layer = 0)
		{
			FPart Result;
			Result.Role = Role;
			Result.Index = Index;
			Result.Layer = Layer;
			Result.Pivot = Pivot;
			Result.RestPitch = Pitch;
			Result.RestYaw = Yaw;
			Result.RestRoll = Roll;
			Result.Boxes = Boxes;
			return Result;
		}

		// A rabbit's hop, from the logged model (13 ticks from the first move of the hind legs; the logged curves are all turns about
		// the sideways axis, so they compose as sums): the body's pitch, the head's, the tail's and the front legs' (those two
		// also move: the front legs' pivot (y, z) in the body's frame) and the hind legs'.
		constexpr int32 HopSteps = 14;
		constexpr float HopBody[HopSteps] = { -0.390f, -0.371f, -0.146f, 0.152f, 0.225f, 0.210f, 0.163f, 0.093f, 0.004f, -0.087f, -0.182f, -0.276f, -0.370f, -0.3927f };
		constexpr float HopHead[HopSteps] = { -0.011f, -0.053f, -0.245f, -0.540f, -0.585f, -0.588f, -0.527f, -0.466f, -0.404f, -0.339f, -0.234f, -0.130f, -0.025f, 0.0f };
		constexpr float HopTail[HopSteps] = { -0.393f, -0.225f, -0.058f, 0.110f, 0.286f, 0.463f, 0.399f, 0.335f, 0.271f, 0.207f, 0.143f, 0.079f, 0.015f, 0.0f };
		constexpr float HopFront[HopSteps] = { 0.066f, 0.333f, -0.034f, -1.094f, -1.291f, -1.323f, -1.239f, -0.972f, -0.625f, -0.278f, -0.022f, -0.022f, -0.022f, 0.0f };
		constexpr float HopFrontY[HopSteps] = { -1.57f, -1.69f, -1.81f, -1.93f, -2.06f, -2.30f, -2.38f, -2.24f, -2.11f, -1.97f, -1.84f, -1.70f, -1.57f, -1.53f };
		constexpr float HopFrontZ[HopSteps] = { -6.27f, -6.13f, -5.99f, -5.84f, -5.72f, -5.84f, -5.94f, -6.00f, -6.06f, -6.12f, -6.18f, -6.24f, -6.30f, -6.31f };
		constexpr float HopBack[HopSteps] = { 0.227f, 1.100f, 1.972f, 2.182f, 2.133f, 2.026f, 1.764f, 1.329f, 0.774f, 0.462f, 0.168f, 0.0f, 0.0f, 0.0f };
		constexpr float HopHind[HopSteps] = { 0.0f, 0.427f, 0.880f, 0.927f, 0.926f, 0.878f, 0.405f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

		float HopAt(const float (&Table)[HopSteps], float Pos)
		{
			const int32 Low = FMath::Clamp(FMath::FloorToInt(Pos), 0, HopSteps - 2);
			return FMath::Lerp(Table[Low], Table[Low + 1], FMath::Clamp(Pos - Low, 0.0f, 1.0f));
		}

		FVector TurnAbout(const FVector& Centre, float Pitch, const FVector& Point)
		{
			return Centre + FQuat(FVector::XAxisVector, Pitch).RotateVector(Point - Centre);
		}

		// Moves a rabbit's part (its pivot and turn) to where the hop has it.
		void ApplyHop(float Pos, int32 Group, FVector& Pivot, FQuat& Rotation)
		{
			constexpr float RestBody = -0.3927f;
			const FVector Hips(0.0f, 1.0f, -4.0f);
			const float Body = HopAt(HopBody, Pos);
			// Where a part that hangs from the body is: its place in the body's own frame, turned by the body's pitch.
			const auto Hung = [&](const FVector& RestPivot)
			{
				return TurnAbout(Hips, Body, TurnAbout(Hips, -RestBody, RestPivot));
			};
			float Turn = 0.0f;
			switch (Group)
			{
			case 1:
				Turn = Body - RestBody;
				break;
			case 2:
				Turn = Body - RestBody + HopAt(HopHead, Pos);
				Pivot = Hung(Pivot);
				break;
			case 3:
				Turn = Body - RestBody + HopAt(HopTail, Pos);
				Pivot = Hung(Pivot);
				break;
			case 4:
			{
				Turn = Body - RestBody + HopAt(HopFront, Pos);
				const FVector Rest = Hung(Pivot);
				// The group's pivot moves in the body's frame (ours: y and z the other way round from the log's).
				const FVector Shift(0.0f, -(HopAt(HopFrontY, Pos) - HopFrontY[HopSteps - 1]), -(HopAt(HopFrontZ, Pos) - HopFrontZ[HopSteps - 1]));
				Pivot = Rest + FQuat(FVector::XAxisVector, Body).RotateVector(Shift);
				break;
			}
			case 5:
				Turn = HopAt(HopBack, Pos);
				Pivot = TurnAbout(Hips, Turn, Pivot);
				break;
			default:
				return;
			}
			Rotation = FQuat(FVector::XAxisVector, Turn) * Rotation;
		}

		// A box tilted about a point of its own (in the part's frame).
		FBox Posed(FBox Each, FVector Pivot, float Pitch, float Yaw, float Roll)
		{
			Each.BoxPivot = Pivot;
			Each.BoxRotation = FQuat(FVector::ZAxisVector, Roll) * FQuat(FVector::YAxisVector, Yaw) * FQuat(FVector::XAxisVector, Pitch);
			return Each;
		}

		// The four legs of a pig, cow or sheep.
		// LeftU, if it is not negative, is where the left legs' own picture starts (the ravager has one for each side).
		void AddLegs(FModel& Model, int32 U, int32 V, float Width, float Height, float X, float Front, float Back, float Top, int32 LeftU = -1)
		{
			const FBox Leg = Box(U, V, -Width * 0.5f, -Height, -Width * 0.5f, static_cast<int32>(Width), static_cast<int32>(Height), static_cast<int32>(Width));
			const FBox LeftLeg = LeftU < 0 ? Leg : Box(LeftU, V, -Width * 0.5f, -Height, -Width * 0.5f, static_cast<int32>(Width), static_cast<int32>(Height), static_cast<int32>(Width));
			Model.Parts.Add(Part(ERole::LegFrontRight, FVector(-X, Top, Front), { Leg }));
			Model.Parts.Add(Part(ERole::LegFrontLeft, FVector(X, Top, Front), { LeftLeg }));
			Model.Parts.Add(Part(ERole::LegBackRight, FVector(-X, Top, Back), { Leg }));
			Model.Parts.Add(Part(ERole::LegBackLeft, FVector(X, Top, Back), { LeftLeg }));
		}

		// A person: head, body, two arms and two legs laid out on the player's skin. The left limbs have their own
		// pictures on a 64 high skin and share the right's, mirrored, on a 32 high one.
		void AddPerson(FModel& Model, bool bThin, bool bOwnLeftLimbs)
		{
			const float Arm = bThin ? 2.0f : 4.0f;
			const int32 ArmSize = bThin ? 2 : 4;
			Model.Parts.Add(Part(ERole::Head, FVector(0, 24, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8) }));
			Model.Parts.Add(Part(ERole::Static, FVector(0, 24, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4) }));
			const float Shoulder = bThin ? 5.0f : 5.0f;
			Model.Parts.Add(Part(ERole::ArmRight, FVector(-Shoulder, 22, 0), { Box(40, 16, bThin ? -1.0f : -3.0f, -10, -Arm * 0.5f, ArmSize, 12, ArmSize) }));
			Model.Parts.Add(Part(ERole::ArmLeft, FVector(Shoulder, 22, 0),
				{ bOwnLeftLimbs ? Box(32, 48, -1, -10, -Arm * 0.5f, ArmSize, 12, ArmSize) : Box(40, 16, bThin ? -1.0f : -1.0f, -10, -Arm * 0.5f, ArmSize, 12, ArmSize, 0.0f, true) }));
			const float LegWidth = bThin ? 2.0f : 4.0f;
			const int32 LegSize = bThin ? 2 : 4;
			Model.Parts.Add(Part(ERole::LegRight, FVector(-1.9f, 12, 0), { Box(0, 16, -LegWidth * 0.5f, -12, -LegWidth * 0.5f, LegSize, 12, LegSize) }));
			Model.Parts.Add(Part(ERole::LegLeft, FVector(1.9f, 12, 0),
				{ bOwnLeftLimbs ? Box(16, 48, -LegWidth * 0.5f, -12, -LegWidth * 0.5f, LegSize, 12, LegSize) : Box(0, 16, -LegWidth * 0.5f, -12, -LegWidth * 0.5f, LegSize, 12, LegSize, 0.0f, true) }));
		}

	// A boat's two paddles: a shaft and a blade each, cut from the boat's picture (the left and right pictures are where the texture has them: stacked on a boat's, side by side on a raft's).
	// They are mounted on the sides (HalfWidth from the middle, on a hull's gunwales or a raft's deck edges, clear of a
	// chest), Forward of the middle. A hull's are where paddle_log.txt's log model put them: 9 out, 11 up, 3 forward; a raft's
	// a pixel lower (vehicle_log3.txt: 9 out, 10 up, 3 forward), with the same stroke. The
	// boxes are vanilla's turned half round the shaft (x and y the other way), so the pose stays a turn (see PaddleLeft).
	void AddPaddles(FModel& Model, float Height, float HalfWidth, float Forward, FIntPoint Left, FIntPoint Right)
	{
		Model.Parts.Add(Part(ERole::PaddleLeft, FVector(HalfWidth, Height, Forward), { Box(Left.X, Left.Y, -1, -2, -5, 2, 2, 18), Box(Left.X, Left.Y, 0.001f, -3, 8, 1, 6, 7) }));
		Model.Parts.Add(Part(ERole::PaddleRight, FVector(-HalfWidth, Height, Forward), { Box(Right.X, Right.Y, -1, -2, -5, 2, 2, 18), Box(Right.X, Right.Y, -1.001f, -3, 8, 1, 6, 7) }));
	}

	// A chest (minecraft.wiki: "Chest"): a base 10 high with a lid 5 high over it, 14 across and a pixel in from the
	// block's sides, the lid's top at 14, and a lock on the front (-z) where they meet. Half of a double chest is 15
	// across, reaching the block's side its other half is on (Seam: +1 for +x, -1 for -x, 0 for a single chest), with
	// a lock 1 across at that side. A chest's picture is laid out the other way up from a mob's (the lid's top is where
	// a mob's net has its bottom, and the sides are upside down), so its boxes are turned half round the z axis: they
	// are given with x and y negated.
	void AddChest(FModel& Model, FVector Origin, float Scale, int32 Layer, int32 Seam)
	{
		const int32 Width = Seam == 0 ? 14 : 15;
		const float MinX = Seam > 0 ? -8.0f : -7.0f;
		const float LockX = Seam == 0 ? -1.0f : Seam > 0 ? -8.0f : 7.0f;
		const int32 LockWidth = Seam == 0 ? 2 : 1;
		FPart Base = Part(ERole::Static, Origin, { Box(0, 19, MinX, -10, -7, Width, 10, 14) }, 0, 0, PI, 0, Layer);
		Base.Scale = Scale;
		Model.Parts.Add(Base);
		// The lid and its lock swing about the lid's back edge.
		FPart Lid = Part(ERole::ChestLid, Origin + FVector(0, 9, 7) * Scale, { Box(0, 0, MinX, -5, -14, Width, 5, 14), Box(0, 0, LockX, -2, -15, LockWidth, 4, 1) }, 0, 0, PI, 0, Layer);
		Lid.Scale = Scale;
		Model.Parts.Add(Lid);
	}

		// A villager's body (minecraft.wiki, "Villager"): a head with a long nose, a hat, a robe, arms folded across the chest
		// and two legs. The biome type, profession and level pictures are laid over the base one into a single picture (the
		// game draws each over the whole body in turn, which comes to the same), so the hat and robe are cut from it as well.
		// A zombie villager holds its arms out in front like the zombie: two arms of its own, cut from the same place of the
		// picture the crossed ones are, instead of the folded pair.
		void AddVillager(FModel& Model, bool bArmsOut = false)
		{
			// Measured: the zombie villager is full size and swings its legs like a zombie; the others are 15/16 and swing half as far (0.7).
			Model.Scale = bArmsOut ? 1.0f : 15.0f / 16.0f;
			Model.LegSwing = bArmsOut ? 1.4f : 0.7f;
			Model.TextureSize = FIntPoint(64, 64);
			Model.BaseWidth = 0.6f;
			Model.bComposite = true;
			Model.Parts.Add(Part(ERole::Head, FVector(0, 24, 0), { Box(0, 0, -4, 0, -4, 8, 10, 8), Box(24, 0, -1, -1, 4, 2, 4, 2), Box(32, 0, -4, 0, -4, 8, 10, 8, 0.51f) }));
			Model.Parts.Add(Part(ERole::Static, FVector(0, 24, 0), { Box(16, 20, -4, -12, -3, 8, 12, 6), Box(0, 38, -4, -18, -3, 8, 18, 6, 0.5f) }));
			if (bArmsOut)
			{
				Model.bArmsForward = true;
				Model.Parts.Add(Part(ERole::ArmRight, FVector(-5, 22, 0), { Box(44, 22, -3, -10, -2, 4, 12, 4) }));
				Model.Parts.Add(Part(ERole::ArmLeft, FVector(5, 22, 0), { Box(44, 22, -1, -10, -2, 4, 12, 4, 0.0f, true) }));
			}
			else
			{
				Model.Parts.Add(Part(ERole::Static, FVector(0, 21, 1), { Box(44, 22, -8, -6, -2, 4, 8, 4), Box(44, 22, 4, -6, -2, 4, 8, 4, 0.0f, true), Box(40, 38, -4, -6, -2, 8, 4, 4) }, -0.75f));
			}
			Model.Parts.Add(Part(ERole::LegRight, FVector(-2, 12, 0), { Box(0, 22, -2, -12, -2, 4, 12, 4) }));
			Model.Parts.Add(Part(ERole::LegLeft, FVector(2, 12, 0), { Box(0, 22, -2, -12, -2, 4, 12, 4, 0.0f, true) }));
		}

		// The right hand's item of a humanoid arm, placed from vanilla shots: an armor stand holding an iron sword in two arm poses,
		// shot from the front, side and top, its logged hand matrix, and the sprite's outline fitted over all six (24 axis-aligned
		// turns, the display turn's order, and an offset; PLAN has the scores). The fit picked one turn clearly (0.46 against 0.36 for
		// the next) and put the sprite's centre 1.9 pixels in, 10.2 down and 6.3 forward of the hand frame, good to about a pixel
		// (the camera was set by eye). With the handheld display (turned -90 about y and 55 about z, 0.85 scale, moved 0, 4, 0.5
		// pixels, read from the item model data) the sprite lies in the arm's y-z plane, 13.6 pixels square, handle-to-tip diagonal
		// 10 degrees below the arm's forward line: our axis-aligned flat turned -35 degrees about x. The vex's hand frame is the
		// arm's with a 0.55 scale and a shift (logged: the hand matrix less the arm's own, in pixels, vanilla's axes).
		void AddHeldItemInHand(FModel& Model, const FVector& Pivot, float HandScale = 1.0f, const FVector& HandShift = FVector::ZeroVector)
		{
			constexpr float Tilt = -0.6109f;
			Model.HeldLayer = Model.Textures.Num();
			Model.Textures.Add(FString());
			// The sprite's centre in the arm's frame (vanilla's axes, y down and z back), then ours (y up, z forward).
			const FVector Vanilla = FVector(-1.93f, 10.21f, -6.34f) * HandScale + HandShift;
			const FVector Centre(Vanilla.X, -Vanilla.Y, -Vanilla.Z);
			const float Size = 13.6f * HandScale;
			// The box sits where the tilt about x lands its centre on Centre.
			const FVector Placed = FQuat(FVector::XAxisVector, -Tilt).RotateVector(Centre);
			FBox Picture = Box(0, 0, Centre.X, Placed.Y - Size * 0.5f, Placed.Z - Size * 0.5f, 0, Size, Size);
			Picture.bFlat = true;
			Model.Parts.Add(Part(ERole::ArmRight, Pivot, { Picture }, Tilt, 0, 0, 0, Model.HeldLayer));
		}

		// An illager (minecraft.wiki, "Illager"): the villager's shape without its robe's colour, on its own picture. Idle it
		// folds its arms like the villager; armed it has two arms of its own, held out with the weapon of its Style (1 a crossbow,
		// 2 an axe, 3 none: it casts), and Held is the item picture drawn in the right hand.
		void AddIllager(FModel& Model, int32 Style, const TCHAR* Held)
		{
			Model.Scale = 15.0f / 16.0f;
			AddVillager(Model, false);
			Model.bComposite = false;
			Model.IllagerStyle = Style;
			for (FPart& Each : Model.Parts)
			{
				if (Each.Role == ERole::Static && Each.Boxes.Num() == 3)
				{
					// The pillager never folds its arms: its own two hang at its sides.
					Each.ShowPoses = Style == 1 ? 0u : 1u;
				}
			}
			const uint32 Armed = Style == 1 ? ~0u : ~1u;
			Model.Parts.Add(Part(ERole::ArmRight, FVector(-5, 22, 0), { Box(40, 46, -3, -10, -2, 4, 12, 4) }));
			Model.Parts.Add(Part(ERole::ArmLeft, FVector(5, 22, 0), { Box(40, 46, -1, -10, -2, 4, 12, 4, 0.0f, true) }));
			Model.Parts.Last(1).ShowPoses = Armed;
			Model.Parts.Last(0).ShowPoses = Armed;
			if (Held)
			{
				Model.Textures.Add(FString(TEXT("../item/")) + Held + TEXT(".png"));
				FBox Picture = Style == 1 ? Box(0, 0, -1, -16, 0, 12, 12, 0) : Box(0, 0, -1, -22, -1, 0, 12, 12);
				Picture.bFlat = true;
				// The crossbow lies flat along the arms, its barrel forward (the item model's display turns it down by 90 degrees).
				Picture.bFlatTop = Style == 1;
				FPart Hand = Part(ERole::ArmRight, FVector(-5, 22, 0), { Picture }, 0.0f, 0, 0, 0, 1);
				Hand.ShowPoses = Armed;
				Model.Parts.Add(Hand);
			}
		}

		TMap<FName, FModel> BuildModels()
		{
			TMap<FName, FModel> Models;
			constexpr float Flat = HALF_PI;

			{
				FModel Pig;
				Pig.Kind = TEXT("minecraft:pig");
				Pig.TextureSize = FIntPoint(64, 64);
				Pig.Textures = { TEXT("pig/pig_temperate.png") };
				Pig.Variants = { { TEXT("pig/pig_cold.png") }, { TEXT("pig/pig_temperate.png") }, { TEXT("pig/pig_warm.png") } };
				Pig.Parts.Add(Part(ERole::Static, FVector(0, 10, 0), { Box(28, 8, -5, -8, -4, 10, 16, 8) }, Flat));
				Pig.Parts.Add(Part(ERole::Head, FVector(0, 12, 6), { Box(0, 0, -4, -4, 0, 8, 8, 8), Box(16, 16, -2, -3, 8, 4, 3, 1) }));
				AddLegs(Pig, 0, 16, 4, 6, 3, 5, -7, 6);
				Models.Add(Pig.Kind, Pig);
			}
			{
				FModel Cow;
				Cow.Kind = TEXT("minecraft:cow");
				Cow.TextureSize = FIntPoint(64, 64);
				Cow.Textures = { TEXT("cow/cow_temperate.png") };
				Cow.Variants = { { TEXT("cow/cow_cold.png") }, { TEXT("cow/cow_temperate.png") }, { TEXT("cow/cow_warm.png") } };
				Cow.Parts.Add(Part(ERole::Static, FVector(0, 17, -1), { Box(18, 4, -6, -9, -5, 12, 18, 10), Box(52, 0, -2, -9, 5, 4, 6, 1) }, Flat));
				Cow.Parts.Add(Part(ERole::Head, FVector(0, 20, 8), { Box(0, 0, -4, -4, 0, 8, 8, 6), Box(22, 0, -5, 2, 4, 1, 3, 1), Box(22, 0, 4, 2, 4, 1, 3, 1, 0.0f, true), Box(1, 33, -3, -4, 6, 6, 3, 1) }));
				AddLegs(Cow, 0, 16, 4, 12, 4, 5, -7, 12);
				Models.Add(Cow.Kind, Cow);
				// Mooshroom: the cow's body and head in its own pictures (red; brown is a variant). The mushrooms on its back
				// are not drawn yet (open, see PLAN.md "Missing mobs").
				FModel Moo = Cow;
				Moo.Kind = TEXT("minecraft:mooshroom");
				Moo.Textures = { TEXT("cow/mooshroom_red.png") };
				Moo.Variants = { { TEXT("cow/mooshroom_red.png") }, { TEXT("cow/mooshroom_brown.png") } };
				Models.Add(Moo.Kind, Moo);
			}
			// Models of the missing mobs, batch 1 (2026-11-05): every box, pivot and rest turn is from the reference's own logged model (E:\mc-reference\scripts\models_b1.txt,
			// `log model`, adult, one frame at rest), converted (y and z flipped, roll and yaw negated). Animation, babies and layers (patterns, mushrooms, eyes, outer
			// slime layer) are not done; tropical fish are not here yet. The polar bear's logged root offset (y -4.8, which would float it 0.3 above the ground) is left out,
			// and the happy ghast's and sulfur cube's entity scale is not in the log (UNVERIFIED: their size and height are the model as logged).
			{
				FModel Ocelot;
				Ocelot.Kind = TEXT("minecraft:ocelot");
				Ocelot.TextureSize = FIntPoint(64, 32);
				Ocelot.Textures = { TEXT("cat/ocelot.png") };
				Ocelot.Parts.Add(Part(ERole::LegBackLeft, FVector(1.1f, 6.0f, -5.0f), { Box(8, 13, -1.0f, -6.0f, -3.0f, 2, 6, 2) }));  // leftHindLeg
				Ocelot.Parts.Add(Part(ERole::LegBackRight, FVector(-1.1f, 6.0f, -5.0f), { Box(8, 13, -1.0f, -6.0f, -3.0f, 2, 6, 2) }));  // rightHindLeg
				Ocelot.Parts.Add(Part(ERole::LegFrontLeft, FVector(1.2f, 9.9f, 5.0f), { Box(40, 0, -1.0f, -10.0f, -2.0f, 2, 10, 2) }));  // leftFrontLeg
				Ocelot.Parts.Add(Part(ERole::LegFrontRight, FVector(-1.2f, 9.9f, 5.0f), { Box(40, 0, -1.0f, -10.0f, -2.0f, 2, 10, 2) }));  // rightFrontLeg
				Ocelot.Parts.Add(Part(ERole::Static, FVector(0.0f, 9.0f, -8.0f), { Box(0, 15, -0.5f, -8.0f, -1.0f, 1, 8, 1) }, 0.9f, 0.0f, 0.0f));  // tail1
				Ocelot.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.0f, -14.0f), { Box(4, 15, -0.5f, -8.0f, -1.0f, 1, 8, 1) }, 1.7279f, 0.0f, 0.0f));  // tail2
				Ocelot.Parts.Add(Part(ERole::Head, FVector(0.0f, 9.0f, 9.0f), { Box(0, 0, -2.5f, -2.0f, -2.0f, 5, 4, 5), Box(0, 24, -1.5f, -1.999f, 2.0f, 3, 2, 2), Box(0, 10, -2.0f, 2.0f, -2.0f, 1, 1, 2), Box(6, 10, 1.0f, 2.0f, -2.0f, 1, 1, 2) }));  // head
				Ocelot.Parts.Add(Part(ERole::Static, FVector(0.0f, 12.0f, 10.0f), { Box(20, 0, -2.0f, -19.0f, 2.0f, 4, 16, 6) }, 1.5708f, 0.0f, 0.0f));  // body
				Models.Add(Ocelot.Kind, Ocelot);
			}
			{
				FModel PolarBear;
				PolarBear.Kind = TEXT("minecraft:polar_bear");
				PolarBear.TextureSize = FIntPoint(128, 64);
				PolarBear.Textures = { TEXT("bear/polarbear.png") };
				PolarBear.Parts.Add(Part(ERole::Head, FVector(0.0f, 14.0f, 16.0f), { Box(0, 0, -3.5f, -4.0f, -4.0f, 7, 7, 7), Box(0, 44, -2.5f, -4.0f, 3.0f, 5, 3, 3), Box(26, 0, -4.5f, 2.0f, 0.0f, 2, 2, 1), Box(26, 0, 2.5f, 2.0f, 0.0f, 2, 2, 1, 0.0f, true) }));  // head
				PolarBear.Parts.Add(Part(ERole::Static, FVector(-2.0f, 15.0f, -12.0f), { Box(0, 19, -5.0f, -1.0f, -4.0f, 14, 14, 11), Box(39, 0, -4.0f, 13.0f, -3.0f, 12, 12, 10) }, 1.5708f, 0.0f, 0.0f));  // body
				PolarBear.Parts.Add(Part(ERole::LegBackRight, FVector(-4.5f, 10.0f, -6.0f), { Box(50, 22, -2.0f, -10.0f, -6.0f, 4, 10, 8) }));  // rightHindLeg
				PolarBear.Parts.Add(Part(ERole::LegBackLeft, FVector(4.5f, 10.0f, -6.0f), { Box(50, 22, -2.0f, -10.0f, -6.0f, 4, 10, 8) }));  // leftHindLeg
				PolarBear.Parts.Add(Part(ERole::LegFrontRight, FVector(-3.5f, 10.0f, 8.0f), { Box(50, 40, -2.0f, -10.0f, -4.0f, 4, 10, 6) }));  // rightFrontLeg
				PolarBear.Parts.Add(Part(ERole::LegFrontLeft, FVector(3.5f, 10.0f, 8.0f), { Box(50, 40, -2.0f, -10.0f, -4.0f, 4, 10, 6) }));  // leftFrontLeg
				Models.Add(PolarBear.Kind, PolarBear);
			}
			{
				FModel Panda;
				Panda.Kind = TEXT("minecraft:panda");
				Panda.TextureSize = FIntPoint(64, 64);
				Panda.Textures = { TEXT("panda/panda.png") };
				Panda.Parts.Add(Part(ERole::Head, FVector(0.0f, 12.5f, 17.0f), { Box(0, 6, -6.5f, -5.0f, -5.0f, 13, 10, 9), Box(45, 16, -3.5f, -5.0f, 4.0f, 7, 5, 2), Box(52, 25, 3.5f, 4.0f, 0.0f, 5, 4, 1, 0.0f, true), Box(52, 25, -8.5f, 4.0f, 0.0f, 5, 4, 1) }));  // head
				Panda.Parts.Add(Part(ERole::Static, FVector(0.0f, 14.0f, 0.0f), { Box(0, 25, -9.5f, -13.0f, -6.5f, 19, 26, 13) }, 1.5708f, 0.0f, 0.0f));  // body
				Panda.Parts.Add(Part(ERole::LegBackRight, FVector(-5.5f, 9.0f, -9.0f), { Box(40, 0, -3.0f, -9.0f, -3.0f, 6, 9, 6) }));  // rightHindLeg
				Panda.Parts.Add(Part(ERole::LegBackLeft, FVector(5.5f, 9.0f, -9.0f), { Box(40, 0, -3.0f, -9.0f, -3.0f, 6, 9, 6) }));  // leftHindLeg
				Panda.Parts.Add(Part(ERole::LegFrontRight, FVector(-5.5f, 9.0f, 9.0f), { Box(40, 0, -3.0f, -9.0f, -3.0f, 6, 9, 6) }));  // rightFrontLeg
				Panda.Parts.Add(Part(ERole::LegFrontLeft, FVector(5.5f, 9.0f, 9.0f), { Box(40, 0, -3.0f, -9.0f, -3.0f, 6, 9, 6) }));  // leftFrontLeg
				Models.Add(Panda.Kind, Panda);
			}
			{
				FModel Parrot;
				Parrot.Kind = TEXT("minecraft:parrot");
				Parrot.TextureSize = FIntPoint(32, 32);
				Parrot.Textures = { TEXT("parrot/parrot_red_blue.png") };
				Parrot.Parts.Add(Part(ERole::Static, FVector(0.0f, 7.3243f, 3.0f), { Box(2, 8, -1.5f, -6.0f, -1.5f, 3, 6, 3) }, 0.4937f, 0.0f, 0.0f));  // body
				Parrot.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.7543f, -1.16f), { Box(22, 1, -1.5f, -3.0f, 0.0f, 3, 4, 1) }, 1.015f, 0.0f, 0.0f));  // tail
				Parrot.Parts.Add(Part(ERole::Static, FVector(1.5f, 6.8843f, 2.76f), { Box(19, 8, -0.5f, -5.0f, -1.5f, 1, 5, 3) }, -0.6981f, 3.1416f, 0.6729f));  // leftWing
				Parrot.Parts.Add(Part(ERole::Static, FVector(-1.5f, 6.8843f, 2.76f), { Box(19, 8, -0.5f, -5.0f, -1.5f, 1, 5, 3) }, -0.6981f, 3.1416f, -0.6729f));  // rightWing
				Parrot.Parts.Add(Part(ERole::Head, FVector(0.0f, 8.1343f, 2.76f), { Box(2, 2, -1.0f, -1.5f, -1.0f, 2, 3, 2) }));  // head
				Parrot.Parts.Add(Part(ERole::Static, FVector(0.0f, 8.6343f, 4.26f), { Box(11, 7, -0.5f, -1.0f, -0.5f, 1, 2, 1) }));  // head/beak1
				Parrot.Parts.Add(Part(ERole::Static, FVector(0.0f, 9.8843f, 5.21f), { Box(16, 7, -0.5f, -2.0f, -0.5f, 1, 2, 1) }));  // head/beak2
				Parrot.Parts.Add(Part(ERole::Static, FVector(0.0f, 10.2843f, 2.61f), { Box(2, 18, 0.0f, -1.0f, -2.0f, 0, 5, 4) }, -0.2214f, 0.0f, 0.0f));  // head/feather
				Parrot.Parts.Add(Part(ERole::Static, FVector(0.0f, 10.1343f, 3.76f), { Box(10, 0, -1.0f, -0.5f, -2.0f, 2, 1, 4) }));  // head/head2
				Parrot.Parts.Add(Part(ERole::Static, FVector(1.0f, 1.8243f, 1.05f), { Box(14, 18, -0.5f, -2.0f, -0.5f, 1, 2, 1) }, 0.6682f, 0.0f, 0.0f));  // leftLeg
				Parrot.Parts.Add(Part(ERole::Static, FVector(-1.0f, 1.8243f, 1.05f), { Box(14, 18, -0.5f, -2.0f, -0.5f, 1, 2, 1) }, 0.6682f, 0.0f, 0.0f));  // rightLeg
				Models.Add(Parrot.Kind, Parrot);
			}
			{
				FModel Squid;
				Squid.Kind = TEXT("minecraft:squid");
				Squid.TextureSize = FIntPoint(64, 32);
				Squid.Textures = { TEXT("squid/squid.png") };
				Squid.Parts.Add(Part(ERole::Static, FVector(5.0f, 9.0f, 0.0f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.1998f, -1.5708f, 0.0f));  // root/tentacle0
				Squid.Parts.Add(Part(ERole::Static, FVector(3.5355f, 9.0f, -3.5355f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.1998f, -0.7854f, 0.0f));  // root/tentacle1
				Squid.Parts.Add(Part(ERole::Static, FVector(0.0f, 9.0f, 5.0f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.1998f, 3.1416f, 0.0f));  // root/tentacle6
				Squid.Parts.Add(Part(ERole::Static, FVector(3.5355f, 9.0f, 3.5355f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.1998f, 3.927f, 0.0f));  // root/tentacle7
				Squid.Parts.Add(Part(ERole::Static, FVector(0.0f, 16.0f, 0.0f), { Box(0, 0, -6.0f, -8.0f, -6.0f, 12, 16, 12) }));  // root/body
				Squid.Parts.Add(Part(ERole::Static, FVector(-5.0f, 9.0f, 0.0f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.1998f, 1.5708f, 0.0f));  // root/tentacle4
				Squid.Parts.Add(Part(ERole::Static, FVector(-3.5355f, 9.0f, 3.5355f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.1998f, 2.3562f, 0.0f));  // root/tentacle5
				Squid.Parts.Add(Part(ERole::Static, FVector(0.0f, 9.0f, -5.0f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.1998f, 0.0f, 0.0f));  // root/tentacle2
				Squid.Parts.Add(Part(ERole::Static, FVector(-3.5355f, 9.0f, -3.5355f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.1998f, 0.7854f, 0.0f));  // root/tentacle3
				Models.Add(Squid.Kind, Squid);
			}
			{
				FModel GlowSquid;
				GlowSquid.Kind = TEXT("minecraft:glow_squid");
				GlowSquid.TextureSize = FIntPoint(64, 32);
				GlowSquid.Textures = { TEXT("squid/glow_squid.png") };
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(5.0f, 9.0f, 0.0f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.0f, -1.5708f, 0.0f));  // root/tentacle0
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(3.5355f, 9.0f, -3.5355f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.0f, -0.7854f, 0.0f));  // root/tentacle1
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(0.0f, 9.0f, 5.0f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.0f, 3.1416f, 0.0f));  // root/tentacle6
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(3.5355f, 9.0f, 3.5355f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.0f, 3.927f, 0.0f));  // root/tentacle7
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(0.0f, 16.0f, 0.0f), { Box(0, 0, -6.0f, -8.0f, -6.0f, 12, 16, 12) }));  // root/body
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(-5.0f, 9.0f, 0.0f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.0f, 1.5708f, 0.0f));  // root/tentacle4
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(-3.5355f, 9.0f, 3.5355f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.0f, 2.3562f, 0.0f));  // root/tentacle5
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(0.0f, 9.0f, -5.0f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }));  // root/tentacle2
				GlowSquid.Parts.Add(Part(ERole::Static, FVector(-3.5355f, 9.0f, -3.5355f), { Box(48, 0, -1.0f, -18.0f, -1.0f, 2, 18, 2) }, 0.0f, 0.7854f, 0.0f));  // root/tentacle3
				Models.Add(GlowSquid.Kind, GlowSquid);
			}
			{
				FModel Cod;
				Cod.Kind = TEXT("minecraft:cod");
				Cod.TextureSize = FIntPoint(32, 32);
				Cod.Textures = { TEXT("fish/cod.png") };
				Cod.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.0f, -7.0f), { Box(22, 3, 0.0f, -2.0f, -4.0f, 0, 4, 4) }, 0.0f, -0.1792f, 0.0f));  // tailFin
				Cod.Parts.Add(Part(ERole::Head, FVector(0.0f, 2.0f, 0.0f), { Box(11, 0, -1.0f, -2.0f, 0.0f, 2, 4, 3) }));  // root/head
				Cod.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.0f, 3.0f), { Box(0, 0, -1.0f, -1.0f, 0.0f, 2, 3, 1) }));  // root/nose
				Cod.Parts.Add(Part(ERole::Static, FVector(1.0f, 1.0f, 0.0f), { Box(22, 4, 0.0f, 0.0f, -1.0f, 2, 0, 2) }, 0.0f, 0.0f, -0.7854f));  // root/left_fin
				Cod.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.0f, 0.0f), { Box(20, -6, 0.0f, 0.0f, -5.0f, 0, 1, 6) }));  // root/top_fin
				Cod.Parts.Add(Part(ERole::Static, FVector(-1.0f, 1.0f, 0.0f), { Box(22, 1, -2.0f, 0.0f, -1.0f, 2, 0, 2) }, 0.0f, 0.0f, 0.7854f));  // root/right_fin
				Cod.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.0f, 0.0f), { Box(0, 0, -1.0f, -2.0f, -7.0f, 2, 4, 7) }));  // root/body
				Models.Add(Cod.Kind, Cod);
			}
			{
				FModel Salmon;
				Salmon.Kind = TEXT("minecraft:salmon");
				Salmon.TextureSize = FIntPoint(32, 32);
				Salmon.Textures = { TEXT("fish/salmon.png") };
				Salmon.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.0f, -0.8f), { Box(0, 13, -1.5f, -2.5f, -8.0f, 3, 5, 8) }, 0.0f, -0.3247f, 0.0f));  // bodyBack
				Salmon.Parts.Add(Part(ERole::Static, FVector(-0.319f, 8.5f, 0.1477f), { Box(0, 2, 0.0f, -2.0f, -4.0f, 0, 2, 4) }, 0.0f, -0.3247f, 0.0f));  // bodyBack/top_back_fin
				Salmon.Parts.Add(Part(ERole::Static, FVector(2.5522f, 4.0f, -8.382f), { Box(20, 10, 0.0f, -2.5f, -6.0f, 0, 5, 6) }, 0.0f, -0.3247f, 0.0f));  // bodyBack/back_fin
				Salmon.Parts.Add(Part(ERole::Head, FVector(0.0f, 4.0f, 7.2f), { Box(22, 0, -1.0f, -2.0f, 0.0f, 2, 4, 3) }));  // root/head
				Salmon.Parts.Add(Part(ERole::Static, FVector(1.5f, 2.5f, 7.2f), { Box(0, 0, 0.0f, 0.0f, -2.0f, 2, 0, 2) }, 0.0f, 0.0f, -0.7854f));  // root/left_fin
				Salmon.Parts.Add(Part(ERole::Static, FVector(-1.5f, 2.5f, 7.2f), { Box(-4, 0, -2.0f, 0.0f, -2.0f, 2, 0, 2) }, 0.0f, 0.0f, 0.7854f));  // root/right_fin
				Salmon.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.0f, 7.2f), { Box(0, 0, -1.5f, -2.5f, -8.0f, 3, 5, 8) }));  // root/body_front
				Salmon.Parts.Add(Part(ERole::Static, FVector(0.0f, 8.5f, 2.2f), { Box(2, 1, 0.0f, -2.0f, -3.0f, 0, 2, 3) }));  // root/body_front/top_front_fin
				Models.Add(Salmon.Kind, Salmon);
			}
			{
				FModel Puffer;
				Puffer.Kind = TEXT("minecraft:pufferfish");
				Puffer.TextureSize = FIntPoint(32, 32);
				Puffer.Textures = { TEXT("fish/pufferfish.png") };
				Puffer.Parts.Add(Part(ERole::Static, FVector(2.5f, 6.0f, 1.5f), { Box(24, 3, 0.0f, 0.0f, -2.0f, 2, 0, 2) }));  // leftBlueFin
				Puffer.Parts.Add(Part(ERole::Static, FVector(-2.5f, 6.0f, 1.5f), { Box(24, 0, -2.0f, 0.0f, -2.0f, 2, 0, 2) }));  // rightBlueFin
				Puffer.Parts.Add(Part(ERole::Static, FVector(0.0f, 7.0f, -2.5f), { Box(11, 17, -2.5f, 0.0f, 0.0f, 5, 1, 0) }, -0.7854f, 0.0f, 0.0f));  // root/top_back_fin
				Puffer.Parts.Add(Part(ERole::Static, FVector(2.5f, 2.0f, -2.5f), { Box(1, 17, 0.0f, 0.0f, 0.0f, 1, 5, 0) }, 0.0f, 0.7854f, 0.0f));  // root/left_back_fin
				Puffer.Parts.Add(Part(ERole::Static, FVector(2.5f, 2.0f, 2.5f), { Box(1, 17, 0.0f, 0.0f, 0.0f, 1, 5, 0) }, 0.0f, -0.7854f, 0.0f));  // root/left_front_fin
				Puffer.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.0f, 2.5f), { Box(17, 19, -2.5f, -1.0f, -1.0f, 5, 1, 1) }, -0.7854f, 0.0f, 0.0f));  // root/bottom_front_fin
				Puffer.Parts.Add(Part(ERole::Static, FVector(-2.5f, 2.0f, 2.5f), { Box(5, 17, -1.0f, 0.0f, 0.0f, 1, 5, 0) }, 0.0f, 0.7854f, 0.0f));  // root/right_front_fin
				Puffer.Parts.Add(Part(ERole::Static, FVector(-2.5f, 2.0f, -2.5f), { Box(9, 17, -1.0f, 0.0f, 0.0f, 1, 5, 0) }, 0.0f, -0.7854f, 0.0f));  // root/right_back_fin
				Puffer.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.0f, 0.0f), { Box(12, 22, -2.5f, 0.0f, -2.5f, 5, 5, 5) }));  // root/body
				Puffer.Parts.Add(Part(ERole::Static, FVector(0.0f, 7.0f, 2.5f), { Box(19, 17, -2.5f, 0.0f, 0.0f, 5, 1, 0) }, 0.7854f, 0.0f, 0.0f));  // root/top_front_fin
				Puffer.Parts.Add(Part(ERole::Static, FVector(-2.5f, 2.0f, -2.5f), { Box(18, 20, 0.0f, -1.0f, 0.0f, 5, 1, 0) }, 0.7854f, 0.0f, 0.0f));  // root/bottom_back_fin
				Models.Add(Puffer.Kind, Puffer);
			}
			{
				FModel Tadpole;
				Tadpole.Kind = TEXT("minecraft:tadpole");
				Tadpole.TextureSize = FIntPoint(16, 16);
				Tadpole.Textures = { TEXT("tadpole/tadpole.png") };
				Tadpole.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.0f, 0.0f), { Box(0, 0, 0.0f, -1.0f, -7.0f, 0, 2, 7) }, 0.0f, -0.3745f, 0.0f));  // tail
				Tadpole.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.0f, 3.0f), { Box(0, 0, -1.5f, -1.0f, -3.0f, 3, 2, 3) }));  // root/body
				Models.Add(Tadpole.Kind, Tadpole);
			}
			{
				FModel Axolotl;
				Axolotl.Kind = TEXT("minecraft:axolotl");
				Axolotl.TextureSize = FIntPoint(64, 64);
				Axolotl.Textures = { TEXT("axolotl/axolotl_lucy.png") };
				Axolotl.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.5f, -6.0f), { Box(2, 19, 0.0f, -2.0f, -12.0f, 0, 5, 12) }));  // tail
				Axolotl.Parts.Add(Part(ERole::LegBackLeft, FVector(3.5f, 3.5f, -4.0f), { Box(2, 13, -1.0f, -5.0f, 0.0f, 3, 5, 0, 0.0f, true) }));  // leftHindLeg
				Axolotl.Parts.Add(Part(ERole::LegBackRight, FVector(-3.5f, 3.5f, -4.0f), { Box(2, 13, -2.0f, -5.0f, 0.0f, 3, 5, 0) }));  // rightHindLeg
				Axolotl.Parts.Add(Part(ERole::LegFrontLeft, FVector(3.5f, 3.5f, 3.0f), { Box(2, 13, -1.0f, -5.0f, 0.0f, 3, 5, 0, 0.0f, true) }));  // leftFrontLeg
				Axolotl.Parts.Add(Part(ERole::LegFrontRight, FVector(-3.5f, 3.5f, 3.0f), { Box(2, 13, -2.0f, -5.0f, 0.0f, 3, 5, 0) }));  // rightFrontLeg
				Axolotl.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.5f, -5.0f), { Box(0, 11, -4.0f, -2.0f, -1.0f, 8, 4, 10), Box(2, 17, 0.0f, -2.0f, -1.0f, 0, 5, 9) }));  // body
				Axolotl.Parts.Add(Part(ERole::Head, FVector(0.0f, 4.5f, 4.0f), { Box(0, 1, -4.0f, -2.0f, 0.0f, 8, 5, 5) }));  // body/head
				Axolotl.Parts.Add(Part(ERole::Static, FVector(0.0f, 7.5f, 5.0f), { Box(3, 37, -4.0f, 0.0f, 0.0f, 8, 3, 0) }));  // body/head/top_gills
				Axolotl.Parts.Add(Part(ERole::Static, FVector(-4.0f, 4.5f, 5.0f), { Box(0, 40, -3.0f, -2.0f, 0.0f, 3, 7, 0) }));  // body/head/left_gills
				Axolotl.Parts.Add(Part(ERole::Static, FVector(4.0f, 4.5f, 5.0f), { Box(11, 40, 0.0f, -2.0f, 0.0f, 3, 7, 0) }));  // body/head/right_gills
				Models.Add(Axolotl.Kind, Axolotl);
			}
			{
				FModel SnowGolem;
				SnowGolem.Kind = TEXT("minecraft:snow_golem");
				SnowGolem.TextureSize = FIntPoint(64, 64);
				SnowGolem.Textures = { TEXT("snow_golem/snow_golem.png") };
				SnowGolem.Parts.Add(Part(ERole::Static, FVector(0.0f, 11.0f, 0.0f), { Box(0, 16, -5.0f, 0.0f, -5.0f, 10, 10, 10) }));  // upperBody
				SnowGolem.Parts.Add(Part(ERole::Head, FVector(0.0f, 20.0f, 0.0f), { Box(0, 0, -4.0f, 0.0f, -4.0f, 8, 8, 8) }));  // head
				SnowGolem.Parts.Add(Part(ERole::Static, FVector(5.0f, 18.0f, 0.0f), { Box(32, 0, -1.0f, -2.0f, -1.0f, 12, 2, 2) }, 0.0f, 0.0f, -1.0f));  // leftArm
				SnowGolem.Parts.Add(Part(ERole::Static, FVector(-5.0f, 18.0f, 0.0f), { Box(32, 0, -1.0f, -2.0f, -1.0f, 12, 2, 2) }, 0.0f, -3.1416f, 1.0f));  // rightArm
				SnowGolem.Parts.Add(Part(ERole::Static, FVector(0.0f, 0.0f, 0.0f), { Box(0, 36, -6.0f, 0.0f, -6.0f, 12, 12, 12) }));  // root/lower_body
				Models.Add(SnowGolem.Kind, SnowGolem);
			}
			{
				FModel Armadillo;
				Armadillo.Kind = TEXT("minecraft:armadillo");
				Armadillo.TextureSize = FIntPoint(64, 64);
				Armadillo.Textures = { TEXT("armadillo/armadillo.png") };
				Armadillo.Parts.Add(Part(ERole::Static, FVector(0.0f, 3.0f, -4.0f), { Box(0, 20, -4.0f, -1.0f, -2.0f, 8, 8, 12), Box(0, 40, -4.0f, -1.0f, -2.0f, 8, 8, 12) }));  // body
				Armadillo.Parts.Add(Part(ERole::Static, FVector(0.0f, 5.0f, 7.0f), { Box(43, 15, -1.5f, -4.0f, -1.0f, 3, 5, 2) }, -0.3927f, 0.0f, 0.0f));  // body/head/head_cube
				Armadillo.Parts.Add(Part(ERole::Static, FVector(-1.5f, 6.0f, 7.6f), { Box(43, 10, -2.0f, -2.0f, 0.0f, 2, 5, 0) }, 0.1886f, 0.3864f, 0.0718f));  // body/head/right_ear/right_ear_cube
				Armadillo.Parts.Add(Part(ERole::Static, FVector(1.5f, 6.0f, 7.6f), { Box(47, 10, 0.0f, -2.0f, 0.0f, 2, 5, 0) }, 0.1886f, -0.3864f, -0.0718f));  // body/head/left_ear/left_ear_cube
				Armadillo.Parts.Add(Part(ERole::Static, FVector(0.0f, 6.0f, -5.0f), { Box(44, 53, -0.5f, -5.914f, -1.093f, 1, 6, 1) }, 0.5061f, 0.0f, 0.0f));  // body/tail
				Armadillo.Parts.Add(Part(ERole::LegBackRight, FVector(-2.0f, 3.0f, -4.0f), { Box(51, 31, -1.0f, -3.0f, -1.0f, 2, 3, 2) }));  // rightHindLeg
				Armadillo.Parts.Add(Part(ERole::LegBackLeft, FVector(2.0f, 3.0f, -4.0f), { Box(42, 31, -1.0f, -3.0f, -1.0f, 2, 3, 2) }));  // leftHindLeg
				Armadillo.Parts.Add(Part(ERole::Static, FVector(-2.0f, 3.0f, 4.0f), { Box(51, 43, -1.0f, -3.0f, -1.0f, 2, 3, 2) }));  // root/right_front_leg
				Armadillo.Parts.Add(Part(ERole::Static, FVector(2.0f, 3.0f, 4.0f), { Box(42, 43, -1.0f, -3.0f, -1.0f, 2, 3, 2) }));  // root/left_front_leg
				Models.Add(Armadillo.Kind, Armadillo);
			}
			{
				FModel Zoglin;
				Zoglin.Kind = TEXT("minecraft:zoglin");
				Zoglin.TextureSize = FIntPoint(128, 64);
				Zoglin.Textures = { TEXT("hoglin/zoglin.png") };
				Zoglin.Parts.Add(Part(ERole::Head, FVector(0.0f, 22.0f, 12.0f), { Box(61, 1, -7.0f, -3.0f, 0.0f, 14, 6, 19) }, 0.8727f, 0.0f, 0.0f));  // head
				Zoglin.Parts.Add(Part(ERole::Static, FVector(-7.0f, 20.0f, 24.0f), { Box(10, 13, -1.0f, 0.0f, -1.0f, 2, 11, 2) }));  // head/right_horn
				Zoglin.Parts.Add(Part(ERole::Static, FVector(7.0f, 20.0f, 24.0f), { Box(1, 13, -1.0f, 0.0f, -1.0f, 2, 11, 2) }));  // head/left_horn
				Zoglin.Parts.Add(Part(ERole::Static, FVector(-6.0f, 24.0f, 15.0f), { Box(1, 1, -6.0f, 0.0f, -2.0f, 6, 1, 4) }, 0.0f, 0.0f, 0.6981f));  // head/right_ear
				Zoglin.Parts.Add(Part(ERole::Static, FVector(6.0f, 24.0f, 15.0f), { Box(1, 6, 0.0f, 0.0f, -2.0f, 6, 1, 4) }, 0.0f, 0.0f, -0.6981f));  // head/left_ear
				Zoglin.Parts.Add(Part(ERole::LegFrontRight, FVector(-4.0f, 14.0f, 8.5f), { Box(66, 42, -3.0f, -14.0f, -3.0f, 6, 14, 6) }));  // rightFrontLeg
				Zoglin.Parts.Add(Part(ERole::LegFrontLeft, FVector(4.0f, 14.0f, 8.5f), { Box(41, 42, -3.0f, -14.0f, -3.0f, 6, 14, 6) }));  // leftFrontLeg
				Zoglin.Parts.Add(Part(ERole::LegBackRight, FVector(-5.0f, 11.0f, -10.0f), { Box(21, 45, -2.5f, -11.0f, -2.5f, 5, 11, 5) }));  // rightHindLeg
				Zoglin.Parts.Add(Part(ERole::LegBackLeft, FVector(5.0f, 11.0f, -10.0f), { Box(0, 45, -2.5f, -11.0f, -2.5f, 5, 11, 5) }));  // leftHindLeg
				Zoglin.Parts.Add(Part(ERole::Static, FVector(0.0f, 17.0f, 0.0f), { Box(1, 1, -8.0f, -7.0f, -13.0f, 16, 14, 26) }));  // root/body
				Zoglin.Parts.Add(Part(ERole::Static, FVector(0.0f, 31.0f, 7.0f), { Box(90, 33, 0.0f, -10.0f, -10.0f, 0, 10, 19) }));  // root/body/mane
				Models.Add(Zoglin.Kind, Zoglin);
			}
			{
				FModel Allay;
				Allay.Kind = TEXT("minecraft:allay");
				Allay.TextureSize = FIntPoint(32, 32);
				Allay.Textures = { TEXT("allay/allay.png") };
				Allay.Parts.Add(Part(ERole::Head, FVector(0.0f, 4.241f, 0.0f), { Box(0, 0, -2.5f, 0.0f, -2.5f, 5, 5, 5) }));  // head
				Allay.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.251f, 0.0f), { Box(0, 10, -1.5f, -4.0f, -1.0f, 3, 4, 2), Box(0, 16, -1.5f, -5.0f, -1.0f, 3, 5, 2) }));  // body
				Allay.Parts.Add(Part(ERole::Static, FVector(-1.75f, 3.751f, 0.0f), { Box(23, 0, -0.75f, -3.5f, -1.0f, 1, 4, 2) }, 0.0f, 0.0f, -0.4149f));  // body/right_arm
				Allay.Parts.Add(Part(ERole::Static, FVector(1.75f, 3.751f, 0.0f), { Box(23, 6, -0.25f, -3.5f, -1.0f, 1, 4, 2) }, 0.0f, 0.0f, 0.4149f));  // body/left_arm
				Allay.Parts.Add(Part(ERole::Static, FVector(-0.5f, 4.251f, -0.6f), { Box(16, 14, 0.0f, -6.0f, -8.0f, 0, 5, 8) }, 0.4363f, 0.7985f, 0.0f));  // body/right_wing
				Allay.Parts.Add(Part(ERole::Static, FVector(0.5f, 4.251f, -0.6f), { Box(16, 14, 0.0f, -6.0f, -8.0f, 0, 5, 8) }, 0.4363f, -0.7985f, 0.0f));  // body/left_wing
				Models.Add(Allay.Kind, Allay);
			}
			{
				FModel Breeze;
				Breeze.Kind = TEXT("minecraft:breeze");
				Breeze.TextureSize = FIntPoint(32, 32);
				Breeze.Textures = { TEXT("breeze/breeze.png") };
				Breeze.Parts.Add(Part(ERole::Head, FVector(0.0f, 20.0096f, 0.0f), { Box(4, 24, -5.0f, 2.0f, 0.2f, 10, 3, 4), Box(0, 0, -4.0f, 0.0f, -4.0f, 8, 8, 8) }));  // head
				Breeze.Parts.Add(Part(ERole::Static, FVector(0.0f, 20.0096f, 0.0f), { Box(4, 24, -5.0f, 2.0f, 0.2f, 10, 3, 4), Box(0, 0, -4.0f, 0.0f, -4.0f, 8, 8, 8) }));  // head/eyes
				Breeze.Parts.Add(Part(ERole::Static, FVector(-2.3076f, 18.982f, -1.917f), { Box(0, 17, -1.0f, -8.0f, 1.0f, 2, 8, 2) }, -2.7489f, -1.2168f, -3.1416f));  // rods/rod_2
				Breeze.Parts.Add(Part(ERole::Static, FVector(-0.5064f, 18.982f, 2.957f), { Box(0, 17, -1.0f, -8.0f, 1.0f, 2, 8, 2) }, 0.3927f, -0.1696f, 0.0f));  // rods/rod_3
				Breeze.Parts.Add(Part(ERole::Static, FVector(2.814f, 18.982f, -1.04f), { Box(0, 17, -1.0f, -8.0f, 1.0f, 2, 8, 2) }, -2.7489f, 0.8776f, -3.1416f));  // rods/rod_1
				Models.Add(Breeze.Kind, Breeze);
			}
			{
				FModel Parched;
				Parched.Kind = TEXT("minecraft:parched");
				Parched.TextureSize = FIntPoint(64, 64);
				Parched.Textures = { TEXT("skeleton/parched.png") };
				Parched.Parts.Add(Part(ERole::Head, FVector(0.0f, 24.0f, 0.0f), { Box(0, 0, -4.0f, 0.0f, -4.0f, 8, 8, 8), Box(0, 32, -4.0f, 0.0f, -4.0f, 8, 8, 8) }));  // head
				Parched.Parts.Add(Part(ERole::Static, FVector(0.0f, 24.0f, 0.0f), { Box(16, 16, -4.0f, -12.0f, -2.0f, 8, 12, 4), Box(28, 0, -4.0f, -11.0f, -2.0f, 8, 1, 4), Box(16, 48, -4.0f, -12.0f, -2.0f, 8, 12, 4) }));  // body
				Parched.Parts.Add(Part(ERole::Static, FVector(-5.5f, 22.0f, 0.0f), { Box(40, 16, -1.0f, -10.0f, -1.0f, 2, 12, 2), Box(42, 33, -1.55f, -9.975f, -1.5f, 3, 12, 3) }, 0.0175f, 0.0f, -0.0087f));  // rightArm
				Parched.Parts.Add(Part(ERole::Static, FVector(5.5f, 22.0f, 0.0f), { Box(56, 16, -1.0f, -10.0f, -1.0f, 2, 12, 2), Box(40, 48, -1.45f, -9.975f, -1.5f, 3, 12, 3) }, -0.0175f, 0.0f, 0.0087f));  // leftArm
				Parched.Parts.Add(Part(ERole::Static, FVector(-2.0f, 12.0f, 0.0f), { Box(0, 16, -1.0f, -12.0f, -1.0f, 2, 12, 2), Box(0, 49, -1.5f, -12.0f, -1.5f, 3, 12, 3) }, 0.0f, -0.005f, -0.005f));  // rightLeg
				Parched.Parts.Add(Part(ERole::Static, FVector(2.0f, 12.0f, 0.0f), { Box(0, 16, -1.0f, -12.0f, -1.0f, 2, 12, 2), Box(4, 49, -1.5f, -12.0f, -1.5f, 3, 12, 3) }, 0.0f, 0.005f, 0.005f));  // leftLeg
				Models.Add(Parched.Kind, Parched);
			}
			{
				FModel CamelHusk;
				CamelHusk.Kind = TEXT("minecraft:camel_husk");
				CamelHusk.TextureSize = FIntPoint(128, 128);
				CamelHusk.Textures = { TEXT("camel/camel_husk.png") };
				CamelHusk.Parts.Add(Part(ERole::Head, FVector(0.0f, 27.0f, 19.5f), { Box(60, 24, -3.5f, -1.0f, -4.0f, 7, 8, 19), Box(21, 0, -3.5f, 7.0f, 8.0f, 7, 14, 7), Box(50, 0, -2.5f, 16.0f, 15.0f, 5, 5, 6) }, -0.0436f, 0.0f, 0.0f));  // head
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(-2.5f, 48.0f, 29.0f), { Box(67, 0, -2.5f, -1.5f, -1.0f, 3, 1, 2) }, 0.0f, 0.0f, -0.7854f));  // head/right_ear
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(2.5f, 48.0f, 29.0f), { Box(45, 0, -0.5f, -1.5f, -1.0f, 3, 1, 2) }, 0.0f, 0.0f, 0.7854f));  // head/left_ear
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(-4.9f, 23.0f, 10.5f), { Box(0, 26, -2.5f, -23.0f, -2.5f, 5, 21, 5) }));  // root/right_front_leg
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(-4.9f, 23.0f, -9.5f), { Box(94, 16, -2.5f, -23.0f, -2.5f, 5, 21, 5) }));  // root/right_hind_leg
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(4.9f, 23.0f, -9.5f), { Box(58, 16, -2.5f, -23.0f, -2.5f, 5, 21, 5) }));  // root/left_hind_leg
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(0.0f, 20.0f, -9.5f), { Box(0, 25, -7.5f, 0.0f, -3.5f, 15, 12, 27) }));  // root/body
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(0.0f, 32.0f, 0.5f), { Box(74, 0, -4.5f, 0.0f, -5.5f, 9, 5, 11) }));  // root/body/hump
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(0.0f, 29.0f, -13.0f), { Box(122, 0, -1.5f, -14.0f, 0.0f, 3, 14, 0) }, 0.087f, -0.0011f, 0.0123f));  // root/body/tail
				CamelHusk.Parts.Add(Part(ERole::Static, FVector(4.9f, 23.0f, 10.5f), { Box(0, 0, -2.5f, -23.0f, -2.5f, 5, 21, 5) }));  // root/left_front_leg
				Models.Add(CamelHusk.Kind, CamelHusk);
			}
			{
				FModel ZombieNautilus;
				ZombieNautilus.Kind = TEXT("minecraft:zombie_nautilus");
				ZombieNautilus.TextureSize = FIntPoint(128, 128);
				ZombieNautilus.Textures = { TEXT("nautilus/zombie_nautilus.png") };
				ZombieNautilus.Parts.Add(Part(ERole::Static, FVector(0.0f, 3.5f, -6.3f), { Box(0, 54, -5.0f, -3.49f, -11.0f, 10, 8, 14), Box(0, 76, -5.0f, -3.49f, -7.0f, 10, 8, 0) }));  // body
				ZombieNautilus.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.01f, -13.3f), { Box(54, 62, -5.0f, -2.02f, -4.0f, 10, 4, 4) }));  // body/lower_mouth
				ZombieNautilus.Parts.Add(Part(ERole::Static, FVector(0.0f, 6.01f, -13.3f), { Box(54, 54, -5.0f, -2.0f, -4.0f, 10, 4, 4) }));  // body/upper_mouth
				ZombieNautilus.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.01f, -13.8f), { Box(54, 70, -3.0f, -2.0f, -3.5f, 6, 4, 4) }));  // body/inner_mouth
				ZombieNautilus.Parts.Add(Part(ERole::Static, FVector(0.0f, 8.0f, 1.0f), { Box(0, 0, -7.0f, 0.0f, -9.0f, 14, 10, 16), Box(0, 26, -7.0f, -8.0f, -13.0f, 14, 8, 20), Box(48, 26, -7.0f, -8.0f, -6.0f, 14, 8, 0) }));  // nautilus/shell
				Models.Add(ZombieNautilus.Kind, ZombieNautilus);
			}
			{
				FModel HappyGhast;
				HappyGhast.Kind = TEXT("minecraft:happy_ghast");
				HappyGhast.TextureSize = FIntPoint(128, 128);
				HappyGhast.Textures = { TEXT("ghast/happy_ghast.png") };
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(0.0f, 80.048f, 0.0f), { Box(0, 0, -8.0f, -8.0f, -8.0f, 16, 16, 16) }));  // body
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(-3.75f, 73.048f, 5.0f), { Box(0, 0, -1.0f, -5.0f, -1.0f, 2, 5, 2) }, 0.4031f, 0.0f, 0.0f));  // body/tentacle0
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(1.25f, 73.048f, 5.0f), { Box(0, 0, -1.0f, -7.0f, -1.0f, 2, 7, 2) }, 0.57f, 0.0f, 0.0f));  // body/tentacle1
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(6.25f, 73.048f, -5.0f), { Box(0, 0, -1.0f, -5.0f, -1.0f, 2, 5, 2) }, 0.5974f, 0.0f, 0.0f));  // body/tentacle8
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(-3.75f, 73.048f, -5.0f), { Box(0, 0, -1.0f, -8.0f, -1.0f, 2, 8, 2) }, 0.3471f, 0.0f, 0.0f));  // body/tentacle6
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(1.25f, 73.048f, -5.0f), { Box(0, 0, -1.0f, -8.0f, -1.0f, 2, 8, 2) }, 0.5337f, 0.0f, 0.0f));  // body/tentacle7
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(-1.25f, 73.048f, 0.0f), { Box(0, 0, -1.0f, -5.0f, -1.0f, 2, 5, 2) }, 0.2466f, 0.0f, 0.0f));  // body/tentacle4
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(3.75f, 73.048f, 0.0f), { Box(0, 0, -1.0f, -7.0f, -1.0f, 2, 7, 2) }, 0.2091f, 0.0f, 0.0f));  // body/tentacle5
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(6.25f, 73.048f, 5.0f), { Box(0, 0, -1.0f, -4.0f, -1.0f, 2, 4, 2) }, 0.5805f, 0.0f, 0.0f));  // body/tentacle2
				HappyGhast.Parts.Add(Part(ERole::Static, FVector(-6.25f, 73.048f, 0.0f), { Box(0, 0, -1.0f, -5.0f, -1.0f, 2, 5, 2) }, 0.4251f, 0.0f, 0.0f));  // body/tentacle3
				Models.Add(HappyGhast.Kind, HappyGhast);
			}
			{
				FModel SulfurCube;
				SulfurCube.Kind = TEXT("minecraft:sulfur_cube");
				SulfurCube.TextureSize = FIntPoint(128, 128);
				SulfurCube.Textures = { TEXT("sulfur_cube/sulfur_cube_inner.png") };
				SulfurCube.Parts.Add(Part(ERole::Static, FVector(0.0f, 24.0f, 0.0f), { Box(0, 0, -9.0f, -9.0f, -9.0f, 18, 18, 18) }));  // root/cube
				Models.Add(SulfurCube.Kind, SulfurCube);
			}
			{
				FModel Sheep;
				Sheep.Kind = TEXT("minecraft:sheep");
				Sheep.Textures = { TEXT("sheep/sheep.png"), TEXT("sheep/sheep_wool.png") };
				Sheep.Parts.Add(Part(ERole::Static, FVector(0, 16, 0), { Box(28, 8, -4, -8, -3, 8, 16, 6) }, Flat));
				Sheep.Parts.Add(Part(ERole::Head, FVector(0, 18, 8), { Box(0, 0, -3, -2, -2, 6, 6, 8) }));
				AddLegs(Sheep, 0, 16, 4, 12, 3, 5, -7, 12);
				// The wool: a bigger body, a head covering, and the top of each leg.
				Sheep.Parts.Add(Part(ERole::Static, FVector(0, 16, 0), { Box(28, 8, -4, -8, -3, 8, 16, 6, 1.75f) }, Flat, 0, 0, 0, 1));
				Sheep.Parts.Add(Part(ERole::Head, FVector(0, 19, 6), { Box(0, 0, -3, -3, 0, 6, 6, 6, 0.6f) }, 0, 0, 0, 0, 1));
				const FBox WoolLeg = Box(0, 16, -2, -6, -2, 4, 6, 4, 0.5f);
				Sheep.Parts.Add(Part(ERole::LegFrontRight, FVector(-3, 12, 5), { WoolLeg }, 0, 0, 0, 0, 1));
				Sheep.Parts.Add(Part(ERole::LegFrontLeft, FVector(3, 12, 5), { WoolLeg }, 0, 0, 0, 0, 1));
				Sheep.Parts.Add(Part(ERole::LegBackRight, FVector(-3, 12, -7), { WoolLeg }, 0, 0, 0, 0, 1));
				Sheep.Parts.Add(Part(ERole::LegBackLeft, FVector(3, 12, -7), { WoolLeg }, 0, 0, 0, 0, 1));
				// The wool takes the sheep's dye (white when it has none).
				Sheep.TintedLayers = 2u;
				Models.Add(Sheep.Kind, Sheep);
			}
			{
				FModel Chicken;
				Chicken.Kind = TEXT("minecraft:chicken");
				Chicken.Textures = { TEXT("chicken/chicken_temperate.png") };
				// A chick is the same model at half the width.
				Chicken.BaseWidth = 0.4f;
				Chicken.Variants = { { TEXT("chicken/chicken_cold.png") }, { TEXT("chicken/chicken_temperate.png") }, { TEXT("chicken/chicken_warm.png") } };
				Chicken.Parts.Add(Part(ERole::Static, FVector(0, 8, 0), { Box(0, 9, -3, -4, -3, 6, 8, 6) }, Flat));
				Chicken.Parts.Add(Part(ERole::Head, FVector(0, 9, 4), { Box(0, 0, -2, 0, -1, 4, 6, 3), Box(14, 0, -2, 2, 2, 4, 2, 2), Box(14, 4, -1, 0, 1, 2, 2, 2) }));
				const FBox Leg = Box(26, 0, -1, -5, 0, 3, 5, 3);
				Chicken.Parts.Add(Part(ERole::LegRight, FVector(-2, 5, -1), { Leg }));
				Chicken.Parts.Add(Part(ERole::LegLeft, FVector(1, 5, -1), { Leg }));
				Chicken.Parts.Add(Part(ERole::WingRight, FVector(-3, 11, 0), { Box(24, 13, -1, -4, -3, 1, 4, 6) }));
				Chicken.Parts.Add(Part(ERole::WingLeft, FVector(3, 11, 0), { Box(24, 13, 0, -4, -3, 1, 4, 6) }));
				Models.Add(Chicken.Kind, Chicken);
			}
			{
				FModel Zombie;
				Zombie.Kind = TEXT("minecraft:zombie");
				Zombie.TextureSize = FIntPoint(64, 64);
				Zombie.Textures = { TEXT("zombie/zombie.png") };
				Zombie.bArmsForward = true;
				AddPerson(Zombie, false, true);
				AddHeldItemInHand(Zombie, FVector(-5, 22, 0));
				Models.Add(Zombie.Kind, Zombie);
			}
			{
				// The drowned: the zombie's body with its own picture (the swimming pose, the second picture it wears over it and the
				// trident's own hold are not done: the trident is drawn as its flat inventory picture).
				FModel Drowned;
				Drowned.Kind = TEXT("minecraft:drowned");
				Drowned.TextureSize = FIntPoint(64, 64);
				Drowned.Textures = { TEXT("zombie/drowned.png") };
				Drowned.bArmsForward = true;
				AddPerson(Drowned, false, true);
				AddHeldItemInHand(Drowned, FVector(-5, 22, 0));
				Models.Add(Drowned.Kind, Drowned);
			}
			{
				struct FIllager { const TCHAR* Name; int32 Style; const TCHAR* Held; };
				for (const FIllager& Each : { FIllager{ TEXT("pillager"), 1, TEXT("crossbow_standby") }, FIllager{ TEXT("vindicator"), 2, TEXT("iron_axe") }, FIllager{ TEXT("evoker"), 3, nullptr } })
				{
					FModel Illager;
					Illager.Kind = FName(*(FString(TEXT("minecraft:")) + Each.Name));
					Illager.Textures = { FString(TEXT("illager/")) + Each.Name + TEXT(".png") };
					AddIllager(Illager, Each.Style, Each.Held);
					Models.Add(Illager.Kind, Illager);
				}
			}
			{
				// The witch: a villager under a tall pointed hat of four stacked pieces.
				FModel Witch;
				Witch.Kind = TEXT("minecraft:witch");
				Witch.TextureSize = FIntPoint(64, 128);
				Witch.Textures = { TEXT("witch/witch.png") };
				AddVillager(Witch);
				Witch.TextureSize = FIntPoint(64, 128);
				Witch.bComposite = false;
				for (FPart& Part : Witch.Parts)
				{
					if (Part.Role == ERole::Head)
					{
						Part.Boxes.Reset();
						Part.Boxes.Add(Box(0, 0, -4, 0, -4, 8, 10, 8));
						FBox Nose = Box(24, 0, -1, -1, 4, 2, 4, 2);
						Nose.BoxPivot = FVector(0, 2, 0);
						Nose.bNoseWobble = true;
						Part.Boxes.Add(Nose);
						// The hat bends: the brim, then three cones each on the last, every one turned about its own pivot (the
						// pivots and turns are the vanilla model's as logged, x and y offsets in its own frame: y down, z south).
						const auto Turn = [](float Pitch, float Roll) { return FQuat(FVector::ZAxisVector, Roll) * FQuat(FVector::XAxisVector, Pitch); };
						FVector Pivot(-5.0f, 10.03125f, 5.0f);
						FQuat Frame = FQuat::Identity;
						FBox Brim = Box(0, 64, 0, 0, 0, 10, 10, 2);
						Brim.Min += Pivot;
						Brim.BoxPivot = Pivot;
						Brim.BoxRotation = FQuat(FVector::XAxisVector, -HALF_PI);
						Part.Boxes.Add(Brim);
						struct FCone { int32 V; int32 W, H, D; float Dx, Dy, Dz; float Pitch, Roll; };
						const FCone Cones[] = {
							{ 76, 7, 4, 7, 1.75f, 4.0f, -2.0f, -0.0524f, -0.0262f },
							{ 87, 4, 4, 4, 1.75f, 4.0f, -2.0f, -0.1047f, -0.0524f },
							{ 95, 1, 2, 1, 1.75f, 2.0f, -2.0f, -0.2094f, -0.1047f },
						};
						for (const FCone& Cone : Cones)
						{
							Pivot += Frame.RotateVector(FVector(Cone.Dx, Cone.Dy, Cone.Dz));
							Frame = Frame * Turn(Cone.Pitch, Cone.Roll);
							FBox Piece = Box(0, Cone.V, 0, -Cone.H, -Cone.D, Cone.W, Cone.H, Cone.D);
							Piece.Min += Pivot;
							Piece.BoxPivot = Pivot;
							Piece.BoxRotation = Frame;
							Part.Boxes.Add(Piece);
						}
					}
				}
				// The potion it holds up to drink, placed from the vanilla front and side shots of a witch holding one (witchhold.txt;
				// measured against the head's own width, 8 pixels, so the camera does not enter): a flat picture facing the front,
				// its plane at the face's front (z 4, the side view's sliver), the sprite's centre 0.6 pixels right of the middle and
				// 1.8 below the chin (y 22.2), 6.3 pixels for the sprite's 16 (its bottle 13 rows is 32 screen pixels against the head's
				// 50 for 8), turned 12.5 degrees in the picture's plane and mirrored (fitted over the bottle's outline, overlap 0.77).
				// The hold is only drawn while it
				// drinks, as the witch has nothing in hand otherwise.
				Witch.Textures.Add(TEXT("../item/potion.png"));
				FBox Bottle = Box(0, 0, 0, -3, -3, 0, 6, 6);  // 6.3 measured, whole pixels only
				Bottle.bFlat = true;
				Bottle.bMirror = true;
				FPart Drinking = Part(ERole::Static, FVector(0.6f, 22.2f, 6.0f), { Bottle }, 0, -HALF_PI, 0.2182f, 0, 1);
				Drinking.ShowPoses = 1u << 4;
				Witch.Parts.Add(Drinking);
				// The liquid: the grey overlay sprite times a colour. A plain potion's colour read off the same vanilla shot (median of 248
				// blue pixels, 47/78/161) over the overlay's usual grey (166): 72/120/247. Lit by the shot's own lighting, so about right.
				Witch.Textures.Add(TEXT("../item/potion_overlay.png"));
				Witch.TintedLayers = 1u << 2;
				Witch.bFixedTint = true;
				Witch.FixedTint = FColor(72, 120, 247);
				FBox Liquid = Bottle;
				FPart LiquidPart = Part(ERole::Static, FVector(0.6f, 22.2f, 6.05f), { Liquid }, 0, -HALF_PI, 0.2182f, 0, 2);
				LiquidPart.ShowPoses = 1u << 4;
				Witch.Parts.Add(LiquidPart);
				Models.Add(Witch.Kind, Witch);
			}
			{
				// The vex: a small head and body with arms, floating.
				FModel Vex;
				Vex.Kind = TEXT("minecraft:vex");
				Vex.TextureSize = FIntPoint(32, 32);
				Vex.Textures = { TEXT("illager/vex.png") };
				// Placed from the logged model (the model sits 2.5 higher than its parts say; the body leans 0.1571 forward at rest).
				Vex.Parts.Add(Part(ERole::Head, FVector(0, 6.5f, 0), { Box(0, 0, -2.5f, 0, -2.5f, 5, 5, 5) }));
				Vex.Parts.Add(Part(ERole::Static, FVector(0, 6.5f, 0), { Box(0, 10, -1.5f, -4, -1, 3, 4, 2), Box(0, 16, -1.5f, -6, -1, 3, 5, 2) }, 0.1571f));
				Vex.Parts.Add(Part(ERole::ArmRight, FVector(-1.75f, 6.25f, 0), { Box(23, 0, -1.25f, -3.5f, -1, 2, 4, 2) }));
				Vex.Parts.Add(Part(ERole::ArmLeft, FVector(1.75f, 6.25f, 0), { Box(23, 6, -0.75f, -3.5f, -1, 2, 4, 2) }));
				// Two wings at its back, beating all the time.
				Vex.bFlutter = true;
				Vex.Parts.Add(Part(ERole::WingRight, FVector(-0.5f, 5.5f, -1), { Box(16, 22, 0, -5, -8, 0, 5, 8) }));
				Vex.Parts.Add(Part(ERole::WingLeft, FVector(0.5f, 5.5f, -1), { Box(16, 22, 0, -5, -8, 0, 5, 8, 0.0f, true) }));
				AddHeldItemInHand(Vex, FVector(-1.75f, 6.25f, 0), 0.55f, FVector(-0.91f, -3.47f, 1.08f));
				Models.Add(Vex.Kind, Vex);
			}
			{
				// The ravager (minecraft.wiki, "Ravager"): a long torso between four tall legs and a big head
				// carried low in front with a jaw under it and two horns at its sides.
				FModel Ravager;
				Ravager.Kind = TEXT("minecraft:ravager");
				Ravager.TextureSize = FIntPoint(128, 128);
				Ravager.Textures = { TEXT("illager/ravager.png") };
				// The torso is two blocks end to end: the front 14x16x20 (its saddle patch on top) and behind it the 12x13x18
				// hindquarters, so it runs about 29 long and 20 high between four whole 8x37x8 legs that stand a little above
				// its back. The head hangs low in front, its bottom near the knees, the neck hidden inside it.
				Ravager.Parts.Add(Part(ERole::Static, FVector(0, 26, 0), { Box(0, 55, -7, -8, -10, 14, 16, 20), Box(0, 91, -6, -21, -8, 12, 13, 18) }, Flat));
				Ravager.Parts.Add(Part(ERole::Static, FVector(0, 27, 0), { Box(68, 73, -5, -5, -3, 10, 10, 15) }));
				// The head stands level with the top of the legs, its back against the front legs' fronts, so no neck shows; the nose hangs from the face.
				Ravager.Parts.Add(Part(ERole::Head, FVector(0, 25, 9), { Box(0, 0, -8, -10, 0, 16, 20, 16), Box(0, 36, -8, -13, 0, 16, 3, 16), Box(0, 0, -2, -12, 16, 4, 8, 4) }));
				// The horns stand on the head's top corners and lean well forward (vanilla's 63 degrees).
				Ravager.Parts.Add(Part(ERole::Head, FVector(-8.5f, 30, 19), { Box(74, 55, -1, 0, -2, 2, 14, 4) }, 1.1f, 0, 0));
				Ravager.Parts.Add(Part(ERole::Head, FVector(8.5f, 30, 19), { Box(74, 55, -1, 0, -2, 2, 14, 4, 0.0f, true) }, 1.1f, 0, 0));
				AddLegs(Ravager, 96, 0, 8, 37, 8, 5, -18, 37, 64);
				Ravager.LegSwing = 0.4f;
				Models.Add(Ravager.Kind, Ravager);
			}
			{
				// The evoker fangs: a base with a pair of jaws.
				FModel Fangs;
				Fangs.Kind = TEXT("minecraft:evoker_fangs");
				Fangs.TextureSize = FIntPoint(64, 32);
				Fangs.Textures = { TEXT("illager/evoker_fangs.png") };
				Fangs.Parts.Add(Part(ERole::Static, FVector(0, 0, 0), { Box(0, 0, -5, 0, -5, 10, 12, 10) }));
				Fangs.Parts.Add(Part(ERole::Static, FVector(0, 12, 0), { Box(40, 0, -3, 0, -2, 4, 14, 8), Box(40, 0, -1, 0, -2, 4, 14, 8, 0.0f, true) }));
				Models.Add(Fangs.Kind, Fangs);
			}
			{
				// The iron golem (minecraft.wiki, "Iron Golem"): a 2.7 block tall slab of a body on short legs with arms to its knees.
				FModel Golem;
				Golem.Kind = TEXT("minecraft:iron_golem");
				Golem.TextureSize = FIntPoint(128, 128);
				Golem.Textures = { TEXT("iron_golem/iron_golem.png") };
				Golem.Parts.Add(Part(ERole::Head, FVector(0, 31, 2), { Box(0, 0, -4, 2, -2.5f, 8, 10, 8), Box(24, 0, -1, 1, 5.5f, 2, 4, 2) }));
				Golem.Parts.Add(Part(ERole::Static, FVector(0, 21, 0), { Box(0, 40, -9, 0, -5, 18, 12, 11), Box(0, 70, -4.5f, -5, -3, 9, 5, 6) }));
				Golem.Parts.Add(Part(ERole::ArmRight, FVector(0, 33.5f, 0), { Box(60, 21, -13, -30, -3, 4, 30, 6) }));
				Golem.Parts.Add(Part(ERole::ArmLeft, FVector(0, 33.5f, 0), { Box(60, 58, 9, -30, -3, 4, 30, 6) }));
				Golem.Parts.Add(Part(ERole::LegRight, FVector(-4, 13, 0), { Box(37, 0, -3.5f, -13, -2, 6, 16, 5) }));
				Golem.Parts.Add(Part(ERole::LegLeft, FVector(5, 13, 0), { Box(60, 0, -3.5f, -13, -2, 6, 16, 5) }));
				// Measured: a triangle wave of 13 steps a cycle, 1.5 radians a point of walk amount, no limit below that.
				Golem.bTriangleWalk = true;
				Golem.LegSwing = 1.5f;
				Golem.ArmSwing = 1.5f;
				Models.Add(Golem.Kind, Golem);
			}
			{
				FModel Skeleton;
				Skeleton.Kind = TEXT("minecraft:skeleton");
				Skeleton.Textures = { TEXT("skeleton/skeleton.png") };
				AddPerson(Skeleton, true, false);
				AddHeldItemInHand(Skeleton, FVector(-5, 22, 0));
				Skeleton.bBowAim = true;
				Models.Add(Skeleton.Kind, Skeleton);
			}
			{
				// Husk: the zombie's body on its own picture, drawn a little bigger. Measured from one shot of a row of them (head to foot
				// height against the zombie's, 185 against 173 screen pixels: 1.07, good to about 0.03).
				FModel Husk;
				Husk.Kind = TEXT("minecraft:husk");
				Husk.TextureSize = FIntPoint(64, 64);
				Husk.Textures = { TEXT("zombie/husk.png") };
				Husk.Scale = 1.07f;
				Husk.bArmsForward = true;
				AddPerson(Husk, false, true);
				AddHeldItemInHand(Husk, FVector(-5, 22, 0));
				Models.Add(Husk.Kind, Husk);
			}
			{
				// Stray, bogged and wither skeleton: the skeleton's body on their own pictures. The stray and bogged wear a second picture over
				// it (their clothes, the body's boxes grown by a quarter pixel: not fitted); the bogged's mushrooms are not drawn. The wither
				// skeleton is drawn 1.2 times the size (height against the stray's in one row shot: 210 against 175 screen pixels, ~0.03).
				struct FSkeletonKin { const TCHAR* Name; const TCHAR* Texture; const TCHAR* Overlay; float Scale; bool bBow; };
				for (const FSkeletonKin& Each : { FSkeletonKin{ TEXT("stray"), TEXT("skeleton/stray.png"), TEXT("skeleton/stray_overlay.png"), 1.0f, true },
					FSkeletonKin{ TEXT("bogged"), TEXT("skeleton/bogged.png"), TEXT("skeleton/bogged_overlay.png"), 1.0f, true },
					FSkeletonKin{ TEXT("wither_skeleton"), TEXT("skeleton/wither_skeleton.png"), nullptr, 1.2f, false } })
				{
					FModel Kin;
					Kin.Kind = FName(*(FString(TEXT("minecraft:")) + Each.Name));
					Kin.Textures = { Each.Texture };
					Kin.Scale = Each.Scale;
					AddPerson(Kin, true, false);
					if (Each.Overlay)
					{
						Kin.Textures.Add(Each.Overlay);
						const TArray<FPart> Body = Kin.Parts;
						for (FPart Copy : Body)
						{
							for (FBox& Box : Copy.Boxes)
							{
								Box.Inflate += 0.25f;
							}
							Copy.Layer = 1;
							Kin.Parts.Add(Copy);
						}
					}
					AddHeldItemInHand(Kin, FVector(-5, 22, 0));
					Kin.bBowAim = Each.bBow;
					Models.Add(Kin.Kind, Kin);
				}
			}
			{
				// A hoglin, from the logged model (`hogmagma1.txt`, the adult; the baby's boxes are logged too but not built): 128 x 64 picture, a
				// 14 x 6 x 19 head held tilted down (0.8727) with two horns standing up on it (they are children of the head in the log, so they
				// are boxes of the head here) and two ears at their own logged slants (flattened to world position and roll), a 16 x 14 x 26
				// body with a mane fin on top and four legs, the front pair longer (14) than the hind (11). Box sizes and pivots were converted
				// from the log by script (y = 24 - y, z = -z, the parents' turns folded in); hind legs sit at -10, front at 8.5.
				{
					FModel Hoglin;
					Hoglin.Kind = TEXT("minecraft:hoglin");
					Hoglin.TextureSize = FIntPoint(128, 64);
					Hoglin.Textures = { TEXT("hoglin/hoglin.png") };
					Hoglin.Parts.Add(Part(ERole::Head, FVector(0, 22, 12), { Box(61, 1, -7, -3, 0, 14, 6, 19), Box(10, 13, -8, -2, 11, 2, 11, 2), Box(1, 13, 6, -2, 11, 2, 11, 2) }, 0.8727f));
					Hoglin.Parts.Add(Part(ERole::Static, FVector(-6, 20.99f, 15.46f), { Box(1, 1, -6, 0, -2, 6, 1, 4) }, 0.74f, -0.5148f, 0.4946f));
					Hoglin.Parts.Add(Part(ERole::Static, FVector(6, 20.99f, 15.46f), { Box(1, 6, 0, 0, -2, 6, 1, 4) }, 0.74f, 0.5148f, -0.4946f));
					Hoglin.Parts.Add(Part(ERole::Static, FVector(0, 17, 0), { Box(1, 1, -8, -7, -13, 16, 14, 26) }));
					Hoglin.Parts.Add(Part(ERole::Static, FVector(0, 31, 7), { Box(90, 33, 0, -10, -10, 0, 10, 19) }));
					Hoglin.Parts.Add(Part(ERole::LegFrontRight, FVector(-4, 14, 8.5f), { Box(66, 42, -3, -14, -3, 6, 14, 6) }));
					Hoglin.Parts.Add(Part(ERole::LegFrontLeft, FVector(4, 14, 8.5f), { Box(41, 42, -3, -14, -3, 6, 14, 6) }));
					Hoglin.Parts.Add(Part(ERole::LegBackRight, FVector(-5, 11, -10), { Box(21, 45, -2.5f, -11, -2.5f, 5, 11, 5) }));
					Hoglin.Parts.Add(Part(ERole::LegBackLeft, FVector(5, 11, -10), { Box(0, 45, -2.5f, -11, -2.5f, 5, 11, 5) }));
					Models.Add(Hoglin.Kind, Hoglin);
				}
				// The piglins: one body from the logged model (`variants3.txt`): a 10 x 8 x 8 head with a snout and two tusks, ears
				// hanging out at their logged slants, the ordinary body, arms and legs. Zombified ones hold their arms out like the
				// zombie. The jacket, sleeves and trousers layers log as the very same boxes as the body's.
				struct FPiglinKin { const TCHAR* Name; const TCHAR* Texture; bool bArmsOut; };
				for (const FPiglinKin& Each : { FPiglinKin{ TEXT("piglin"), TEXT("piglin/piglin.png"), false }, FPiglinKin{ TEXT("piglin_brute"), TEXT("piglin/piglin_brute.png"), false },
					FPiglinKin{ TEXT("zombified_piglin"), TEXT("piglin/zombified_piglin.png"), true } })
				{
					FModel Piglin;
					Piglin.Kind = FName(*(FString(TEXT("minecraft:")) + Each.Name));
					Piglin.TextureSize = FIntPoint(64, 64);
					Piglin.Textures = { Each.Texture };
					Piglin.bArmsForward = Each.bArmsOut;
					Piglin.Parts.Add(Part(ERole::Head, FVector(0, 24, 0), { Box(0, 0, -5, 0, -4, 10, 8, 8), Box(31, 1, -2, 0, 4, 4, 4, 1), Box(2, 4, 2, 0, 4, 1, 2, 1), Box(2, 0, -3, 0, 4, 1, 2, 1) }));
					Piglin.Parts.Add(Part(ERole::Static, FVector(-4.5f, 30, 0), { Box(39, 6, -1, -5, -2, 1, 5, 4) }, 0, 0, -0.6004f));
					Piglin.Parts.Add(Part(ERole::Static, FVector(4.5f, 30, 0), { Box(51, 6, 0, -5, -2, 1, 5, 4) }, 0, 0, 0.5214f));
					Piglin.Parts.Add(Part(ERole::Static, FVector(0, 24, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4) }));
					Piglin.Parts.Add(Part(ERole::ArmRight, FVector(-5, 22, 0), { Box(40, 16, -3, -10, -2, 4, 12, 4) }));
					Piglin.Parts.Add(Part(ERole::ArmLeft, FVector(5, 22, 0), { Box(32, 48, -1, -10, -2, 4, 12, 4) }));
					Piglin.Parts.Add(Part(ERole::LegRight, FVector(-1.9f, 12, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4) }));
					Piglin.Parts.Add(Part(ERole::LegLeft, FVector(1.9f, 12, 0), { Box(16, 48, -2, -12, -2, 4, 12, 4) }));
					// The jacket, sleeves and trousers over them (the piglin brute's black clothes are these; the others' are clear there),
					// grown a twentieth of a pixel so they do not fight the body under them.
					Piglin.Parts.Add(Part(ERole::Static, FVector(0, 24, 0), { Box(16, 32, -4, -12, -2, 8, 12, 4, 0.05f) }));
					Piglin.Parts.Add(Part(ERole::ArmRight, FVector(-5, 22, 0), { Box(40, 32, -3, -10, -2, 4, 12, 4, 0.05f) }));
					Piglin.Parts.Add(Part(ERole::ArmLeft, FVector(5, 22, 0), { Box(48, 48, -1, -10, -2, 4, 12, 4, 0.05f) }));
					Piglin.Parts.Add(Part(ERole::LegRight, FVector(-1.9f, 12, 0), { Box(0, 32, -2, -12, -2, 4, 12, 4, 0.05f) }));
					Piglin.Parts.Add(Part(ERole::LegLeft, FVector(1.9f, 12, 0), { Box(0, 48, -2, -12, -2, 4, 12, 4, 0.05f) }));
					AddHeldItemInHand(Piglin, FVector(-5, 22, 0));
					Models.Add(Piglin.Kind, Piglin);
				}
			}
			{
				FModel Spider;
				Spider.Kind = TEXT("minecraft:spider");
				// The second picture is the eyes, which glow in the dark.
				Spider.Textures = { TEXT("spider/spider.png"), TEXT("spider/spider_eyes.png") };
				Spider.GlowLayers = 2;
				Spider.Parts.Add(Part(ERole::Head, FVector(0, 9, 7), { Box(32, 4, -4, -4, -4, 8, 8, 8) }));
				Spider.Parts.Add(Part(ERole::Head, FVector(0, 9, 7), { Box(32, 4, -4, -4, -4, 8, 8, 8, 0.05f) }, 0, 0, 0, 0, 1));
				Spider.Parts.Add(Part(ERole::Static, FVector(0, 9, 0), { Box(0, 0, -3, -3, -3, 6, 6, 6) }));
				Spider.Parts.Add(Part(ERole::Static, FVector(0, 9, -9), { Box(0, 12, -5, -4, -6, 10, 8, 12) }));
				// Four legs a side, fanned out and sloping down.
				const float Fan[4] = { 0.7854f, 0.3927f, -0.3927f, -0.7854f };
				const float Spread[4] = { 1.0f, 0.0f, -1.0f, -2.0f };
				const float Roll[4] = { 0.7854f, 0.5812f, 0.5812f, 0.7854f };
				for (int32 Row = 0; Row < 4; ++Row)
				{
					Spider.Parts.Add(Part(ERole::SpiderLeg, FVector(-4, 9, Spread[Row]), { Box(18, 0, -16, -1, -1, 16, 2, 2) }, 0, Fan[Row], Roll[Row], Row * 2));
					Spider.Parts.Add(Part(ERole::SpiderLeg, FVector(4, 9, Spread[Row]), { Box(18, 0, 0, -1, -1, 16, 2, 2, 0.0f, true) }, 0, -Fan[Row], -Roll[Row], Row * 2 + 1));
				}
				Models.Add(Spider.Kind, Spider);
			}
			{
				// A creeper: a head, a body and four stubs of legs (minecraft.wiki, "Creeper": 0.6 by 1.7).
				FModel Creeper;
				Creeper.Kind = TEXT("minecraft:creeper");
				Creeper.Textures = { TEXT("creeper/creeper.png") };
				Creeper.Parts.Add(Part(ERole::Head, FVector(0, 18, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8) }));
				Creeper.Parts.Add(Part(ERole::Static, FVector(0, 18, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4) }));
				const FBox Leg = Box(0, 16, -2, -6, -2, 4, 6, 4);
				Creeper.Parts.Add(Part(ERole::LegBackRight, FVector(-2, 6, -4), { Leg }));
				Creeper.Parts.Add(Part(ERole::LegBackLeft, FVector(2, 6, -4), { Leg }));
				Creeper.Parts.Add(Part(ERole::LegFrontRight, FVector(-2, 6, 4), { Leg }));
				Creeper.Parts.Add(Part(ERole::LegFrontLeft, FVector(2, 6, 4), { Leg }));
				Models.Add(Creeper.Kind, Creeper);
			}
			{
				// An enderman: a person with a thin body and arms and legs 30 pixels long (0.6 by 2.9). The second picture
				// is the eyes.
				FModel Enderman;
				Enderman.Kind = TEXT("minecraft:enderman");
				Enderman.Textures = { TEXT("enderman/enderman.png"), TEXT("enderman/enderman_eyes.png") };
				Enderman.GlowLayers = 2;
				Enderman.MaxLimbSwing = 0.4f;
				Enderman.LegSwing = 0.7f;
				Enderman.ArmSwing = 0.56f;
				Enderman.Parts.Add(Part(ERole::Head, FVector(0, 37, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8) }));
				Enderman.Parts.Add(Part(ERole::Head, FVector(0, 37, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8, 0.05f) }, 0, 0, 0, 0, 1));
				Enderman.Parts.Add(Part(ERole::Static, FVector(0, 38, 0), { Box(32, 16, -4, -12, -2, 8, 12, 4) }));
				Enderman.Parts.Add(Part(ERole::ArmRight, FVector(-5, 36, 0), { Box(56, 0, -1, -28, -1, 2, 30, 2) }));
				Enderman.Parts.Add(Part(ERole::ArmLeft, FVector(5, 36, 0), { Box(56, 0, -1, -28, -1, 2, 30, 2, 0.0f, true) }));
				Enderman.Parts.Add(Part(ERole::LegRight, FVector(-2, 29, 0), { Box(56, 0, -1, -30, -1, 2, 30, 2) }));
				Enderman.Parts.Add(Part(ERole::LegLeft, FVector(2, 29, 0), { Box(56, 0, -1, -30, -1, 2, 30, 2, 0.0f, true) }));
				Models.Add(Enderman.Kind, Enderman);
			}
			{
				// A slime of size 1 (0.52 across): a solid core with eyes and a mouth, in a see-through shell. Bigger sizes
				// are the same model grown with the hitbox.
				FModel Slime;
				Slime.Kind = TEXT("minecraft:slime");
				Slime.Textures = { TEXT("slime/slime.png"), TEXT("slime/slime.png") };
				Slime.TranslucentLayers = 2;
				Slime.BaseWidth = 0.52f;
				Slime.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 16, -3, 1, -3, 6, 6, 6), Box(32, 0, -3.25f, 4, 1.5f, 2, 2, 2), Box(32, 4, 1.25f, 4, 1.5f, 2, 2, 2), Box(32, 8, 0, 2, 2.5f, 1, 1, 1) }));
				Slime.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -4, 0, -4, 8, 8, 8) }, 0, 0, 0, 0, 1));
				Models.Add(Slime.Kind, Slime);
			}
			{
				// A minecart: a floor and four walls, the long way along the rail (z). The ones with a load have a block
				// standing in it at three quarters size: a chest (the entity's own picture), or a furnace, hopper or TNT from
				// the block's own model and pictures.
				const struct { const TCHAR* Kind; const TCHAR* Block; } Carts[] = {
					{ TEXT("minecraft:minecart"), nullptr }, { TEXT("minecraft:chest_minecart"), nullptr },
					{ TEXT("minecraft:furnace_minecart"), TEXT("minecraft:furnace[facing=south,lit=false]") },
					{ TEXT("minecraft:hopper_minecart"), TEXT("minecraft:hopper[enabled=true,facing=down]") },
					{ TEXT("minecraft:tnt_minecart"), TEXT("minecraft:tnt") } };
				for (const auto& Variant : Carts)
				{
					FModel Cart;
					Cart.Kind = Variant.Kind;
					Cart.TextureSize = FIntPoint(64, 32);
					Cart.Textures = { TEXT("minecart/minecart.png") };
					Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, 0), { Box(0, 10, -10, -8, -1, 20, 16, 2) }, Flat, HALF_PI));
					Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, 9), { Box(0, 0, -8, 0, -1, 16, 8, 2) }));
					Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, -9), { Box(0, 0, -8, 0, -1, 16, 8, 2) }));
					Cart.Parts.Add(Part(ERole::Static, FVector(7, 3, 0), { Box(0, 0, -8, 0, -1, 16, 8, 2) }, 0, HALF_PI));
					Cart.Parts.Add(Part(ERole::Static, FVector(-7, 3, 0), { Box(0, 0, -8, 0, -1, 16, 8, 2) }, 0, HALF_PI));
					if (Variant.Block)
					{
						Cart.Block = Variant.Block;
						// Seen from the south with the cart at rest (vehicle_look.txt, ref_side_*): the furnace's and TNT's tops
						// stand 36 px over the rim where the cart is 140 px long (7 px a unit), the hopper's 10 px: the hopper sits
						// 3.75 (5 pixels of the block's 16, at three quarters) lower than the others.
						Cart.BlockOffset = FVector(0, Variant.Block == FString(TEXT("minecraft:hopper[enabled=true,facing=down]")) ? 6.375f : 10.125f, 0);
					}
					else if (Cart.Kind == TEXT("minecraft:chest_minecart"))
					{
						// A chest has no block model (it is drawn from its own picture), so it is boxes on a second layer.
						Cart.Textures.Add(TEXT("chest/normal.png"));
						Cart.LayerTextureSizes = { FIntPoint(0, 0), FIntPoint(64, 64) };
						// Its top stands 20 px higher over the rim than the blocks' had at first (ref_side_chest, 7 px a unit): 2.875 up.
						AddChest(Cart, FVector(0, 5.875f, 0), 0.75f, 1, 0);
					}
					Models.Add(Cart.Kind, Cart);
				}
			}
			{
				// A boat: a hull floor and four sides and two paddles. The wood is chosen by the texture.
				// A chest boat has the same hull with a chest on its floor, cut from a taller texture.
				for (const bool bChest : { false, true })
				{
					FModel Boat;
					Boat.Kind = bChest ? TEXT("minecraft:chest_boat") : TEXT("minecraft:boat");
					Boat.TextureSize = FIntPoint(128, bChest ? 128 : 64);
					Boat.Textures = { bChest ? TEXT("chest_boat/oak.png") : TEXT("boat/oak.png") };
					Boat.Parts.Add(Part(ERole::Static, FVector(0, 2, 0), { Box(0, 0, -14, -8, -3, 28, 16, 3) }, Flat, HALF_PI));
					Boat.Parts.Add(Part(ERole::Static, FVector(0, 3, 15), { Box(0, 27, -8, 0, -1, 16, 6, 2) }));
					Boat.Parts.Add(Part(ERole::Static, FVector(0, 3, -15), { Box(0, 19, -9, 0, -1, 18, 6, 2) }));
					Boat.Parts.Add(Part(ERole::Static, FVector(9, 3, 0), { Box(0, 35, -14, 0, -1, 28, 6, 2) }, 0, HALF_PI));
					Boat.Parts.Add(Part(ERole::Static, FVector(-9, 3, 0), { Box(0, 43, -14, 0, -1, 28, 6, 2) }, 0, HALF_PI));
					AddPaddles(Boat, 11.0f, 9.0f, 3.0f, FIntPoint(62, 0), FIntPoint(62, 20));
					// The space inside the sides, up to their tops: water is not drawn there.
					Boat.MaskLayer = 1;
					Boat.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -8, 3, -14, 16, 6, 28) }, 0, 0, 0, 0, 1));
					if (bChest)
					{
						// The chest sits towards the stern, its lock facing the bow.
						Boat.Parts.Add(Part(ERole::Static, FVector(0, 5, -4), { Box(0, 76, -6, 0, -6, 12, 8, 12), Box(0, 59, -6, 8, -6, 12, 4, 12), Box(0, 59, -1, 6, 6, 2, 4, 1) }));
					}
					Models.Add(Boat.Kind, Boat);
				}
				// The bamboo raft is a flat deck (28 by 20, 4 thick) over a narrower one (28 by 16, 4 thick), both cut from the
				// picture's corner, with the chest on it for the chest raft (vehicle_log3.txt's log model: bottom at (0, -2.1, 1)
				// turned a quarter about x, boxes y -11 to 9, z -4 to 0 and y -9 to 7, z -8 to -4, both from u 0, v 0).
				for (const bool bChest : { false, true })
				{
					FModel Raft;
					Raft.Kind = bChest ? TEXT("minecraft:bamboo_chest_raft") : TEXT("minecraft:bamboo_raft");
					Raft.TextureSize = FIntPoint(128, bChest ? 128 : 64);
					Raft.Textures = { bChest ? TEXT("chest_boat/bamboo.png") : TEXT("boat/bamboo.png") };
					// It floats higher than a hull, its deck clear of the water.
					Raft.Parts.Add(Part(ERole::Static, FVector(0, 8.1f, 0), { Box(0, 0, -14, -10, -4, 28, 20, 4), Box(0, 0, -14, -8, -8, 28, 16, 4) }, Flat, HALF_PI));
					AddPaddles(Raft, 10.0f, 9.0f, 3.0f, FIntPoint(0, 24), FIntPoint(40, 24));
					if (bChest)
					{
						Raft.Parts.Add(Part(ERole::Static, FVector(0, 11, -4), { Box(0, 76, -6, 0, -6, 12, 8, 12), Box(0, 59, -6, 8, -6, 12, 4, 12), Box(0, 59, -1, 6, 6, 2, 4, 1) }));
					}
					Models.Add(Raft.Kind, Raft);
				}
			}
			{
				// Placed chests (McChestComponent), one model for each picture: "mc:chest/normal", "mc:chest/normal_left"...
				// A "_left" picture is the half whose other half is on its +x side, a "_right" one the half on the -x side.
				for (const TCHAR* Picture : { TEXT("normal"), TEXT("trapped"), TEXT("christmas"), TEXT("ender"), TEXT("copper"), TEXT("copper_exposed"),
					TEXT("copper_weathered"), TEXT("copper_oxidized") })
				{
					for (const int32 Seam : { 0, 1, -1 })
					{
						// An ender chest doesn't pair.
						if (Seam != 0 && FCString::Strcmp(Picture, TEXT("ender")) == 0)
						{
							continue;
						}
						const FString Name = FString(Picture) + (Seam > 0 ? TEXT("_left") : Seam < 0 ? TEXT("_right") : TEXT(""));
						FModel Chest;
						Chest.Kind = FName(TEXT("mc:chest/") + Name);
						Chest.TextureSize = FIntPoint(64, 64);
						Chest.Textures = { TEXT("chest/") + Name + TEXT(".png") };
						AddChest(Chest, FVector::ZeroVector, 1.0f, 0, Seam);
						Models.Add(Chest.Kind, Chest);
					}
				}
			}
			{
				// Placed shulker boxes (McChestComponent), one model for each colour: "mc:shulker_box/shulker",
				// "mc:shulker_box/shulker_red"... (minecraft.wiki, "Shulker Box"). A base 8 high and a lid 12 high over it,
				// whole blocks across, the lid's rim over the base's top; the origin is the block's middle, so the box can be
				// turned to any face. The lid rises and turns as the box opens.
				const TCHAR* const Colours[] = { TEXT(""), TEXT("_white"), TEXT("_orange"), TEXT("_magenta"), TEXT("_light_blue"), TEXT("_yellow"),
					TEXT("_lime"), TEXT("_pink"), TEXT("_gray"), TEXT("_light_gray"), TEXT("_cyan"), TEXT("_purple"), TEXT("_blue"), TEXT("_brown"),
					TEXT("_green"), TEXT("_red"), TEXT("_black") };
				for (const TCHAR* Colour : Colours)
				{
					const FString Picture = FString(TEXT("shulker")) + Colour;
					FModel Shulker;
					Shulker.Kind = FName(TEXT("mc:shulker_box/") + Picture);
					Shulker.TextureSize = FIntPoint(64, 64);
					Shulker.Textures = { TEXT("shulker/") + Picture + TEXT(".png") };
					Shulker.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 28, -8, -8, -8, 16, 8, 16) }));
					Shulker.Parts.Add(Part(ERole::ShulkerLid, FVector::ZeroVector, { Box(0, 0, -8, -4, -8, 16, 12, 16, 0.01f) }));
					Models.Add(Shulker.Kind, Shulker);
				}
			}
			{
				// A placed bell's body (McChestComponent; the bar, posts and chain are its block model): a cup 6 across and 7
				// high with a rim 8 across and 2 high under it, hanging from a pivot at the top of the cup, 12 pixels up.
				FModel Bell;
				Bell.Kind = FName(TEXT("mc:bell"));
				Bell.TextureSize = FIntPoint(32, 32);
				Bell.Textures = { TEXT("bell/bell_body.png") };
				Bell.Parts.Add(Part(ERole::BellBody, FVector(0, 12, 0), { Box(0, 0, -3, -6, -3, 6, 7, 6), Box(0, 13, -4, -8, -4, 8, 2, 8) }));
				Models.Add(Bell.Kind, Bell);
			}
			{
				// The book on a lectern and over an enchanting table (McChestComponent), cut as its picture is laid out: two
				// covers 6 by 10 and the spine 2 by 10 between them, a block of pages 5 by 8 by 1 in each half, and a loose
				// page 5 by 8. The spine runs up the model's y at its origin; opened flat the covers lie across x with the
				// pages on their +z side, and shut they fold up to +z about the spine's edges.
				FModel Book;
				Book.Kind = FName(TEXT("mc:book"));
				Book.TextureSize = FIntPoint(64, 32);
				Book.Textures = { TEXT("enchantment/enchanting_table_book.png") };
				Book.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(12, 0, -1, -5, 0, 2, 10, 0) }));
				Book.Parts.Add(Part(ERole::BookLeft, FVector(-1, 0, 0), { Box(0, 0, -6, -5, 0, 6, 10, 0), Box(0, 10, -5, -4, 0.01f, 5, 8, 1) }));
				Book.Parts.Add(Part(ERole::BookRight, FVector(1, 0, 0), { Box(16, 0, 0, -5, 0, 6, 10, 0), Box(12, 10, 0, -4, 0.01f, 5, 8, 1) }));
				Book.Parts.Add(Part(ERole::BookPage, FVector(0, 0, 1), { Box(24, 10, 0, -4, 0.02f, 5, 8, 0) }, 0, 0, 0, 0));
				Book.Parts.Add(Part(ERole::BookPage, FVector(0, 0, 1), { Box(24, 10, 0, -4, 0.03f, 5, 8, 0) }, 0, 0, 0, 1));
				Models.Add(Book.Kind, Book);
			}
			{
				// Placed mob heads (McChestComponent): "mc:head/zombie"... An 8 cube from the mob's own picture standing on the
				// model's origin, facing south, with the hat layer a little bigger over it where the picture has one. The
				// piglin's is wider (10), with its snout, tusks and floppy ears, as its picture lays them out.
				struct FHeadKind { const TCHAR* Name; const TCHAR* Texture; int32 Height; bool bHat; };
				const FHeadKind Kinds[] = {
					{ TEXT("skeleton"), TEXT("skeleton/skeleton.png"), 32, false },
					{ TEXT("wither_skeleton"), TEXT("skeleton/wither_skeleton.png"), 32, false },
					{ TEXT("zombie"), TEXT("zombie/zombie.png"), 64, true },
					{ TEXT("creeper"), TEXT("creeper/creeper.png"), 32, false },
					{ TEXT("player"), TEXT("player/wide/steve.png"), 64, true },
				};
				for (const FHeadKind& Kind : Kinds)
				{
					FModel Head;
					Head.Kind = FName(FString(TEXT("mc:head/")) + Kind.Name);
					Head.TextureSize = FIntPoint(64, Kind.Height);
					Head.Textures = { Kind.Texture };
					Head.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -4, 0, -4, 8, 8, 8) }));
					if (Kind.bHat)
					{
						Head.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(32, 0, -4, 0, -4, 8, 8, 8, 0.25f) }));
					}
					Models.Add(Head.Kind, Head);
				}
				FModel Piglin;
				Piglin.Kind = FName(TEXT("mc:head/piglin"));
				Piglin.TextureSize = FIntPoint(64, 64);
				Piglin.Textures = { TEXT("piglin/piglin.png") };
				Piglin.Parts.Add(Part(ERole::Static, FVector::ZeroVector, {
					Box(0, 0, -5, 0, -4, 10, 8, 8), Box(31, 1, -2, 0, 4, 4, 4, 1), Box(2, 0, 2, 0, 4, 1, 2, 1), Box(2, 4, -3, 0, 4, 1, 2, 1) }));
				// The ears hang from the top edge of each side, leaning out 0.9 radians. Fitted to vanilla's placed heads
				// (ref_scene_heads) and hotbar icon (ref_heads_hotbar) by sweeping leans 0.5 to 1.3 and hinge heights 6 to 8;
				// -McPiglinEar=<radians>[,<hinge>] tries others.
				float EarLean = 0.9f, EarHinge = 8.0f;
				FString EarArg;
				if (FParse::Value(FCommandLine::Get(), TEXT("-McPiglinEar="), EarArg))
				{
					FString Lean, Hinge;
					EarLean = FCString::Atof(*(EarArg.Split(TEXT(","), &Lean, &Hinge) ? Lean : EarArg));
					EarHinge = Hinge.IsEmpty() ? EarHinge : FCString::Atof(*Hinge);
				}
				Piglin.Parts.Add(Part(ERole::Static, FVector(4.5f, EarHinge, 0), { Box(51, 6, 0, -5, -2, 1, 5, 4) }, 0, 0, EarLean));
				Piglin.Parts.Add(Part(ERole::Static, FVector(-4.5f, EarHinge, 0), { Box(39, 6, -1, -5, -2, 1, 5, 4) }, 0, 0, -EarLean));
				Models.Add(Piglin.Kind, Piglin);
				// The dragon's, cut from its 256 picture and drawn at three quarters: a 16 cube with two scales on top, and
				// in front an upper jaw (12 by 5 by 16, two nostrils on it) over a lower one (12 by 4 by 16).
				FModel Dragon;
				Dragon.Kind = FName(TEXT("mc:head/dragon"));
				Dragon.TextureSize = FIntPoint(256, 256);
				Dragon.Textures = { TEXT("enderdragon/dragon.png") };
				FPart DragonHead = Part(ERole::Static, FVector::ZeroVector, {
					Box(112, 30, -8, 0, -8, 16, 16, 16),
					Box(0, 0, -5, 16, -4, 2, 4, 6), Box(0, 0, 3, 16, -4, 2, 4, 6, 0.0f, true),
					Box(176, 44, -6, 4, 8, 12, 5, 16),
					Box(112, 0, -5, 9, 18, 2, 2, 4), Box(112, 0, 3, 9, 18, 2, 2, 4, 0.0f, true) });
				DragonHead.Scale = 0.75f;
				Dragon.Parts.Add(DragonHead);
				// The lower jaw hangs open from its top back edge, 0.2 radians (11.4 degrees measured side on in vanilla,
				// ref_jaw_wall, for an unpowered head).
				FPart DragonJaw = Part(ERole::Static, FVector(0, 4, 8) * 0.75f, { Box(176, 65, -6, -4, 0, 12, 4, 16) }, 0.2f);
				DragonJaw.Scale = 0.75f;
				Dragon.Parts.Add(DragonJaw);
				Models.Add(Dragon.Kind, Dragon);
				// The whole dragon, APPROXIMATE: its proportions are read off a vanilla frozen dragon seen from above and the side
				// (dragon_shot1, ref_dr_top and ref_dr_side: about 13.5 blocks nose to tail, wings about 14 across) and its
				// parts are the familiar ones (a body, five neck pieces, the head above, eight tail pieces, two wings with a
				// membrane). The texture cuts (UVs) of everything but the head are guesses, the wings do not beat, and neck and
				// tail do not follow the dragon's turns. The front is -z, as for the other mobs.
				{
					FModel Full;
					Full.Kind = FName(TEXT("minecraft:ender_dragon"));
					Full.TextureSize = FIntPoint(256, 256);
					Full.Textures = { TEXT("enderdragon/dragon.png") };
					Full.Parts.Add(Part(ERole::Static, FVector(0, 12, 0), { Box(0, 0, -12, -12, -20, 24, 24, 64) }));
					for (int32 Piece = 0; Piece < 5; ++Piece)
					{
						Full.Parts.Add(Part(ERole::Static, FVector(0, 12 - Piece, -20 - 10 * Piece), { Box(192, 104, -5, -5, -10, 10, 10, 10) }, -0.04f * Piece));
					}
					for (int32 Piece = 0; Piece < 8; ++Piece)
					{
						Full.Parts.Add(Part(ERole::Static, FVector(0, 12 + Piece, 44 + 10 * Piece), { Box(192, 104, -5, -5, 0, 10, 10, 10) }, 0.03f * Piece));
					}
					for (const FPart& Source : Dragon.Parts)
					{
						FPart Moved = Source;
						Moved.Pivot += FVector(0, 7, -86);
						Moved.RestYaw = PI;
						Full.Parts.Add(Moved);
					}
					// Wings: a bone 96 long, a membrane behind it; raised a little (0.25) from level.
					Full.Parts.Add(Part(ERole::Static, FVector(-12, 2, -4), { Box(0, 88, -96, -4, -4, 96, 8, 8), Box(0, 152, -96, 0, 4, 96, 0, 56) }, 0.0f, 0.0f, 0.25f));
					Full.Parts.Add(Part(ERole::Static, FVector(12, 2, -4), { Box(0, 88, 0, -4, -4, 96, 8, 8, 0.0f, true), Box(0, 152, 0, 0, 4, 96, 0, 56, 0.0f, true) }, 0.0f, 0.0f, -0.25f));
					Models.Add(Full.Kind, Full);
				}
				// A carved pumpkin worn on a head (McPlayerModel): its block model 10.1 pixels across (the block at 0.63),
				// its middle on the head's, 4 over the neck, its face forward (measured in vanilla, ref_wh_pumpkin against
				// ref_wh_bare, from the front: 82 pixels to the bare head's 64, its top 1 pixel over the head's).
				FModel Pumpkin;
				Pumpkin.Kind = FName(TEXT("mc:worn/carved_pumpkin"));
				Pumpkin.Block = TEXT("minecraft:carved_pumpkin[facing=south]");
				Pumpkin.BlockScale = 0.63f;
				Pumpkin.BlockOffset = FVector(0, 4, 0);
				Models.Add(Pumpkin.Kind, Pumpkin);
			}
			{
				// A decorated pot (McChestComponent), 14 across and 16 high with a neck 8 across and 4 high on top. The base
				// picture has the neck's net at its top left and the body's top and bottom side by side under it; each side
				// is the middle 14 columns of a side picture, the plain one or its sherd's pattern (the component's item):
				// facing south, the front south, then the right east, the back north and the left west (vanilla's
				// heads_pots.txt, ref_pots_front: the blade front faces the camera, the arms up right on its east).
				FModel Pot;
				Pot.Kind = FName(TEXT("mc:decorated_pot"));
				Pot.TextureSize = FIntPoint(32, 32);
				Pot.Textures = { TEXT("decorated_pot/decorated_pot_base.png") };
				Pot.LayerTextureSizes = { FIntPoint(32, 32) };
				for (int32 Side = 0; Side < 4; ++Side)
				{
					Pot.Textures.Add(TEXT("decorated_pot/decorated_pot_side.png"));
					Pot.LayerTextureSizes.Add(FIntPoint(16, 16));
				}
				Pot.Parts.Add(Part(ERole::Static, FVector::ZeroVector, {
					Box(0, 0, -4, 16, -4, 8, 4, 8), Box(-14, 13, -7, 16, -7, 14, 0, 14), Box(-14, 13, -7, 0, -7, 14, 0, 14) }));
				for (int32 Side = 0; Side < 4; ++Side)
				{
					Pot.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(1, 0, -7, 0, 7, 14, 16, 0) }, 0, Side * HALF_PI, 0, 0, 1 + Side));
				}
				Models.Add(Pot.Kind, Pot);
			}
			{
				// A conduit (McChestComponent) about the block's middle: idle, its 6 cube shell; active, the 8 cube cage
				// turning, with the eye inside looking at the player.
				FModel Shell;
				Shell.Kind = FName(TEXT("mc:conduit/shell"));
				Shell.TextureSize = FIntPoint(32, 16);
				Shell.Textures = { TEXT("conduit/base.png") };
				Shell.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -3, -3, -3, 6, 6, 6) }));
				Models.Add(Shell.Kind, Shell);
				FModel Cage;
				Cage.Kind = FName(TEXT("mc:conduit/cage"));
				Cage.TextureSize = FIntPoint(32, 16);
				// The eye is shut while the conduit is awake, and opens when it hunts, with the whole frame (vanilla's
				// hunting flag comes on with the 42nd frame block, conduit_log; minecraft.wiki: "an orange iris").
				Cage.Textures = { TEXT("conduit/cage.png"), TEXT("conduit/closed_eye.png"), TEXT("conduit/wind.png"), TEXT("conduit/wind_vertical.png") };
				Cage.LayerTextureSizes = { FIntPoint(32, 16), FIntPoint(16, 16), FIntPoint(64, 704), FIntPoint(64, 704) };
				Cage.Parts.Add(Part(ERole::ConduitCage, FVector::ZeroVector, { Box(0, 0, -4, -4, -4, 8, 8, 8) }));
				// The eye's picture is 8 pixels, drawn at 0.6 (about 37 pixels across in ref_scene_conduits and
				// ref_conduits_b/c, against 0.5 and 0.6 on -McScene=conduits). The wind is 1.25 times a block: the ring in
				// ref_conduits_c is 1.33 times the block wide, and its two streaks, 3 texels apart, say 1.2.
				// (-McConduitLook=<eye scale>,<wind scale> tries others.)
				float EyeScale = 0.6f, WindScale = 1.25f;
				FString Look;
				if (FParse::Value(FCommandLine::Get(), TEXT("-McConduitLook="), Look, false))
				{
					TArray<FString> Parts;
					Look.ParseIntoArray(Parts, TEXT(","));
					EyeScale = Parts.IsValidIndex(0) ? FCString::Atof(*Parts[0]) : EyeScale;
					WindScale = Parts.IsValidIndex(1) ? FCString::Atof(*Parts[1]) : WindScale;
				}
				FPart Eye = Part(ERole::ConduitEye, FVector::ZeroVector, { Box(0, 0, -4, -4, 0, 8, 8, 0) }, 0, 0, 0, 0, 1);
				Eye.Scale = EyeScale;
				Cage.Parts.Add(Eye);
				// The streaks round it, a 16 cube (the picture's 64 by 32 frames), seen from both sides and unshaded: in
				// ref_conduits_c the ring's far sides show, and every streak is the one colour, (103,153,191) under water.
				FPart Wind = Part(ERole::ConduitWind, FVector::ZeroVector, { Box(0, 0, -8, -8, -8, 16, 16, 16) }, 0, 0, 0, 0, 2);
				Cage.GlowLayers |= 1u << 2;
				Wind.Scale = WindScale;
				Cage.Parts.Add(Wind);
				// Every other run of the 22 frames it is wind_vertical.png on the box stood on end, its streaks upright
				// (conduit_frames.txt: rings in ref_cf_07 to 23 and 50 to 59, upright frames in ref_cf_33 to 45).
				FPart WindVertical = Part(ERole::ConduitWind, FVector::ZeroVector, { Box(0, 0, -8, -8, -8, 16, 16, 16) }, HALF_PI, 0, 0, 0, 3);
				WindVertical.Scale = WindScale;
				Cage.Parts.Add(WindVertical);
				Cage.GlowLayers |= 1u << 3;
				Models.Add(Cage.Kind, Cage);
				FModel Hunting = Cage;
				Hunting.Kind = FName(TEXT("mc:conduit/hunting"));
				Hunting.Textures[1] = TEXT("conduit/open_eye.png");
				Models.Add(Hunting.Kind, Hunting);
			}
			{
				// A blaze, from the logged model (`stridermodel1.txt`): an 8 x 8 x 8 head and twelve 2 x 8 x 2 rods, each at the place the log has
				// it for one tick (they circle the body, in three rings of four at different heights; the circling and bobbing are not
				// built, so the rods stand still), on a 64 x 32 picture.
				FModel Blaze;
				Blaze.Kind = TEXT("minecraft:blaze");
				Blaze.Textures = { TEXT("blaze/blaze.png") };
				Blaze.Parts.Add(Part(ERole::Head, FVector(0, 24, 0), { Box(0, 0, -4, -4, -4, 8, 8, 8) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(-7.412f, 26.99f, -5.105f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(-5.105f, 26.94f, 7.412f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(7.412f, 26.67f, 5.105f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(5.106f, 26.23f, -7.412f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(-4.928f, 13.07f, 0.844f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(-2.321f, 21.74f, -6.604f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(-0.844f, 12.37f, -4.928f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(-6.604f, 21.31f, 2.321f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(2.321f, 21.05f, 6.604f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(6.604f, 21.02f, -2.321f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(0.844f, 12.18f, 4.928f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Blaze.Parts.Add(Part(ERole::Static, FVector(4.928f, 12.01f, -0.844f), { Box(0, 16, 0, -8, -2, 2, 8, 2) }));
				Models.Add(Blaze.Kind, Blaze);
			}
			{
				// A phantom, from the logged model (`phantom1.txt`, one frame): body, head, two wing pairs and a two-part tail on a 64 x 64 picture, all at their rest tilts. The wing flap and the tail sway are not fitted. Lowered 21 (1.3125 blocks) from the log's pivots to sit in the hitbox: the reference shows a phantom on the ground with its wings flat on it (pairshots1.txt); the exact lift is not measured.
				FModel Phantom;
				Phantom.Kind = TEXT("minecraft:phantom");
				Phantom.TextureSize = FIntPoint(64, 64);
				Phantom.Textures = { TEXT("phantom/phantom.png") };
				Phantom.Parts.Add(Part(ERole::Static, FVector(2.0f, 5.0f, 8.0f), { Box(23, 12, 0.0f, -2.0f, -9.0f, 6, 2, 9) }, 0.0f, 0, -0.1f));
				Phantom.Parts.Add(Part(ERole::Static, FVector(7.97f, 4.4f, 8.0f), { Box(16, 24, 0.0f, -1.0f, -9.0f, 13, 1, 9) }, 0.0f, 0, -0.2f));
				Phantom.Parts.Add(Part(ERole::Static, FVector(-3.0f, 5.0f, 8.0f), { Box(23, 12, -6.0f, -2.0f, -9.0f, 6, 2, 9) }, 0.0f, 0, 0.1f));
				Phantom.Parts.Add(Part(ERole::Static, FVector(-8.97f, 4.4f, 8.0f), { Box(16, 24, -13.0f, -1.0f, -9.0f, 13, 1, 9) }, 0.0f, 0, 0.2f));
				Phantom.Parts.Add(Part(ERole::Static, FVector(0.0f, 5.0f, -1.0f), { Box(3, 20, -2.0f, -2.0f, -6.0f, 3, 2, 6) }, 0.0f, 0, 0.0f));
				Phantom.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.5f, -7.0f), { Box(4, 29, -1.0f, -1.0f, -6.0f, 1, 1, 6) }, 0.0f, 0, 0.0f));
				Phantom.Parts.Add(Part(ERole::Static, FVector(0.0f, 3.0f, 0), { Box(0, 8, -3.0f, -1.0f, -1.0f, 5, 3, 9) }, -0.1f, 0, 0.0f));
				Phantom.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.7f, 7.065f), { Box(0, 0, -4.0f, -1.0f, 0, 7, 3, 5) }, 0.1f, 0, 0.0f));
				Models.Add(Phantom.Kind, Phantom);
			}
			{
				// A guardian, from the logged model (`phantom1.txt`, one frame): the 12 x 12 x 16 head with its side and top plates, twelve spikes at rest, the eye and a three-part tail, on a 64 x 64 picture. Spike extension, the eye's look and the tail swish are not fitted.
				FModel Guardian;
				Guardian.Kind = TEXT("minecraft:guardian");
				Guardian.TextureSize = FIntPoint(64, 64);
				Guardian.Textures = { TEXT("guardian/guardian.png") };
				Guardian.Parts.Add(Part(ERole::Static, FVector(0.0f, 24.0f, 0), { Box(0, 0, -6.0f, -22.0f, -8.0f, 12, 12, 16), Box(0, 28, -8.0f, -22.0f, -6.0f, 2, 12, 12), Box(0, 28, 6.0f, -22.0f, -6.0f, 2, 12, 12), Box(16, 40, -6.0f, -10.0f, -6.0f, 12, 2, 12), Box(16, 40, -6.0f, -24.0f, -6.0f, 12, 2, 12) }, 0.0f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(7.933f, 0.0671f, 0), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, 0.0f, 0, -2.356f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(-8.0f, -0.0004f, 0), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, 0.0f, 0, 2.356f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(8.077f, 8.0f, -8.077f), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, -1.571f, 0.7854f, 3.142f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(-8.06f, 8.0f, -8.06f), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, -1.571f, -0.7854f, 3.142f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(0.0f, 0.0116f, -7.988f), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, -2.356f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(0.0f, 0.0729f, 7.927f), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, 2.356f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(0.0f, 24.0f, 8.25f), { Box(8, 0, -1.0f, -17.0f, -1.0f, 2, 2, 1) }, 0.0f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(0.0f, 24.0f, 0), { Box(40, 0, -2.0f, -18.0f, -15.0f, 4, 4, 8) }, 0.0f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(-1.5f, 23.5f, -14.0f), { Box(0, 54, 0.0f, -17.0f, -7.0f, 3, 3, 7) }, 0.0f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(-1.0f, 23.0f, -20.0f), { Box(41, 32, 0.0f, -16.0f, -6.0f, 2, 2, 6), Box(25, 19, 1.0f, -19.5f, -12.0f, 1, 9, 9) }, 0.0f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(0.0f, 16.08f, -8.08f), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, -0.7854f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(0.0f, 16.04f, 8.043f), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, 0.7854f, 0, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(7.967f, 15.97f, 0), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, 0.0f, 0, -0.7854f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(-7.921f, 15.92f, 0), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, 0.0f, 0, 0.7854f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(-7.948f, 8.0f, 7.948f), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, 1.571f, -0.7854f, 0.0f));
				Guardian.Parts.Add(Part(ERole::Static, FVector(8.023f, 8.0f, 8.023f), { Box(0, 0, -1.0f, -4.5f, -1.0f, 2, 9, 2) }, 1.571f, 0.7854f, 0.0f));
				Models.Add(Guardian.Kind, Guardian);
			}
			{
				// An endermite, from the logged model (`endmobs1.txt`): four stacked segments on a 64 x 32 picture, front to back
				// 4 x 3 x 2, 6 x 4 x 5, 3 x 3 x 1 and 1 x 2 x 1. The wriggle (each segment swings sideways) is not fitted.
				FModel Endermite;
				Endermite.Kind = TEXT("minecraft:endermite");
				Endermite.TextureSize = FIntPoint(64, 32);
				Endermite.Textures = { TEXT("endermite/endermite.png") };
				Endermite.Parts.Add(Part(ERole::Static, FVector(0, 3, 3.5f), { Box(0, 0, -2, -3, -1, 4, 3, 2) }));
				Endermite.Parts.Add(Part(ERole::Static, FVector(0, 4, 0), { Box(0, 5, -3, -4, -2.5f, 6, 4, 5) }));
				Endermite.Parts.Add(Part(ERole::Static, FVector(0, 3, -3), { Box(0, 14, -1.5f, -3, -0.5f, 3, 3, 1) }));
				Endermite.Parts.Add(Part(ERole::Static, FVector(0, 2, -4), { Box(0, 18, -0.5f, -2, -0.5f, 1, 2, 1) }));
				Models.Add(Endermite.Kind, Endermite);
			}
			{
				// A shulker, from the logged model (`endmobs1.txt`, closed): a 16 x 8 x 16 base, a 16 x 12 x 16 lid overlapping it, and
				// a 6 cube head standing 12 up, on a 64 x 64 picture. The lid's opening and the head's turn are not built.
				FModel Shulker;
				Shulker.Kind = TEXT("minecraft:shulker");
				Shulker.TextureSize = FIntPoint(64, 64);
				Shulker.Textures = { TEXT("shulker/shulker.png") };
				Shulker.Parts.Add(Part(ERole::Static, FVector(0, 0, 0), { Box(0, 28, -8, 0, -8, 16, 8, 16) }));
				// The lid is a hair bigger than the game's, so its notched lower edge shows the base through the gaps instead of fighting it for the depth.
				Shulker.Parts.Add(Part(ERole::Static, FVector(0, 0, 0), { Box(0, 0, -8, 4, -8, 16, 12, 16, 0.02f) }));
				Shulker.Parts.Add(Part(ERole::Static, FVector(0, 12, 0), { Box(0, 52, -3, -6, -3, 6, 6, 6) }));
				Models.Add(Shulker.Kind, Shulker);
			}
			{
				// A strider, from the logged model (`stridermodel1.txt`; the adult): a 16 x 14 x 16 body 1 above the middle, two 4 x 16 x 4 legs,
				// and three bristles each side (flat 12 x 16 fins, children of the body, at their rest rolls). 64 x 128 picture. The legs' swing
				// and the bristles' sway are not fitted.
				FModel Strider;
				Strider.Kind = TEXT("minecraft:strider");
				Strider.TextureSize = FIntPoint(64, 128);
				Strider.Textures = { TEXT("strider/strider.png") };
				Strider.Parts.Add(Part(ERole::Static, FVector(0, 21.7f, 0), { Box(0, 0, -8, -6, -8, 16, 14, 16) }));
				Strider.Parts.Add(Part(ERole::LegRight, FVector(-4, 16, 0), { Box(0, 32, -2, -16, -2, 4, 16, 4) }));
				Strider.Parts.Add(Part(ERole::LegLeft, FVector(4, 16, 0), { Box(0, 55, -2, -16, -2, 4, 16, 4) }));
				Strider.Parts.Add(Part(ERole::Static, FVector(-8, 19.0f, 8), { Box(16, 65, -12, 0, -16, 12, 0, 16) }, 0, 0, 1.222f));
				Strider.Parts.Add(Part(ERole::Static, FVector(-8, 24.0f, 8), { Box(16, 49, -12, 0, -16, 12, 0, 16) }, 0, 0, 1.135f));
				Strider.Parts.Add(Part(ERole::Static, FVector(-8, 28.0f, 8), { Box(16, 33, -12, 0, -16, 12, 0, 16) }, 0, 0, 0.873f));
				Strider.Parts.Add(Part(ERole::Static, FVector(8, 29.0f, 8), { Box(16, 33, 0, 0, -16, 12, 0, 16) }, 0, 0, -0.873f));
				Strider.Parts.Add(Part(ERole::Static, FVector(8, 25.0f, 8), { Box(16, 49, 0, 0, -16, 12, 0, 16) }, 0, 0, -1.135f));
				Strider.Parts.Add(Part(ERole::Static, FVector(8, 20.0f, 8), { Box(16, 65, 0, 0, -16, 12, 0, 16) }, 0, 0, -1.222f));
				Models.Add(Strider.Kind, Strider);
			}
			{
				// A ghast, from the logged model (`ghast1.txt`, one frame): a 16 cube body and nine 2 x L x 2 tentacles hanging from under it, all
				// drawn 4.5 times as big (the log's root scale) on a 64 x 32 picture. The tentacles' sway (between 0.2 and 0.6 tilt) is not
				// built, they rest at 0.4, and the shooting face is not shown.
				FModel Ghast;
				Ghast.Kind = TEXT("minecraft:ghast");
				Ghast.Scale = 4.5f;
				Ghast.Textures = { TEXT("ghast/ghast.png") };
				Ghast.Parts.Add(Part(ERole::Static, FVector(0, 6.4f, 0), { Box(0, 0, -8, -8, -8, 16, 16, 16) }));
				Ghast.Parts.Add(Part(ERole::Static, FVector(-3.75f, -0.6f, 5.0f), { Box(0, 0, -1, -8, -1, 2, 8, 2) }, 0.4f));
				Ghast.Parts.Add(Part(ERole::Static, FVector(1.25f, -0.6f, 5.0f), { Box(0, 0, -1, -13, -1, 2, 13, 2) }, 0.4f));
				Ghast.Parts.Add(Part(ERole::Static, FVector(6.25f, -0.6f, 5.0f), { Box(0, 0, -1, -9, -1, 2, 9, 2) }, 0.4f));
				Ghast.Parts.Add(Part(ERole::Static, FVector(-6.25f, -0.6f, 0.0f), { Box(0, 0, -1, -11, -1, 2, 11, 2) }, 0.4f));
				Ghast.Parts.Add(Part(ERole::Static, FVector(-1.25f, -0.6f, 0.0f), { Box(0, 0, -1, -11, -1, 2, 11, 2) }, 0.4f));
				Ghast.Parts.Add(Part(ERole::Static, FVector(3.75f, -0.6f, 0.0f), { Box(0, 0, -1, -10, -1, 2, 10, 2) }, 0.4f));
				Ghast.Parts.Add(Part(ERole::Static, FVector(-3.75f, -0.6f, -5.0f), { Box(0, 0, -1, -12, -1, 2, 12, 2) }, 0.4f));
				Ghast.Parts.Add(Part(ERole::Static, FVector(1.25f, -0.6f, -5.0f), { Box(0, 0, -1, -9, -1, 2, 9, 2) }, 0.4f));
				Ghast.Parts.Add(Part(ERole::Static, FVector(6.25f, -0.6f, -5.0f), { Box(0, 0, -1, -12, -1, 2, 12, 2) }, 0.4f));
				Models.Add(Ghast.Kind, Ghast);
			}
			{
				// A magma cube of size 1 (0.52 across), from the logged model (hogmagma1.txt): eight 8 x 1 x 8 slices stacked 8 high, each
				// with its own piece of a 64 x 64 picture (two columns of four), and a 4 x 4 x 4 core inside. Bigger sizes are the same model
				// grown with the hitbox.
				FModel Magma;
				Magma.Kind = TEXT("minecraft:magma_cube");
				Magma.TextureSize = FIntPoint(64, 64);
				Magma.Textures = { TEXT("slime/magmacube.png") };
				Magma.BaseWidth = 0.52f;
				for (int32 Slice = 0; Slice < 8; ++Slice)
				{
					Magma.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(Slice < 4 ? 0 : 32, (Slice % 4) * 9, -4, 7 - Slice, -4, 8, 1, 8) }));
				}
				Magma.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(24, 40, -2, 2, -2, 4, 4, 4) }));
				Models.Add(Magma.Kind, Magma);
			}
			{
				// The pictures are chosen per villager (GetVillagerTextures); these are the plains novice farmer's.
				FModel Villager;
				Villager.Kind = TEXT("minecraft:villager");
				Villager.Textures = { TEXT("villager/villager.png"), TEXT("villager/type/plains.png"), TEXT("villager/profession/farmer.png"), TEXT("villager/profession_level/stone.png") };
				AddVillager(Villager);
				Models.Add(Villager.Kind, Villager);
			}
			{
				// A zombie villager is the villager's shape in the zombie's greens; the pictures are chosen per one.
				FModel Zombie;
				Zombie.Kind = TEXT("minecraft:zombie_villager");
				Zombie.Textures = { TEXT("zombie_villager/zombie_villager.png"), TEXT("zombie_villager/type/plains.png") };
				AddVillager(Zombie, true);
				// Its arms are the zombie's, so it holds as the zombie does (compared with vanilla in zvheld.txt).
				AddHeldItemInHand(Zombie, FVector(-5, 22, 0));
				Models.Add(Zombie.Kind, Zombie);
			}
			{
				// The wandering trader wears one picture, laid out as the villager's.
				FModel Trader;
				Trader.Kind = TEXT("minecraft:wandering_trader");
				Trader.Textures = { TEXT("wandering_trader/wandering_trader.png") };
				AddVillager(Trader);
				Models.Add(Trader.Kind, Trader);
			}
			{
				// Pieces are cut from the textures' own nets (box sizes read off the picture), placed from minecraft.wiki's models:
				// the body is a long box along z, the wolf's and the others' laid down as the pig's is.
				FModel Horse;
				Horse.Kind = TEXT("minecraft:horse");
				Horse.TextureSize = FIntPoint(64, 64);
				Horse.Textures = { TEXT("horse/horse_brown.png") };
				// Measured: the hind legs swing 0.5 a point of walk amount and the front legs 0.8.
				Horse.LegSwing = 0.5f;
				Horse.FrontLegSwing = 0.8f;
				// The coat with its markings laid over it.
				Horse.bComposite = true;
				for (const TCHAR* Coat : { TEXT("white"), TEXT("creamy"), TEXT("chestnut"), TEXT("brown"), TEXT("black"), TEXT("gray"), TEXT("darkbrown") })
				{
					for (const TCHAR* Marking : { TEXT(""), TEXT("white"), TEXT("whitefield"), TEXT("whitedots"), TEXT("blackdots") })
					{
						Horse.Variants.Add({ FString::Printf(TEXT("horse/horse_%s.png"), Coat), FCString::Strlen(Marking) ? FString::Printf(TEXT("horse/horse_markings_%s.png"), Marking) : FString() });
					}
				}
				// Placed from the logged model (pivots and every box's extent, in vanilla's frame; ours is y = 24 - y, z = -z), which
				// draws the whole horse 1.1 times its size about its feet.
				Horse.Scale = 1.1f;
				Horse.Parts.Add(Part(ERole::Static, FVector(0, 13, -5), { Box(0, 32, -5, -2, -5, 10, 10, 22) }));
				// The tail hangs from the rump, leaning back.
				Horse.Parts.Add(Part(ERole::Static, FVector(0, 18, -7), { Box(42, 36, -1.5f, -14, -4, 3, 14, 4) }, 0.5236f));
				// The neck, head, mane, mouth and ears all lean forward from the withers together.
				Horse.Parts.Add(Part(ERole::Head, FVector(0, 20, 12),
					{ Box(0, 35, -2.05f, -6, -5, 4, 12, 7), Box(0, 0, -3, 6, -5, 6, 5, 7), Box(0, 25, -2, 6, 2, 4, 5, 5), Box(56, 36, -1, -5, -7.01f, 2, 16, 2),
					  Box(19, 16, -2.55f, 10, -5, 2, 3, 1), Box(19, 16, 0.55f, 10, -5, 2, 3, 1) }, 0.5236f));
				// The legs are 4 wide, 11 tall (a hair short of the ground), their boxes set 1 in from the hip and the front ones 0.1 forward.
				Horse.Parts.Add(Part(ERole::LegFrontRight, FVector(-4, 10, 10), { Box(48, 21, -1, -9.99f, -2.1f, 4, 11, 4) }));
				Horse.Parts.Add(Part(ERole::LegFrontLeft, FVector(4, 10, 10), { Box(48, 21, -3, -9.99f, -2.1f, 4, 11, 4) }));
				Horse.Parts.Add(Part(ERole::LegBackRight, FVector(-4, 10, -7), { Box(48, 21, -1, -9.99f, -3, 4, 11, 4) }));
				Horse.Parts.Add(Part(ERole::LegBackLeft, FVector(4, 10, -7), { Box(48, 21, -3, -9.99f, -3, 4, 11, 4) }));
				Models.Add(Horse.Kind, Horse);
			}
			{
				FModel Wolf;
				Wolf.Kind = TEXT("minecraft:wolf");
				Wolf.Textures = { TEXT("wolf/wolf.png"), TEXT("wolf/wolf_collar.png") };
				Wolf.TintedLayers = 2u;
				// In the order of data/minecraft/wolf_variant/ (McMobLook): the files by name.
				for (const TCHAR* Breed : { TEXT("wolf_ashen"), TEXT("wolf_black"), TEXT("wolf_chestnut"), TEXT("wolf"), TEXT("wolf_rusty"), TEXT("wolf_snowy"), TEXT("wolf_spotted"), TEXT("wolf_striped"), TEXT("wolf_woods") })
				{
					Wolf.Variants.Add({ FString::Printf(TEXT("wolf/%s.png"), Breed), TEXT("wolf/wolf_collar.png") });
				}
				// The poses are bits: 1 sitting, 2 angry, 4 tame. Standing is where bit 1 is clear; the collar is drawn for tame ones.
				// The boxes are cut from the picture; where they sit (pivots, tilts, the tail's angle by mood) was fitted to the
				// silhouettes of side-on shots of the real thing, at the same camera and a steady field of view.
				constexpr uint32 Standing = 0x55u;
				constexpr uint32 Sitting = 0xAAu;
				constexpr uint32 Wild = 0x03u;
				constexpr uint32 Angry = 0x0Cu;
				constexpr uint32 Tame = 0xF0u;
				// From the logged model (pivots and every box's extent), standing and sitting.
				const FBox StandRear = Box(18, 14, -3, -7, -3, 6, 9, 6);
				const FBox StandChest = Box(21, 0, -3, -3, -4, 8, 6, 7);
				const FBox StandChestCollar = Box(21, 0, -3, -3, -4, 8, 6, 7, 0.03f);
				const FBox StandLeg = Box(0, 18, 0, -8, -1, 2, 8, 2);
				const FBox StandTail = Box(9, 18, 0, -8, -1, 2, 8, 2);
				const auto Add = [&Wolf](FPart Each, uint32 Poses)
				{
					Each.ShowPoses = Poses;
					Wolf.Parts.Add(Each);
				};
				const std::initializer_list<FBox> HeadBoxes = { Box(0, 0, -3, -3, -2, 6, 6, 4), Box(16, 14, -3, 3, -1, 2, 2, 1), Box(16, 14, 1, 3, -1, 2, 2, 1), Box(0, 10, -1.5f, -3, 1, 3, 3, 4) };
				// Standing.
				Add(Part(ERole::Head, FVector(-1, 10.5f, 7), { Box(0, 0, -2, -3, -2, 6, 6, 4), Box(16, 14, -2, 3, -1, 2, 2, 1), Box(16, 14, 2, 3, -1, 2, 2, 1), Box(0, 10, -0.5f, -3, 1, 3, 3, 4) }), Standing);
				Add(Part(ERole::Static, FVector(0, 10, -2), { StandRear }, Flat), Standing);
				Add(Part(ERole::Static, FVector(-1, 10, 3), { StandChest }, Flat), Standing);
				Add(Part(ERole::Static, FVector(-1, 12, -8), { StandTail }, 0.6283f), Standing & Wild);
				Add(Part(ERole::Static, FVector(-1, 12, -8), { StandTail }, 1.45f), Standing & Angry);
				Add(Part(ERole::Static, FVector(-1, 12, -8), { StandTail }, 1.65f), Standing & Tame);
				Add(Part(ERole::LegFrontRight, FVector(-2.5f, 8, 4), { StandLeg }), Standing);
				Add(Part(ERole::LegFrontLeft, FVector(0.5f, 8, 4), { StandLeg }), Standing);
				Add(Part(ERole::LegBackRight, FVector(-2.5f, 8, -7), { StandLeg }), Standing);
				Add(Part(ERole::LegBackLeft, FVector(0.5f, 8, -7), { StandLeg }), Standing);
				// Sitting: the rear on its haunches, the chest raised, the hind legs folded forward and the front ones straight down.
				Add(Part(ERole::Head, FVector(-1, 10.5f, 7), { Box(0, 0, -2, -3, -2, 6, 6, 4), Box(16, 14, -2, 3, -1, 2, 2, 1), Box(16, 14, 2, 3, -1, 2, 2, 1), Box(0, 10, -0.5f, -3, 1, 3, 3, 4) }), Sitting);
				Add(Part(ERole::Static, FVector(0, 6, 0), { StandRear }, 0.7854f), Sitting);
				Add(Part(ERole::Static, FVector(-1, 8, 3), { StandChest }, 1.2566f), Sitting);
				Add(Part(ERole::Static, FVector(-1, 3, -6), { StandTail }, 0.6283f), Sitting & Wild);
				Add(Part(ERole::Static, FVector(-1, 3, -6), { StandTail }, 1.5f), Sitting & Angry);
				Add(Part(ERole::Static, FVector(-1, 3, -6), { StandTail }, 1.64f), Sitting & Tame);
				Add(Part(ERole::Static, FVector(-2.5f, 1.3f, -2), { StandLeg }, 4.7124f), Sitting);
				Add(Part(ERole::Static, FVector(0.5f, 1.3f, -2), { StandLeg }, 4.7124f), Sitting);
				Add(Part(ERole::Static, FVector(-2.49f, 7, 4), { StandLeg }, 5.8119f), Sitting);
				Add(Part(ERole::Static, FVector(0.49f, 7, 4), { StandLeg }, 5.8119f), Sitting);
				// The collar: the chest drawn again, a hair larger, in the collar's own picture.
				Add(Part(ERole::Static, FVector(-1, 10, 3), { StandChestCollar }, Flat, 0.0f, 0.0f, 0, 1), Standing & Tame);
				Add(Part(ERole::Static, FVector(-1, 8, 3), { StandChestCollar }, 1.2566f, 0.0f, 0.0f, 0, 1), Sitting & Tame);
				Models.Add(Wolf.Kind, Wolf);
			}
			{
				FModel Goat;
				Goat.Kind = TEXT("minecraft:goat");
				Goat.TextureSize = FIntPoint(64, 64);
				Goat.Textures = { TEXT("goat/goat.png") };
				// Placed from the logged model, which sits half a point to the right of centre (the head's pivot is at x 1 and the legs
				// at -3 and 1). The head is a box tipped 55 degrees forward about a pivot of its own, with the horns on the head itself.
				Goat.Parts.Add(Part(ERole::Static, FVector(0, 0, 0), { Box(1, 1, -4, 6, -9, 9, 11, 16), Box(0, 28, -5, 4, -3, 11, 14, 11) }));
				FBox GoatNose = Box(34, 46, -3, 5, 6, 5, 7, 10);
				GoatNose.BoxPivot = FVector(0, 8, 8);
				GoatNose.BoxRotation = FQuat(FVector::XAxisVector, 0.9599f);
				Goat.Parts.Add(Part(ERole::Head, FVector(1, 10, 0), { GoatNose, Box(12, 55, -2.99f, 9, 8, 2, 7, 2), Box(12, 55, -0.01f, 9, 8, 2, 7, 2, 0.0f, true), Box(2, 61, -6, 9, 9, 3, 2, 1), Box(2, 61, 2, 9, 9, 3, 2, 1, 0.0f, true), Box(23, 52, -0.5f, -4, 9, 0, 7, 5) }));
				const FBox FrontLegRight = Box(35, 2, 0, -10, -3, 3, 10, 3);
				const FBox FrontLegLeft = Box(49, 2, 0, -10, -3, 3, 10, 3);
				const FBox BackLegRight = Box(49, 29, 0, -10, -3, 3, 6, 3);
				const FBox BackLegLeft = Box(36, 29, 0, -10, -3, 3, 6, 3);
				Goat.Parts.Add(Part(ERole::LegFrontRight, FVector(-3, 10, 6), { FrontLegRight }));
				Goat.Parts.Add(Part(ERole::LegFrontLeft, FVector(1, 10, 6), { FrontLegLeft }));
				Goat.Parts.Add(Part(ERole::LegBackRight, FVector(-3, 10, -4), { BackLegRight }));
				Goat.Parts.Add(Part(ERole::LegBackLeft, FVector(1, 10, -4), { BackLegLeft }));
				Models.Add(Goat.Kind, Goat);
			}
			{
				FModel Fox;
				Fox.Kind = TEXT("minecraft:fox");
				Fox.TextureSize = FIntPoint(48, 32);
				Fox.Textures = { TEXT("fox/fox.png") };
				Fox.Variants = { { TEXT("fox/fox.png") }, { TEXT("fox/fox_snow.png") } };
				// Placed from the logged model: the head's pivot is at x -1, the body lies down about a pivot of its own, and the tail
				// hangs from the body (its turn is the body's quarter turn less 0.0524).
				Fox.Parts.Add(Part(ERole::Static, FVector(0, 8, 6), { Box(24, 15, -3, -14.999f, -2.5f, 6, 11, 6) }, Flat));
				Fox.Parts.Add(Part(ERole::Head, FVector(-1, 7.5f, 3), { Box(1, 5, -3, -4, -1, 8, 6, 6), Box(8, 1, -3, 2, 3, 2, 2, 1), Box(15, 1, 3, 2, 3, 2, 2, 1), Box(6, 18, -1, -4.01f, 5, 4, 2, 3) }));
				Fox.Parts.Add(Part(ERole::Static, FVector(-4, 7, -9), { Box(30, 0, 2, -9, -4, 4, 9, 5) }, Flat - 0.0524f));
				const FBox FoxLeg = Box(4, 24, 2, -6.5f, -1, 2, 6, 2);
				Fox.Parts.Add(Part(ERole::LegFrontRight, FVector(-5, 6.5f, 0), { FoxLeg }));
				Fox.Parts.Add(Part(ERole::LegFrontLeft, FVector(-1, 6.5f, 0), { FoxLeg }));
				Fox.Parts.Add(Part(ERole::LegBackRight, FVector(-5, 6.5f, -7), { FoxLeg }));
				Fox.Parts.Add(Part(ERole::LegBackLeft, FVector(-1, 6.5f, -7), { FoxLeg }));
				Models.Add(Fox.Kind, Fox);
			}
			for (const TCHAR* LlamaKind : { TEXT("minecraft:llama"), TEXT("minecraft:trader_llama") })
			{
				FModel Llama;
				Llama.Kind = LlamaKind;
				Llama.TextureSize = FIntPoint(128, 64);
				Llama.Textures = { TEXT("llama/llama_brown.png") };
				Llama.Variants = { { TEXT("llama/llama_creamy.png") }, { TEXT("llama/llama_white.png") }, { TEXT("llama/llama_brown.png") }, { TEXT("llama/llama_gray.png") } };
				// From the logged model (the same shape for the trader llama).
				Llama.Parts.Add(Part(ERole::Head, FVector(0, 17, 6), { Box(0, 0, -2, 10, 1, 4, 4, 9), Box(0, 14, -4, -2, 0, 8, 18, 6), Box(17, 0, -4, 16, 2, 3, 3, 2), Box(17, 0, 1, 16, 2, 3, 3, 2, 0.0f, true) }));  // head
				Llama.Parts.Add(Part(ERole::LegBackRight, FVector(-3.5f, 14, -6), { Box(29, 29, -2, -14, -2, 4, 14, 4) }));  // rightHindLeg
				Llama.Parts.Add(Part(ERole::LegBackLeft, FVector(3.5f, 14, -6), { Box(29, 29, -2, -14, -2, 4, 14, 4, 0.0f, true) }));  // leftHindLeg
				Llama.Parts.Add(Part(ERole::LegFrontRight, FVector(-3.5f, 14, 5), { Box(29, 29, -2, -14, -2, 4, 14, 4) }));  // rightFrontLeg
				Llama.Parts.Add(Part(ERole::LegFrontLeft, FVector(3.5f, 14, 5), { Box(29, 29, -2, -14, -2, 4, 14, 4, 0.0f, true) }));  // leftFrontLeg
				Llama.Parts.Add(Part(ERole::Static, FVector(0, 19, -2), { Box(29, 0, -6, -8, -3, 12, 18, 10) }, 1.5708f, 0, 0));  // root/body
				Models.Add(Llama.Kind, Llama);
			}
			{
				FModel Bee;
				Bee.Kind = TEXT("minecraft:bee");
				Bee.TextureSize = FIntPoint(64, 64);
				Bee.Textures = { TEXT("bee/bee.png") };
				Bee.Parts.Add(Part(ERole::Static, FVector(0, 5, 0), { Box(0, 0, -3.5f, -3, -5, 7, 7, 10), Box(26, 7, 0, 0, -7, 0, 1, 2) }));
				// The antennae, and three legs that are flat planes (a box with no depth, drawn both sides).
				Bee.Parts.Add(Part(ERole::Static, FVector(0, 5, 0), { Box(2, 0, 1.5f, 2, 5, 1, 2, 3), Box(2, 3, -2.5f, 2, 5, 1, 2, 3) }));
				Bee.Parts.Add(Part(ERole::Static, FVector(0, 5, 0), { Box(26, 1, -3.5f, -5, 2, 7, 2, 0), Box(26, 3, -3.5f, -5, 0, 7, 2, 0), Box(26, 5, -3.5f, -5, -2, 7, 2, 0) }));
				// Two flat wings that beat.
				Bee.Parts.Add(Part(ERole::WingRight, FVector(-1.5f, 9, 3), { Box(0, 18, -9, 0, -6, 9, 0, 6) }));
				Bee.Parts.Add(Part(ERole::WingLeft, FVector(1.5f, 9, 3), { Box(0, 18, 0, 0, -6, 9, 0, 6, 0.0f, true) }));
				Bee.bBeatWings = true;
				Models.Add(Bee.Kind, Bee);
			}
			{
				// The nets of the rabbit's picture (read off its pixels): body 8x6x10, head 5x5x5, ears 2x5x1, haunches 4x4x4, feet 2x1x6.
				FModel Rabbit;
				Rabbit.Kind = TEXT("minecraft:rabbit");
				Rabbit.TextureSize = FIntPoint(64, 64);
				Rabbit.Textures = { TEXT("rabbit/rabbit_brown.png") };
				for (const TCHAR* Coat : { TEXT("brown"), TEXT("white"), TEXT("black"), TEXT("gold"), TEXT("salt"), TEXT("white_splotched"), TEXT("toast") })
				{
					Rabbit.Variants.Add({ FString::Printf(TEXT("rabbit/rabbit_%s.png"), Coat) });
				}
				// From the logged model: the body leans 0.3927 back about a pivot at its hind end, the head, ears and tail hang from it, the
				// front legs turn back the lean (so they stand straight) and the hind feet are flat slabs turned 22.5 degrees out.
				Rabbit.Parts.Add(Part(ERole::Static, FVector(0, 1, -4), { Box(0, 0, -4, 0, -1, 8, 6, 10) }, -0.3927f));
				Rabbit.Parts.Add(Part(ERole::Static, FVector(0, 5.607f, -5.9216f), { Box(20, 16, -2, -0.992f, -2.987f, 4, 4, 4) }, -0.3927f));
				Rabbit.Parts.Add(Part(ERole::Head, FVector(0, 8.9983f, 1.4777f), { Box(0, 16, -2.5f, -2, -1, 5, 5, 5), Box(26, 0, -2.5f, 3, 0, 2, 5, 1), Box(32, 0, 0.5f, 3, 0, 2, 5, 1) }, -0.3927f));
				Rabbit.Parts.Add(Part(ERole::Static, FVector(-2, 2.9093f, 1.6259f), { Box(36, 18, -0.9f, -3, -1.1f, 2, 4, 2) }));
				Rabbit.Parts.Add(Part(ERole::Static, FVector(2, 2.9093f, 1.5336f), { Box(44, 18, -1, -3, -1, 2, 4, 2) }));
				Rabbit.Parts.Add(Part(ERole::LegBackRight, FVector(-3, 1, -4), { Box(20, 24, -1, -1, -1, 2, 1, 6) }, 0, -0.3927f));
				Rabbit.Parts.Add(Part(ERole::LegBackLeft, FVector(3, 1, -4), { Box(36, 24, -1, -1, -1, 2, 1, 6) }, 0, 0.3927f));
				Rabbit.bHops = true;
				for (int32 Index = 0; Index < Rabbit.Parts.Num(); ++Index)
				{
					// body, tail, head, the two front legs, the two hind feet
					static constexpr uint8 Groups[] = { 1, 3, 2, 4, 4, 5, 5 };
					Rabbit.Parts[Index].Hop = Groups[Index];
				}
				Models.Add(Rabbit.Kind, Rabbit);
			}
			{
				// The sea turtle's shell, belly and head, and its four flippers (nets read off the picture).
				FModel Turtle;
				Turtle.Kind = TEXT("minecraft:turtle");
				Turtle.TextureSize = FIntPoint(128, 64);
				Turtle.Textures = { TEXT("turtle/turtle.png") };
				Turtle.Parts.Add(Part(ERole::Static, FVector(0, 6, -3), { Box(7, 37, -9.5f, -10, -3, 19, 20, 6) }, Flat));  // turned like the belly: -Flat drew it upside down
				Turtle.Parts.Add(Part(ERole::Static, FVector(0, 1.5f, -2), { Box(31, 1, -5.5f, -9, -1.5f, 11, 18, 3) }, Flat));
				Turtle.Parts.Add(Part(ERole::Head, FVector(0, 5, 10), { Box(3, 0, -3, -4, -3, 6, 5, 6) }));
				Turtle.Parts.Add(Part(ERole::LegBackRight, FVector(-3.5f, 2, -11), { Box(1, 23, -2, -1, -10, 4, 1, 10) }));
				Turtle.Parts.Add(Part(ERole::LegBackLeft, FVector(3.5f, 2, -11), { Box(1, 23, -2, -1, -10, 4, 1, 10, 0.0f, true) }));
				Turtle.Parts.Add(Part(ERole::LegFrontRight, FVector(-5, 3, 4), { Box(27, 30, -13, -1, -3, 13, 1, 5) }));
				Turtle.Parts.Add(Part(ERole::LegFrontLeft, FVector(5, 3, 4), { Box(27, 30, 0, -1, -3, 13, 1, 5, 0.0f, true) }));
				Models.Add(Turtle.Kind, Turtle);
			}
			{
				// The camel, from the logged model (camel_model.txt): the reference's y runs down from 24 and its z the other way, so a
				// pivot is (x, 24 - y, -z) and a box starts at (x, -maxY, -maxZ). The head carries the neck, the muzzle and the ears; the
				// body carries the hump and the tail.
				FModel Camel;
				Camel.Kind = TEXT("minecraft:camel");
				Camel.TextureSize = FIntPoint(128, 128);
				Camel.Textures = { TEXT("camel/camel.png") };
				Camel.Parts.Add(Part(ERole::Head, FVector(0, 27, 19.5f), { Box(60, 24, -3.5f, -1, -4, 7, 8, 19), Box(21, 0, -3.5f, 7, 8, 7, 14, 7), Box(50, 0, -2.5f, 16, 15, 5, 5, 6), Box(67, 0, -5, 19.5f, 8.5f, 3, 1, 2), Box(45, 0, 2, 19.5f, 8.5f, 3, 1, 2) }));
				Camel.Parts.Add(Part(ERole::Static, FVector(0, 20, -9.5f), { Box(0, 25, -7.5f, 0, -3.5f, 15, 12, 27), Box(74, 0, -4.5f, 12, 4.5f, 9, 5, 11), Box(122, 0, -1.5f, -5, -3.5f, 3, 14, 0) }));
				Camel.Parts.Add(Part(ERole::LegFrontRight, FVector(-4.9f, 23, 10.5f), { Box(0, 26, -2.5f, -23, -2.5f, 5, 21, 5) }));
				Camel.Parts.Add(Part(ERole::LegFrontLeft, FVector(4.9f, 23, 10.5f), { Box(0, 0, -2.5f, -23, -2.5f, 5, 21, 5) }));
				Camel.Parts.Add(Part(ERole::LegBackRight, FVector(-4.9f, 23, -9.5f), { Box(94, 16, -2.5f, -23, -2.5f, 5, 21, 5) }));
				Camel.Parts.Add(Part(ERole::LegBackLeft, FVector(4.9f, 23, -9.5f), { Box(58, 16, -2.5f, -23, -2.5f, 5, 21, 5) }));
				// The saddle layer (the logged saddle model, drawn when the camel is saddled: pose 1 from AMcEntityActor): the bridle on the head
				// (the head's boxes again on the saddle picture, and two straps) and the saddle on the back. The reins part is hidden in every state
				// logged (saddled, ridden: camel_ride.txt), so it is not drawn.
				Camel.Textures.Add(TEXT("equipment/camel_saddle/saddle.png"));
				const uint32 Saddled = 1u << 1;
				Camel.Parts.Add(Part(ERole::Head, FVector(0, 27, 19.5f), { Box(60, 87, -3.5f, -1, -4, 7, 8, 19), Box(21, 64, -3.5f, 7, 8, 7, 14, 7), Box(50, 64, -2.5f, 16, 15, 5, 5, 6), Box(74, 70, 2.5f, 17, 16, 1, 2, 2), Box(74, 70, -3.5f, 17, 16, 1, 2, 2) }, 0, 0, 0, 0, 1));
				Camel.Parts.Add(Part(ERole::Static, FVector(0, 20, -9.5f), { Box(74, 64, -4.5f, 12, 4.5f, 9, 5, 11), Box(92, 114, -3.5f, 17, 4.5f, 7, 3, 11), Box(0, 89, -7.5f, 0, -3.5f, 15, 12, 27, 0.02f) }, 0, 0, 0, 0, 1));
				for (FPart& Each : Camel.Parts)
				{
					if (Each.Layer == 1)
					{
						Each.ShowPoses = Saddled;
					}
				}
				Models.Add(Camel.Kind, Camel);
				// The husk has its own picture and the same shape.
				FModel Husk = Camel;
				Husk.Kind = TEXT("minecraft:camel_husk");
				Husk.Textures = { TEXT("camel/camel_husk.png"), TEXT("equipment/camel_husk_saddle/saddle.png") };
				Models.Add(Husk.Kind, Husk);
			}
			// Young ones with a model of their own (not the grown one's halved), from the logged baby models: every pivot, tilt and box,
			// on the baby pictures (the grown one's names with _baby before the .png).
			{
				const auto BabyPicture = [](const FString& Path)
				{
					return Path.IsEmpty() ? Path : Path.LeftChop(4) + TEXT("_baby.png");
				};
				const auto Young = [&Models, &BabyPicture](const TCHAR* Kind, int32 Width, int32 Height) -> FModel
				{
					FModel Baby = Models[FName(Kind)];
					Baby.Kind = FName(*(FString(Kind) + TEXT("_baby")));
					Baby.bBabyModel = true;
					Baby.TextureSize = FIntPoint(Width, Height);
					Baby.Parts.Reset();
					for (FString& Each : Baby.Textures)
					{
						Each = BabyPicture(Each);
					}
					for (TArray<FString>& Look : Baby.Variants)
					{
						for (FString& Each : Look)
						{
							Each = BabyPicture(Each);
						}
					}
					return Baby;
				};

				{
					FModel Baby = Young(TEXT("minecraft:horse"), 64, 64);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 14, 6), { Box(30, 0, -2, -2, -2, 4, 8, 4), Box(0, 0, -3, 5.9996f, -1.9999f, 6, 4, 9), Posed(Box(0, 0, -3, 9.8f, -1.8499f, 2, 3, 1), FVector(-2, 10.3f, -1.3499f), 0, 0, 0.2618f), Posed(Box(0, 4, 1, 9.8f, -1.85f, 2, 3, 1), FVector(2, 10.3f, -1.65f), 0, 0, -0.2618f) }, 0.6109f, 0, 0));  // headParts
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 11.5f, 0), { Box(0, 13, -4, -3.5f, -7, 8, 7, 14) }));  // body
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 12.5f, -7), { Box(24, 34, -1.5f, -1.5f, -7, 3, 3, 8) }, -0.7418f, 0, 0));  // body/tail
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-2.4f, 8, -5.4f), { Box(0, 46, -1.5f, -8, -1.5f, 3, 9, 3) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(2.4f, 8, -5.4f), { Box(12, 46, -1.5f, -8, -1.5f, 3, 9, 3) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-2.4f, 8, 5.4f), { Box(0, 34, -1.5f, -8, -1.5f, 3, 9, 3) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(2.4f, 8, 5.4f), { Box(12, 34, -1.5f, -8, -1.5f, 3, 9, 3) }));  // leftFrontLeg
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:sheep"), 64, 32);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 8.5f, 2.5f), { Box(0, 0, -2.5f, -0.5f, -1.5f, 5, 5, 5) }));  // head
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 7, -0.5f), { Box(0, 10, -3, -2, -4.5f, 6, 4, 9) }));  // body
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-2, 5, -3), { Box(0, 23, -1, -5, -1, 2, 5, 2) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(2, 5, -3), { Box(24, 12, -1, -5, -1, 2, 5, 2) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-2, 5, 2), { Box(8, 23, -1, -5, -1, 2, 5, 2) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(2, 5, 2), { Box(24, 5, -1, -5, -1, 2, 5, 2) }));  // leftFrontLeg
					// The wool layer's logged model is the baby's own boxes again (same pivots and picture offsets, no inflation), on the wool picture.
					const int32 Skin = Baby.Parts.Num();
					for (int32 Index = 0; Index < Skin; ++Index)
					{
						FPart Wool = Baby.Parts[Index];
						Wool.Layer = 1;
						Baby.Parts.Add(Wool);
					}
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:wolf"), 32, 32);
					// From the logged baby wolf: standing, and sitting (tame ones only), the tail by mood (wild, angry, tame; the log showed no change with health).
					constexpr uint32 Standing = 0x55u;
					constexpr uint32 Sitting = 0xAAu;
					constexpr uint32 Wild = 0x03u;
					constexpr uint32 Angry = 0x0Cu;
					constexpr uint32 Tame = 0xF0u;
					const auto Add = [&Baby](FPart Each, uint32 Poses)
					{
						Each.ShowPoses = Poses;
						Baby.Parts.Add(Each);
					};
					Add(Part(ERole::Head, FVector(0, 5.75f, 4), { Box(0, 12, -2.99f, -1.75f, -2, 6, 5, 5), Box(17, 12, -1.5f, -1.76f, 3, 3, 2, 2), Box(0, 5, -3, 3.25f, 0, 2, 2, 1), Box(20, 5, 1, 3.25f, 0, 2, 2, 1) }), 0xFFFFFFFFu);  // head
					Add(Part(ERole::Static, FVector(0, 5, 0), { Box(0, 0, -3, -2, -4, 6, 4, 8) }), Standing);  // body
					Add(Part(ERole::LegBackRight, FVector(-1.5f, 3, -3), { Box(0, 22, -1, -3, -1, 2, 3, 2) }), Standing);  // rightHindLeg
					Add(Part(ERole::LegBackLeft, FVector(1.5f, 3, -3), { Box(8, 22, -1, -3, -1, 2, 3, 2) }), Standing);  // leftHindLeg
					Add(Part(ERole::LegFrontRight, FVector(-1.5f, 3, 3), { Box(0, 0, -1, -3, -1, 2, 3, 2) }), Standing);  // rightFrontLeg
					Add(Part(ERole::LegFrontLeft, FVector(1.5f, 3, 3), { Box(20, 0, -1, -3, -1, 2, 3, 2) }), Standing);  // leftFrontLeg
					Add(Part(ERole::Static, FVector(0, 5.603f, -2.8091f), { Box(22, 16, -1, -0.3f, -1, 2, 6, 2) }, -2.4717f, 0, 0), Standing & Wild);  // tail/tail_r1
					Add(Part(ERole::Static, FVector(0, 5.2187f, -2.4066f), { Box(22, 16, -1, -0.3f, -1, 2, 6, 2) }, -1.5606f, 0, 0), Standing & Angry);  // tail/tail_r1
					Add(Part(ERole::Static, FVector(0, 5.1037f, -2.3761f), { Box(22, 16, -1, -0.3f, -1, 2, 6, 2) }, -1.3721f, 0, 0), Standing & Tame);  // tail/tail_r1
					Add(Part(ERole::Static, FVector(0, 3, 1), { Box(0, 0, -3, -2, -4, 6, 4, 8) }, -0.7854f, 0, 0), Sitting);  // body sitting
					Add(Part(ERole::Static, FVector(-1.5f, -0.35f, -0.5f), { Box(0, 22, -1, -3, -1, 2, 3, 2) }, -1.5708f, 0, 0), Sitting);  // rightHindLeg sitting
					Add(Part(ERole::Static, FVector(1.5f, -0.35f, -0.5f), { Box(8, 22, -1, -3, -1, 2, 3, 2) }, -1.5708f, 0, 0), Sitting);  // leftHindLeg sitting
					Add(Part(ERole::Static, FVector(-1.495f, 2.5f, 3), { Box(0, 0, -1, -3, -1, 2, 3, 2) }, -0.4713f, 0, 0), Sitting);  // rightFrontLeg sitting
					Add(Part(ERole::Static, FVector(1.495f, 2.5f, 3), { Box(20, 0, -1, -3, -1, 2, 3, 2) }, -0.4713f, 0, 0), Sitting);  // leftFrontLeg sitting
					Add(Part(ERole::Static, FVector(0, 0.6037f, -1.3761f), { Box(22, 16, -1, -0.3f, -1, 2, 6, 2) }, -1.3721f, 0, 0), Sitting);  // tail/tail_r1 sitting
					// The collar: the body drawn again, a hair larger, in the collar picture, for tame ones.
					Add(Part(ERole::Static, FVector(0, 5, 0), { Box(0, 0, -3, -2, -4, 6, 4, 8, 0.03f) }, 0, 0, 0, 0, 1), Standing & Tame);
					Add(Part(ERole::Static, FVector(0, 3, 1), { Box(0, 0, -3, -2, -4, 6, 4, 8, 0.03f) }, -0.7854f, 0, 0, 0, 1), Sitting & Tame);
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:goat"), 64, 64);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 8.5f, 3), { Posed(Box(24, 0, -1.5f, 4, 0, 1, 2, 1), FVector(-1.5f, 1.5f, 1), -0.3927f, 0, 0), Posed(Box(24, 0, 0.5f, 4, 0, 1, 2, 1, 0.0f, true), FVector(-1.5f, 1.5f, 1), -0.3927f, 0, 0), Box(0, 0, -2, -0.1874f, -0.8452f, 4, 4, 6), Posed(Box(0, 12, -3.7f, 1.8126f, -0.6452f, 2, 1, 1), FVector(-1.7f, 2.3126f, -0.1452f), 0, 0.5236f, 0), Posed(Box(0, 12, 1.7f, 1.8126f, -0.6452f, 2, 1, 1, 0.0f, true), FVector(1.7f, 2.3126f, -0.1452f), 0, -0.5236f, 0) }, 0.4363f, 0, 0));  // head
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 6.2f, 0), { Box(0, 10, -3, -2.7f, -4.5f, 6, 5, 9), Box(0, 24, -2.5f, -1.8f, -4, 5, 4, 8) }));  // body
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-1.5f, 4.5f, -3), { Box(21, 12, -1, -4.5f, -1, 2, 5, 2) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(1.5f, 4.5f, -3), { Box(29, 12, -1, -4.5f, -1, 2, 5, 2) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-1.5f, 4.5f, 2), { Box(21, 5, -1, -4.5f, -1, 2, 5, 2) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(1.5f, 4.5f, 2), { Box(29, 5, -1, -4.5f, -1, 2, 5, 2) }));  // leftFrontLeg
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:fox"), 32, 32);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 5.875f, -0.125f), { Box(0, 0, -3, -2.875f, 0.125f, 6, 5, 5), Box(18, 20, -1, -2.875f, 5.125f, 2, 2, 2), Box(22, 8, -3, 2.125f, 3.125f, 2, 2, 1), Box(22, 11, 1, 2.125f, 3.125f, 2, 2, 1) }));  // head
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 4, -2), { Box(0, 10, -2.5f, -2, -3, 5, 4, 6) }));  // body
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 4.5f, -5), { Box(0, 20, -1.5f, -1.52f, -5, 3, 3, 6) }));  // body/tail
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-1.5f, 2, -4), { Box(22, 4, -1, -2, -1, 2, 2, 2) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(1.5f, 2, -4), { Box(22, 0, -1, -2, -1, 2, 2, 2) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-1.5f, 2, 0), { Box(22, 4, -1, -2, -1, 2, 2, 2) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(1.5f, 2, 0), { Box(22, 0, -1, -2, -1, 2, 2, 2) }));  // leftFrontLeg
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:bee"), 32, 32);
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 4.3333f, 1.8567f), { Box(6, 12, 1, -0.333f, 0.163f, 1, 2, 2), Box(0, 12, -2, -0.333f, 0.193f, 1, 2, 2) }));  // bone
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 1, 0), { Box(13, 0, -1.5f, -1, 0, 3, 1, 0) }));  // bone/front_legs
					Baby.Parts.Add(Part(ERole::WingRight, FVector(-1, 5, 1), { Box(3, 9, -3, 0, -3, 3, 0, 3) }, 0.2182f, -0.3491f, 0));  // bone/right_wing
					Baby.Parts.Add(Part(ERole::WingLeft, FVector(1, 5, 1), { Box(3, 9, 0, 0, -3, 3, 0, 3, 0.0f, true) }, 0.2182f, 0.3491f, 0));  // bone/left_wing
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 1, -1), { Box(13, 1, -1.5f, -1, 0, 3, 1, 0) }));  // bone/middle_legs
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 3, -0.5f), { Box(0, 0, -2, -2, -2.5f, 4, 4, 5) }));  // bone/body
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 2.5f, -3), { Box(13, 2, 0, -0.5f, -1, 0, 1, 1) }));  // bone/body/stinger
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 1, -2), { Box(13, 2, -1.5f, -1, 0, 3, 1, 0) }));  // bone/back_legs
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:rabbit"), 32, 32);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 6, 1), { Box(0, 0, -2.5f, -1, -1, 5, 4, 4), Box(18, 0, -2.5f, 3, 0, 2, 4, 1), Box(24, 0, 0.5f, 3, 0, 2, 4, 1) }));  // head
					Baby.Parts.Add(Part(ERole::Static, FVector(-2, 0.5f, -3.4f), { Box(0, 17, -2, -0.5f, -3, 2, 1, 3) }, 3.1416f, 0.7854f, -3.1416f));  // root/backlegs/right_hind_leg/right_haunch
					Baby.Parts.Add(Part(ERole::Static, FVector(0.5f, 0.5f, -2), { Box(10, 17, -2, -0.5f, -3, 2, 1, 3) }, 3.1416f, -0.7854f, -3.1416f));  // root/backlegs/left_hind_leg/left_haunch
					Baby.Parts.Add(Part(ERole::Static, FVector(-0.1f, 3.2f, -3.6f), { Box(0, 21, -1.4f, -0.973f, -1.982f, 3, 3, 3) }, -0.5236f, 0, 0));  // root/body/tail/tail_r1
					Baby.Parts.Add(Part(ERole::Static, FVector(-1, 1.5761f, 1.1173f), { Box(14, 8, -0.5f, -1.5f, -0.5f, 1, 3, 1) }));  // root/body/frontlegs/right_front_leg/right_front_leg_r1
					Baby.Parts.Add(Part(ERole::Static, FVector(1, 1.5761f, 1.1173f), { Box(18, 8, -0.5f, -1.5f, -0.5f, 1, 3, 1) }));  // root/body/frontlegs/left_front_leg/left_front_leg_r1
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 3, 0), { Box(0, 8, -2, -1, -3, 4, 3, 6) }, -0.5236f, 0, 0));  // root/body/body_r1
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:turtle"), 16, 16);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 1.1f, 1), { Box(0, 6, -1.5f, -1, 0, 3, 3, 3) }));  // head
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 1.1f, -1), { Box(0, 0, -2, -1, -2, 4, 2, 4) }));  // body
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-2, 0.1f, -2.5f), { Box(1, 0, -2, 0, -0.5f, 2, 0, 1, 0.0f, true) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(2, 0.1f, -2.5f), { Box(1, 1, 0, 0, -0.5f, 2, 0, 1, 0.0f, true) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-2, 0.1f, 0.5f), { Box(8, 6, -2, 0, -0.5f, 2, 0, 1) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(2, 0.1f, 0.5f), { Box(8, 7, 0, 0, -0.5f, 2, 0, 1) }));  // leftFrontLeg
					Models.Add(Baby.Kind, Baby);
				}
				{
					// The baby camel (same frame change as the grown one's); its ears are turned out 0.8223 about their own pivots.
					FModel Baby = Young(TEXT("minecraft:camel"), 64, 64);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 23, 7.5f), { Box(20, 0, -2.5f, -2, 0.5f, 5, 5, 7), Box(0, 0, -2.5f, 3, 2.5f, 5, 9, 5), Box(0, 14, -2.5f, 8, 7.5f, 5, 4, 3),
						Posed(Box(37, 0, -5.5f, 10.5f, 3, 3, 1, 2), FVector(-2.5f, 11, 4), 0, 0, 0.8223f), Posed(Box(47, 0, 2.5f, 10.5f, 3, 3, 1, 2), FVector(2.5f, 11, 4), 0, 0, -0.8223f) }));  // head and its ears
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 16.9f, 0), { Box(0, 14, -4.5f, -4, -8, 9, 8, 16), Box(50, 38, -1.5f, -7, -8.05f, 3, 9, 0) }));  // body and tail
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-3, 12.5f, 5.5f), { Box(36, 14, -1.5f, -12.5f, -1.5f, 3, 13, 3) }));
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(3, 12.5f, 5.5f), { Box(48, 14, -1.5f, -12.5f, -1.5f, 3, 13, 3) }));
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-3, 12.5f, -5.5f), { Box(0, 38, -1.5f, -12.5f, -1.5f, 3, 13, 3) }));
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(3, 12.5f, -5.5f), { Box(12, 38, -1.5f, -12.5f, -1.5f, 3, 13, 3) }));
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:pig"), 32, 32);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 5, 2), { Box(0, 15, -3.5f, -1, -1, 7, 6, 6), Box(6, 27, -1.5f, -0.025f, 5, 3, 2, 1) }));  // head
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 5, -0.5f), { Box(0, 0, -3.5f, -3, -4.5f, 7, 6, 9) }));  // body
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-2.5f, 2, -4), { Box(23, 4, -1, -2, -1, 2, 2, 2) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(2.5f, 2, -4), { Box(0, 4, -1, -2, -1, 2, 2, 2) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-2.5f, 2, 3), { Box(23, 0, -1, -2, -1, 2, 2, 2) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(2.5f, 2, 3), { Box(0, 0, -1, -2, -1, 2, 2, 2) }));  // leftFrontLeg
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:cow"), 64, 64);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 10.431f, 5.1667f), { Box(0, 18, -3, -1.431f, -0.167f, 6, 6, 5), Box(8, 29, 3, 3.569f, 2.833f, 1, 2, 1), Box(4, 29, -4, 3.569f, 2.833f, 1, 2, 1), Box(12, 29, -2, -1.431f, 4.833f, 4, 3, 1) }));  // head
					Baby.Parts.Add(Part(ERole::Static, FVector(3, 5, 5), { Box(0, 0, -7, 1, -11, 8, 6, 12) }));  // body
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-2.5f, 6, -3.5f), { Box(22, 27, -1.5f, -6, -1.5f, 3, 6, 3) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(2.5f, 6, -3.5f), { Box(34, 27, -1.5f, -6, -1.5f, 3, 6, 3) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-2.5f, 6, 3.5f), { Box(22, 18, -1.5f, -6, -1.5f, 3, 6, 3) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(2.5f, 6, 3.5f), { Box(34, 18, -1.5f, -6, -1.5f, 3, 6, 3) }));  // leftFrontLeg
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:chicken"), 16, 16);
					Baby.WingRest = 0.0611f;
					Baby.Parts.Add(Part(ERole::LegRight, FVector(-1, 2, -0.5f), { Box(0, 2, -0.5f, -2, 0, 1, 2, 0), Box(0, 0, -0.5f, -2, 0, 1, 0, 1) }));  // rightLeg
					Baby.Parts.Add(Part(ERole::LegLeft, FVector(1, 2, -0.5f), { Box(2, 2, -0.5f, -2, 0, 1, 2, 0), Box(0, 1, -0.5f, -2, 0, 1, 0, 1) }));  // leftLeg
					Baby.Parts.Add(Part(ERole::WingLeft, FVector(2, 4, 0), { Box(6, 8, 0, 0, -1, 1, 0, 2) }));  // rightWing
					Baby.Parts.Add(Part(ERole::WingRight, FVector(-2, 4, 0), { Box(4, 8, -1, 0, -1, 1, 0, 2) }));  // leftWing
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 3.75f, 1.25f), { Box(0, 0, -2, -1.75f, -3.25f, 4, 4, 4), Box(10, 8, -1, -0.75f, 0.75f, 2, 1, 1) }));  // root/body
					Models.Add(Baby.Kind, Baby);
				}
				{
					// A donkey: the horse's shape, from its own logged model (the chests are only drawn when carried).
					FModel Beast;
					Beast.Kind = TEXT("minecraft:donkey");
					Beast.TextureSize = FIntPoint(64, 64);
					Beast.Textures = { TEXT("horse/donkey.png") };
					Beast.Scale = 0.87f;
					Beast.Parts.Add(Part(ERole::Head, FVector(0, 20, 12), { Box(0, 35, -2.05f, -6, -5, 4, 12, 7), Box(0, 13, -3, 6, -5, 6, 5, 7), Posed(Box(0, 12, -2.25f, 10, -5, 2, 7, 1), FVector(-1.25f, 10, -4), 0.2618f, 0, 0.2618f), Posed(Box(0, 12, 0.25f, 10, -5, 2, 7, 1, 0.0f, true), FVector(1.25f, 10, -4), 0.2618f, 0, -0.2618f), Box(56, 36, -1, -5, -7.01f, 2, 16, 2), Box(0, 25, -2, 6, 2, 4, 5, 5) }, 0.5236f, 0, 0));  // headParts
					Beast.Parts.Add(Part(ERole::Static, FVector(0, 13, -5), { Box(0, 32, -5, -2, -5, 10, 10, 22) }));  // body
					Beast.Parts.Add(Part(ERole::Static, FVector(0, 18, -7), { Box(42, 36, -1.5f, -14, -4, 3, 14, 4) }, 0.5236f, 0, 0));  // body/tail
					Beast.Parts.Add(Part(ERole::LegBackRight, FVector(-4, 10, -7), { Box(48, 21, -1, -9.99f, -3, 4, 11, 4) }));  // rightHindLeg
					Beast.Parts.Add(Part(ERole::LegBackLeft, FVector(4, 10, -7), { Box(48, 21, -3, -9.99f, -3, 4, 11, 4, 0.0f, true) }));  // leftHindLeg
					Beast.Parts.Add(Part(ERole::LegFrontRight, FVector(-4, 10, 10), { Box(48, 21, -1, -9.99f, -2.1f, 4, 11, 4) }));  // rightFrontLeg
					Beast.Parts.Add(Part(ERole::LegFrontLeft, FVector(4, 10, 10), { Box(48, 21, -3, -9.99f, -2.1f, 4, 11, 4, 0.0f, true) }));  // leftFrontLeg
					Models.Add(Beast.Kind, Beast);
				}
				{
					// A mule: the horse's shape, from its own logged model (the chests are only drawn when carried).
					FModel Beast;
					Beast.Kind = TEXT("minecraft:mule");
					Beast.TextureSize = FIntPoint(64, 64);
					Beast.Textures = { TEXT("horse/mule.png") };
					Beast.Scale = 0.92f;
					Beast.Parts.Add(Part(ERole::Head, FVector(0, 20, 12), { Box(0, 35, -2.05f, -6, -5, 4, 12, 7), Box(0, 13, -3, 6, -5, 6, 5, 7), Posed(Box(0, 12, -2.25f, 10, -5, 2, 7, 1), FVector(-1.25f, 10, -4), 0.2618f, 0, 0.2618f), Posed(Box(0, 12, 0.25f, 10, -5, 2, 7, 1, 0.0f, true), FVector(1.25f, 10, -4), 0.2618f, 0, -0.2618f), Box(56, 36, -1, -5, -7.01f, 2, 16, 2), Box(0, 25, -2, 6, 2, 4, 5, 5) }, 0.5236f, 0, 0));  // headParts
					Beast.Parts.Add(Part(ERole::Static, FVector(0, 13, -5), { Box(0, 32, -5, -2, -5, 10, 10, 22) }));  // body
					Beast.Parts.Add(Part(ERole::Static, FVector(0, 18, -7), { Box(42, 36, -1.5f, -14, -4, 3, 14, 4) }, 0.5236f, 0, 0));  // body/tail
					Beast.Parts.Add(Part(ERole::LegBackRight, FVector(-4, 10, -7), { Box(48, 21, -1, -9.99f, -3, 4, 11, 4) }));  // rightHindLeg
					Beast.Parts.Add(Part(ERole::LegBackLeft, FVector(4, 10, -7), { Box(48, 21, -3, -9.99f, -3, 4, 11, 4, 0.0f, true) }));  // leftHindLeg
					Beast.Parts.Add(Part(ERole::LegFrontRight, FVector(-4, 10, 10), { Box(48, 21, -1, -9.99f, -2.1f, 4, 11, 4) }));  // rightFrontLeg
					Beast.Parts.Add(Part(ERole::LegFrontLeft, FVector(4, 10, 10), { Box(48, 21, -3, -9.99f, -2.1f, 4, 11, 4, 0.0f, true) }));  // leftFrontLeg
					Models.Add(Beast.Kind, Beast);
				}
				{
					FModel Baby = Young(TEXT("minecraft:llama"), 64, 64);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 12, 4), { Box(0, 0, -3, -2, 0, 6, 11, 4), Box(0, 15, -1.5f, 4, 4, 3, 3, 3), Box(20, 4, 0.5f, 9, 1, 2, 2, 2), Box(20, 0, -2.5f, 9, 1, 2, 2, 2) }));  // head
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-2.5f, 7.5f, -4.5f), { Box(0, 45, -1.4f, -7.5f, -1.5f, 3, 8, 3) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(2.5f, 7.5f, -4.5f), { Box(12, 45, -1.6f, -7.5f, -1.5f, 3, 8, 3) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-2.5f, 7.5f, 3.5f), { Box(0, 34, -1.4f, -7.5f, -1.5f, 3, 8, 3) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(2.5f, 7.5f, 3.5f), { Box(12, 34, -1.6f, -7.5f, -1.5f, 3, 8, 3) }));  // leftFrontLeg
					Baby.Parts.Add(Part(ERole::Static, FVector(-8.5f, 20, -3), { Box(45, 28, -3, -8, -3, 8, 8, 3) }, 3.1416f, -1.5708f, -3.1416f));  // rightChest
					Baby.Parts.Add(Part(ERole::Static, FVector(5.5f, 20, -3), { Box(45, 41, -3, -8, -3, 8, 8, 3) }, 3.1416f, -1.5708f, -3.1416f));  // leftChest
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 10, -2.5f), { Box(0, 15, -4, -3, -4.5f, 8, 6, 13) }));  // root/body
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:trader_llama"), 64, 64);
					Baby.Parts.Add(Part(ERole::Head, FVector(0, 12, 4), { Box(0, 0, -3, -2, 0, 6, 11, 4), Box(0, 15, -1.5f, 4, 4, 3, 3, 3), Box(20, 4, 0.5f, 9, 1, 2, 2, 2), Box(20, 0, -2.5f, 9, 1, 2, 2, 2) }));  // head
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-2.5f, 7.5f, -4.5f), { Box(0, 45, -1.4f, -7.5f, -1.5f, 3, 8, 3) }));  // rightHindLeg
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(2.5f, 7.5f, -4.5f), { Box(12, 45, -1.6f, -7.5f, -1.5f, 3, 8, 3) }));  // leftHindLeg
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-2.5f, 7.5f, 3.5f), { Box(0, 34, -1.4f, -7.5f, -1.5f, 3, 8, 3) }));  // rightFrontLeg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(2.5f, 7.5f, 3.5f), { Box(12, 34, -1.6f, -7.5f, -1.5f, 3, 8, 3) }));  // leftFrontLeg
					Baby.Parts.Add(Part(ERole::Static, FVector(-8.5f, 20, -3), { Box(45, 28, -3, -8, -3, 8, 8, 3) }, 3.1416f, -1.5708f, -3.1416f));  // rightChest
					Baby.Parts.Add(Part(ERole::Static, FVector(5.5f, 20, -3), { Box(45, 41, -3, -8, -3, 8, 8, 3) }, 3.1416f, -1.5708f, -3.1416f));  // leftChest
					Baby.Parts.Add(Part(ERole::Static, FVector(0, 10, -2.5f), { Box(0, 15, -4, -3, -4.5f, 8, 6, 13) }));  // root/body
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:donkey"), 64, 64);
					Baby.Scale = 1.0f;
					Baby.Parts.Add(Part(ERole::Head, FVector(1, 13, 5), { Posed(Box(0, 0, -4, 5.6f, 1.4f, 6, 4, 9), FVector(0, 6, 2), 0.3927f, 0, 0), Posed(Box(22, 0, -4, 8, 3.3f, 2, 7, 1), FVector(-2, 8.5f, 4), 0.48f, 0, 0.48f), Posed(Box(0, 0, 0, 8, 3.3f, 2, 7, 1), FVector(2, 8.5f, 4), 0.48f, 0, -0.48f), Posed(Box(30, 9, -3, -2, -1, 4, 8, 4), FVector(0, 0, 0), 0.3927f, 0, 0) }));  // body/head_parts
					Baby.Parts.Add(Part(ERole::Static, FVector(1, 10, 0), { Box(0, 13, -5, -3, -7, 8, 6, 14) }));  // body
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-1.4f, 6.5f, 5.4f), { Box(0, 33, -2.5f, -6.5f, -1.5f, 3, 8, 3) }));  // body/right_front_leg
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-1.4f, 6.5f, -5.4f), { Box(0, 44, -2.5f, -6.5f, -1.5f, 3, 8, 3) }));  // body/right_hind_leg
					Baby.Parts.Add(Part(ERole::Static, FVector(1, 11.5f, -6.5f), { Box(24, 33, -2.5f, -2, -7.5f, 3, 3, 8) }, -0.7418f, 0, 0));  // body/tail/tail_r1
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(3.25f, 6.5f, -5.25f), { Box(12, 44, -2.5f, -6.5f, -1.5f, 3, 8, 3) }));  // body/left_hind_leg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(3.4f, 6.5f, 5.3f), { Box(12, 33, -2.5f, -6.5f, -1.5f, 3, 8, 3) }));  // body/left_front_leg
					Models.Add(Baby.Kind, Baby);
				}
				{
					FModel Baby = Young(TEXT("minecraft:mule"), 64, 64);
					Baby.Scale = 1.0f;
					Baby.Parts.Add(Part(ERole::Head, FVector(1, 13, 5), { Posed(Box(0, 0, -4, 5.6f, 1.4f, 6, 4, 9), FVector(0, 6, 2), 0.3927f, 0, 0), Posed(Box(22, 0, -4, 8, 3.3f, 2, 7, 1), FVector(-2, 8.5f, 4), 0.48f, 0, 0.48f), Posed(Box(0, 0, 0, 8, 3.3f, 2, 7, 1), FVector(2, 8.5f, 4), 0.48f, 0, -0.48f), Posed(Box(30, 9, -3, -2, -1, 4, 8, 4), FVector(0, 0, 0), 0.3927f, 0, 0) }));  // body/head_parts
					Baby.Parts.Add(Part(ERole::Static, FVector(1, 10, 0), { Box(0, 13, -5, -3, -7, 8, 6, 14) }));  // body
					Baby.Parts.Add(Part(ERole::LegFrontRight, FVector(-1.4f, 6.5f, 5.4f), { Box(0, 33, -2.5f, -6.5f, -1.5f, 3, 8, 3) }));  // body/right_front_leg
					Baby.Parts.Add(Part(ERole::LegBackRight, FVector(-1.4f, 6.5f, -5.4f), { Box(0, 44, -2.5f, -6.5f, -1.5f, 3, 8, 3) }));  // body/right_hind_leg
					Baby.Parts.Add(Part(ERole::Static, FVector(1, 11.5f, -6.5f), { Box(24, 33, -2.5f, -2, -7.5f, 3, 3, 8) }, -0.7418f, 0, 0));  // body/tail/tail_r1
					Baby.Parts.Add(Part(ERole::LegBackLeft, FVector(3.25f, 6.5f, -5.25f), { Box(12, 44, -2.5f, -6.5f, -1.5f, 3, 8, 3) }));  // body/left_hind_leg
					Baby.Parts.Add(Part(ERole::LegFrontLeft, FVector(3.4f, 6.5f, 5.3f), { Box(12, 33, -2.5f, -6.5f, -1.5f, 3, 8, 3) }));  // body/left_front_leg
					Models.Add(Baby.Kind, Baby);
				}
				// Baby models of the missing mobs, batch 1 (2026-11-05): boxes from the reference's logged babyModel (models_b1.txt); pictures are the grown one's names with _baby.
				// Not logged: baby tropical fish, parrot, fish and tadpole (no baby in the game).
				const auto AddNewMobBabies = [&Models, &Young]()
				{
				{
					FModel Moo = Young(TEXT("minecraft:mooshroom"), 64, 64);
					Moo.Parts.Add(Part(ERole::Head, FVector(0.0f, 10.431f, 5.1667f), { Box(0, 18, -3.0f, -1.431f, -0.167f, 6, 6, 5), Box(8, 29, 3.0f, 3.569f, 2.833f, 1, 2, 1), Box(4, 29, -4.0f, 3.569f, 2.833f, 1, 2, 1), Box(12, 29, -2.0f, -1.431f, 4.833f, 4, 3, 1) }));  // head
					Moo.Parts.Add(Part(ERole::Static, FVector(3.0f, 5.0f, 5.0f), { Box(0, 0, -7.0f, 1.0f, -11.0f, 8, 6, 12) }));  // body
					Moo.Parts.Add(Part(ERole::LegBackRight, FVector(-2.5f, 6.0f, -3.5f), { Box(22, 27, -1.5f, -6.0f, -1.5f, 3, 6, 3) }));  // rightHindLeg
					Moo.Parts.Add(Part(ERole::LegBackLeft, FVector(2.5f, 6.0f, -3.5f), { Box(34, 27, -1.5f, -6.0f, -1.5f, 3, 6, 3) }));  // leftHindLeg
					Moo.Parts.Add(Part(ERole::LegFrontRight, FVector(-2.5f, 6.0f, 3.5f), { Box(22, 18, -1.5f, -6.0f, -1.5f, 3, 6, 3) }));  // rightFrontLeg
					Moo.Parts.Add(Part(ERole::LegFrontLeft, FVector(2.5f, 6.0f, 3.5f), { Box(34, 18, -1.5f, -6.0f, -1.5f, 3, 6, 3) }));  // leftFrontLeg
					Models.Add(Moo.Kind, Moo);
				}
				{
					FModel Oce = Young(TEXT("minecraft:ocelot"), 32, 32);
					Oce.Parts.Add(Part(ERole::LegBackLeft, FVector(1.0f, 2.0f, -2.5f), { Box(18, 22, -0.5f, -2.0f, -1.0f, 1, 2, 2) }));  // leftHindLeg
					Oce.Parts.Add(Part(ERole::LegBackRight, FVector(-1.0f, 2.0f, -2.5f), { Box(12, 22, -0.5f, -2.0f, -1.0f, 1, 2, 2) }));  // rightHindLeg
					Oce.Parts.Add(Part(ERole::LegFrontLeft, FVector(1.0f, 2.0f, 1.5f), { Box(18, 18, -0.5f, -2.0f, -1.0f, 1, 2, 2) }));  // leftFrontLeg
					Oce.Parts.Add(Part(ERole::LegFrontRight, FVector(-1.0f, 2.0f, 1.5f), { Box(12, 18, -0.5f, -2.0f, -1.0f, 1, 2, 2) }));  // rightFrontLeg
					Oce.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.893f, -3.9151f), { Box(0, 18, -0.5f, -0.893f, -5.085f, 1, 1, 5) }, -0.5672f, 0.0f, 0.0f));  // tail1
					Oce.Parts.Add(Part(ERole::Head, FVector(0.0f, 4.0f, 3.125f), { Box(0, 0, -2.5f, -1.0f, -1.125f, 5, 4, 4), Box(18, 0, -2.0f, 3.0f, -1.125f, 1, 1, 2), Box(24, 0, 1.0f, 3.0f, -1.125f, 1, 1, 2), Box(18, 3, -1.5f, -1.0f, 2.875f, 3, 2, 1) }));  // head
					Oce.Parts.Add(Part(ERole::Static, FVector(0.0f, 3.5f, -0.5f), { Box(0, 8, -2.0f, -1.5f, -3.5f, 4, 3, 7) }));  // body
					Models.Add(Oce.Kind, Oce);
				}
				{
					FModel Bear = Young(TEXT("minecraft:polar_bear"), 64, 64);
					Bear.Parts.Add(Part(ERole::Head, FVector(0.0f, 5.375f, 5.75f), { Box(0, 0, -3.0f, -2.375f, 0.25f, 6, 5, 4), Box(20, 3, -2.0f, -2.375f, 4.25f, 4, 2, 2), Box(20, 0, -4.0f, 1.625f, 1.75f, 2, 2, 1), Box(26, 0, 2.0f, 1.625f, 1.75f, 2, 2, 1) }));  // head
					Bear.Parts.Add(Part(ERole::Static, FVector(0.0f, 6.5f, 0.0f), { Box(0, 9, -4.0f, -3.5f, -6.0f, 8, 7, 12) }));  // body
					Bear.Parts.Add(Part(ERole::LegBackRight, FVector(-2.5f, 2.5f, -4.5f), { Box(0, 34, -1.5f, -2.5f, -1.5f, 3, 3, 3) }));  // rightHindLeg
					Bear.Parts.Add(Part(ERole::LegBackLeft, FVector(2.5f, 2.5f, -4.5f), { Box(12, 34, -1.5f, -2.5f, -1.5f, 3, 3, 3) }));  // leftHindLeg
					Bear.Parts.Add(Part(ERole::LegFrontRight, FVector(-2.5f, 2.5f, 4.5f), { Box(0, 28, -1.5f, -2.5f, -1.5f, 3, 3, 3) }));  // rightFrontLeg
					Bear.Parts.Add(Part(ERole::LegFrontLeft, FVector(2.5f, 2.5f, 4.5f), { Box(12, 28, -1.5f, -2.5f, -1.5f, 3, 3, 3) }));  // leftFrontLeg
					Models.Add(Bear.Kind, Bear);
				}
				{
					FModel Pan = Young(TEXT("minecraft:panda"), 64, 64);
					Pan.Parts.Add(Part(ERole::Head, FVector(0.0f, 5.0f, 3.0f), { Box(0, 0, -3.5f, -3.0f, 0.0f, 7, 6, 5), Box(24, 6, -2.0f, -3.0f, 5.0f, 4, 2, 1), Box(24, 0, -4.5f, 1.0f, 2.5f, 3, 3, 1), Box(33, 0, 1.5f, 1.0f, 2.5f, 3, 3, 1) }));  // head
					Pan.Parts.Add(Part(ERole::Static, FVector(0.0f, 5.5f, -2.5f), { Box(0, 11, -4.5f, -3.5f, -5.5f, 9, 7, 11) }));  // body
					Pan.Parts.Add(Part(ERole::LegBackRight, FVector(-3.0f, 2.0f, -6.5f), { Box(0, 34, -1.5f, -2.0f, -1.5f, 3, 2, 3) }));  // rightHindLeg
					Pan.Parts.Add(Part(ERole::LegBackLeft, FVector(3.0f, 2.0f, -6.5f), { Box(12, 34, -1.5f, -2.0f, -1.5f, 3, 2, 3) }));  // leftHindLeg
					Pan.Parts.Add(Part(ERole::LegFrontRight, FVector(-3.0f, 2.0f, 1.5f), { Box(0, 29, -1.5f, -2.0f, -1.5f, 3, 2, 3) }));  // rightFrontLeg
					Pan.Parts.Add(Part(ERole::LegFrontLeft, FVector(3.0f, 2.0f, 1.5f), { Box(12, 29, -1.5f, -2.0f, -1.5f, 3, 2, 3) }));  // leftFrontLeg
					Models.Add(Pan.Kind, Pan);
				}
				{
					FModel Sq = Young(TEXT("minecraft:squid"), 32, 32);
					Sq.Parts.Add(Part(ERole::Static, FVector(3.0f, 5.5f, 0.0f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, -1.5708f, 0.0f));  // root/tentacle0
					Sq.Parts.Add(Part(ERole::Static, FVector(2.1213f, 5.5f, -2.1213f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, -0.7854f, 0.0f));  // root/tentacle1
					Sq.Parts.Add(Part(ERole::Static, FVector(0.0f, 5.5f, 3.0f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 3.1416f, 0.0f));  // root/tentacle6
					Sq.Parts.Add(Part(ERole::Static, FVector(2.1213f, 5.5f, 2.1213f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 3.927f, 0.0f));  // root/tentacle7
					Sq.Parts.Add(Part(ERole::Static, FVector(0.0f, 11.0f, 0.0f), { Box(0, 0, -4.0f, -5.0f, -4.0f, 8, 10, 8) }));  // root/body
					Sq.Parts.Add(Part(ERole::Static, FVector(-3.0f, 5.5f, 0.0f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 1.5708f, 0.0f));  // root/tentacle4
					Sq.Parts.Add(Part(ERole::Static, FVector(-2.1213f, 5.5f, 2.1213f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 2.3562f, 0.0f));  // root/tentacle5
					Sq.Parts.Add(Part(ERole::Static, FVector(0.0f, 5.5f, -3.0f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }));  // root/tentacle2
					Sq.Parts.Add(Part(ERole::Static, FVector(-2.1213f, 5.5f, -2.1213f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 0.7854f, 0.0f));  // root/tentacle3
					Models.Add(Sq.Kind, Sq);
				}
				{
					FModel GSq = Young(TEXT("minecraft:glow_squid"), 32, 32);
					GSq.Parts.Add(Part(ERole::Static, FVector(3.0f, 5.5f, 0.0f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, -1.5708f, 0.0f));  // root/tentacle0
					GSq.Parts.Add(Part(ERole::Static, FVector(2.1213f, 5.5f, -2.1213f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, -0.7854f, 0.0f));  // root/tentacle1
					GSq.Parts.Add(Part(ERole::Static, FVector(0.0f, 5.5f, 3.0f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 3.1416f, 0.0f));  // root/tentacle6
					GSq.Parts.Add(Part(ERole::Static, FVector(2.1213f, 5.5f, 2.1213f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 3.927f, 0.0f));  // root/tentacle7
					GSq.Parts.Add(Part(ERole::Static, FVector(0.0f, 11.0f, 0.0f), { Box(0, 0, -4.0f, -5.0f, -4.0f, 8, 10, 8) }));  // root/body
					GSq.Parts.Add(Part(ERole::Static, FVector(-3.0f, 5.5f, 0.0f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 1.5708f, 0.0f));  // root/tentacle4
					GSq.Parts.Add(Part(ERole::Static, FVector(-2.1213f, 5.5f, 2.1213f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 2.3562f, 0.0f));  // root/tentacle5
					GSq.Parts.Add(Part(ERole::Static, FVector(0.0f, 5.5f, -3.0f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }));  // root/tentacle2
					GSq.Parts.Add(Part(ERole::Static, FVector(-2.1213f, 5.5f, -2.1213f), { Box(0, 18, -1.0f, -5.5f, -1.0f, 2, 6, 2) }, 0.0f, 0.7854f, 0.0f));  // root/tentacle3
					Models.Add(GSq.Kind, GSq);
				}
				{
					FModel Axo = Young(TEXT("minecraft:axolotl"), 32, 32);
					Axo.Parts.Add(Part(ERole::Static, FVector(0.0f, 1.25f, -1.75f), { Box(0, 0, -2.0f, -1.25f, -3.25f, 4, 2, 6), Box(0, 12, 0.0f, -1.25f, -2.25f, 0, 3, 5) }));  // root/root/body
					Axo.Parts.Add(Part(ERole::Head, FVector(0.0f, 1.0f, 1.0f), { Box(0, 8, -3.0f, -1.0f, 0.0f, 6, 3, 4) }));  // root/root/body/head
					Axo.Parts.Add(Part(ERole::Static, FVector(0.0f, 3.0f, 3.0f), { Box(20, 0, -3.0f, 0.0f, 0.0f, 6, 3, 0) }));  // root/root/body/head/top_gills
					Axo.Parts.Add(Part(ERole::Static, FVector(3.0f, 1.5f, 3.0f), { Box(20, 8, 0.0f, -1.5f, 0.0f, 3, 5, 0) }));  // root/root/body/head/left_gills
					Axo.Parts.Add(Part(ERole::Static, FVector(-3.0f, 1.5f, 3.0f), { Box(20, 3, -3.0f, -1.5f, 0.0f, 3, 5, 0) }));  // root/root/body/head/right_gills
					Axo.Parts.Add(Part(ERole::Static, FVector(-2.0f, 1.0f, -0.5f), { Box(20, 16, -3.0f, 0.0f, -0.5f, 3, 0, 1) }));  // root/root/body/right_front_leg
					Axo.Parts.Add(Part(ERole::Static, FVector(-2.0f, 1.0f, -3.5f), { Box(20, 14, 0.0f, 0.0f, -0.5f, 3, 0, 1) }, -1.5708f, -1.5708f, -1.5708f));  // root/root/body/right_hind_leg/right_leg_r1
					Axo.Parts.Add(Part(ERole::Static, FVector(0.0f, 1.5f, -5.0f), { Box(10, 9, 0.0f, -1.5f, -7.0f, 0, 3, 8) }));  // root/root/body/tail
					Axo.Parts.Add(Part(ERole::Static, FVector(2.0f, 1.0f, -3.5f), { Box(20, 14, 0.0f, 0.0f, -0.5f, 3, 0, 1) }));  // root/root/body/left_hind_leg
					Axo.Parts.Add(Part(ERole::Static, FVector(2.0f, 1.0f, -0.5f), { Box(20, 13, 0.0f, 0.0f, -0.5f, 3, 0, 1) }));  // root/root/body/left_front_leg
					Models.Add(Axo.Kind, Axo);
				}
				{
					FModel Arm = Young(TEXT("minecraft:armadillo"), 64, 64);
					Arm.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.0f, -0.5f), { Box(0, 0, -2.5f, -2.0f, -3.5f, 5, 4, 7), Box(0, 11, -2.5f, -2.0f, -3.0f, 5, 4, 6) }));  // body
					Arm.Parts.Add(Part(ERole::Static, FVector(0.0f, 4.0f, 2.7f), { Box(20, 17, -1.0f, 0.0f, 0.0f, 2, 2, 4) }, 0.7418f, 0.0f, 0.0f));  // body/head/head_cube
					Arm.Parts.Add(Part(ERole::Static, FVector(-1.0f, 6.0f, 3.0f), { Box(28, 8, -1.8f, -1.0f, 0.0f, 2, 3, 0) }, -0.4363f, 0.1134f, -0.0524f));  // body/head/head_cube/right_ear
					Arm.Parts.Add(Part(ERole::Static, FVector(1.0f, 6.0f, 3.0f), { Box(28, 8, -0.2f, -1.0f, 0.0f, 2, 3, 0, 0.0f, true) }, -0.4363f, -0.1134f, 0.0524f));  // body/head/head_cube/left_ear
					Arm.Parts.Add(Part(ERole::Static, FVector(0.0f, 2.5f, -4.9f), { Box(22, 11, -0.5f, -0.5f, -2.0f, 1, 1, 4) }, -1.0472f, 0.0f, 0.0f));  // body/tail/right_ear_cube
					Arm.Parts.Add(Part(ERole::LegBackRight, FVector(-1.5f, 2.0f, -2.5f), { Box(20, 27, -1.0f, -2.0f, -1.0f, 2, 2, 2) }));  // rightHindLeg
					Arm.Parts.Add(Part(ERole::LegBackLeft, FVector(1.5f, 2.0f, -2.5f), { Box(20, 27, -1.0f, -2.0f, -1.0f, 2, 2, 2) }));  // leftHindLeg
					Arm.Parts.Add(Part(ERole::Static, FVector(0.0f, 3.3f, -0.5f), { Box(0, 25, -3.0f, -3.0f, -3.0f, 6, 6, 6) }));  // cube
					Arm.Parts.Add(Part(ERole::Static, FVector(1.5f, 2.0f, 1.5f), { Box(20, 23, -1.0f, -2.0f, -1.0f, 2, 2, 2) }));  // root/right_front_leg
					Arm.Parts.Add(Part(ERole::Static, FVector(-1.5f, 2.0f, 1.5f), { Box(24, 0, -1.0f, -2.0f, -1.0f, 2, 2, 2) }));  // root/left_front_leg
					Models.Add(Arm.Kind, Arm);
				}
				{
					FModel Zog = Young(TEXT("minecraft:zoglin"), 64, 64);
					Zog.Parts.Add(Part(ERole::Head, FVector(0.0f, 11.0f, 7.0f), { Box(0, 0, -5.0f, -1.74f, -1.453f, 10, 4, 12), Box(44, 29, -7.0f, -0.902f, 6.488f, 2, 5, 2), Box(52, 29, 5.0f, -0.902f, 6.488f, 2, 5, 2) }, 0.8727f, 0.0f, 0.0f));  // head
					Zog.Parts.Add(Part(ERole::Static, FVector(-5.0f, 12.0f, 8.5f), { Box(32, 5, -5.1f, -0.5f, -2.0f, 6, 1, 4) }, 0.0f, 0.0f, 0.8727f));  // head/right_ear
					Zog.Parts.Add(Part(ERole::Static, FVector(5.0f, 12.0f, 8.5f), { Box(32, 0, -0.9f, -0.5f, -2.0f, 6, 1, 4) }, 0.0f, 0.0f, -0.8727f));  // head/left_ear
					Zog.Parts.Add(Part(ERole::LegFrontRight, FVector(-2.5f, 6.0f, 4.5f), { Box(0, 38, -1.5f, -6.0f, -1.5f, 3, 6, 3) }));  // rightFrontLeg
					Zog.Parts.Add(Part(ERole::LegFrontLeft, FVector(2.5f, 6.0f, 4.5f), { Box(12, 38, -1.5f, -6.0f, -1.5f, 3, 6, 3) }));  // leftFrontLeg
					Zog.Parts.Add(Part(ERole::LegBackRight, FVector(-2.5f, 6.0f, -4.5f), { Box(0, 47, -1.5f, -6.0f, -1.5f, 3, 6, 3) }));  // rightHindLeg
					Zog.Parts.Add(Part(ERole::LegBackLeft, FVector(2.5f, 6.0f, -4.5f), { Box(12, 47, -1.5f, -6.0f, -1.5f, 3, 6, 3) }));  // leftHindLeg
					Zog.Parts.Add(Part(ERole::Static, FVector(0.0f, 0.0f, 0.0f), { Box(0, 16, -4.0f, 6.0f, -7.0f, 8, 8, 14), Box(24, 39, 0.0f, 12.0f, -3.0f, 0, 6, 11) }));  // root/body
					Models.Add(Zog.Kind, Zog);
				}
				{
					FModel Gha = Young(TEXT("minecraft:happy_ghast"), 64, 64);
					Gha.Parts.Add(Part(ERole::Static, FVector(0.0f, 6.7992f, 0.0f), { Box(0, 0, -8.0f, -8.0f, -8.0f, 16, 16, 16) }));  // body
					Gha.Parts.Add(Part(ERole::Static, FVector(-3.75f, -0.2008f, 5.0f), { Box(0, 0, -1.0f, -5.0f, -1.0f, 2, 5, 2) }));  // body/tentacle0
					Gha.Parts.Add(Part(ERole::Static, FVector(1.25f, -0.2008f, 5.0f), { Box(0, 0, -1.0f, -7.0f, -1.0f, 2, 7, 2) }));  // body/tentacle1
					Gha.Parts.Add(Part(ERole::Static, FVector(0.0f, -1.2008f, 0.0f), { Box(0, 32, -8.0f, 0.0f, -8.0f, 16, 16, 16) }));  // body/inner_body
					Gha.Parts.Add(Part(ERole::Static, FVector(6.25f, -0.2008f, -5.0f), { Box(0, 0, -1.0f, -5.0f, -1.0f, 2, 5, 2) }));  // body/tentacle8
					Gha.Parts.Add(Part(ERole::Static, FVector(-3.75f, -0.2008f, -5.0f), { Box(0, 0, -1.0f, -8.0f, -1.0f, 2, 8, 2) }));  // body/tentacle6
					Gha.Parts.Add(Part(ERole::Static, FVector(1.25f, -0.2008f, -5.0f), { Box(0, 0, -1.0f, -8.0f, -1.0f, 2, 8, 2) }));  // body/tentacle7
					Gha.Parts.Add(Part(ERole::Static, FVector(-1.25f, -0.2008f, 0.0f), { Box(0, 0, -1.0f, -5.0f, -1.0f, 2, 5, 2) }));  // body/tentacle4
					Gha.Parts.Add(Part(ERole::Static, FVector(3.75f, -0.2008f, 0.0f), { Box(0, 0, -1.0f, -7.0f, -1.0f, 2, 7, 2) }));  // body/tentacle5
					Gha.Parts.Add(Part(ERole::Static, FVector(6.25f, -0.2008f, 5.0f), { Box(0, 0, -1.0f, -4.0f, -1.0f, 2, 4, 2) }));  // body/tentacle2
					Gha.Parts.Add(Part(ERole::Static, FVector(-6.25f, -0.2008f, 0.0f), { Box(0, 0, -1.0f, -5.0f, -1.0f, 2, 5, 2) }));  // body/tentacle3
					Models.Add(Gha.Kind, Gha);
				}
				};
				AddNewMobBabies();
			}
			return Models;
		}

		// Minecraft's shading: 1 from above, 0.5 from below, 0.8 from north and south, 0.6 from east and west.
		FColor Shade(const FVector& Normal, float Hurt)
		{
			const double Y = Normal.Y;
			const double Value = Normal.X * Normal.X * 0.6 + Normal.Z * Normal.Z * 0.8 + Y * Y * (Y > 0.0 ? 1.0 : 0.5);
			const auto Byte = [](double V) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(V * 255.0), 0, 255)); };
			// A hurt mob goes red: the other channels fade towards a third.
			const double Rest = FMath::Lerp(1.0, 0.3, FMath::Clamp(Hurt, 0.0f, 1.0f));
			return FColor(Byte(Value), Byte(Value * Rest), Byte(Value * Rest), 255);
		}

		// A held picture: two faces back to back, the whole of the picture on each.
		void AddFlat(const FBox& Box, const FQuat& PartRotation, const FVector& Pivot, float BodyYaw, float Hurt, float WholeScale, McPlayerModel::FMeshData& Out)
		{
			const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
			if (Box.bFlatTop)
			{
				// Corners of the picture (u right, v up) laid in the x-y plane with its diagonal along -y.
				const FVector Centre(Box.Min.X + Box.Size.X * 0.5f, Box.Min.Y + Box.Size.Y * 0.5f, Box.Min.Z);
				const FVector U(0.7071, -0.7071, 0.0);
				const FVector V(-0.7071, -0.7071, 0.0);
				const FVector2D Uv[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
				for (const bool bBack : { false, true })
				{
					const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(FVector(0.0, 0.0, bBack ? -1.0 : 1.0)));
					const FColor Color = Shade(Normal, Hurt);
					const int32 First = Out.Positions.Num();
					for (const FVector2D& Corner : Uv)
					{
						const FVector Local = Centre + Box.Size.Y * ((Corner.X - 0.5) * U + (Corner.Y - 0.5) * V);
						const FVector Model = ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(Local)) * WholeScale;
						Out.Positions.Add(FVector(Model.X, Model.Z, Model.Y) * (McCoords::UnitsPerBlock / 16.0));
						Out.Normals.Add(FVector(Normal.X, Normal.Z, Normal.Y));
						Out.UV0.Add(FVector2D(Corner.X, 1.0 - Corner.Y));
						Out.Colors.Add(Color);
					}
					if (bBack)
					{
						Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
					}
					else
					{
						Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
					}
				}
				return;
			}
			const float Y0 = Box.Min.Y;
			const float Y1 = Box.Min.Y + Box.Size.Y;
			const float Z0 = Box.Min.Z;
			const float Z1 = Box.Min.Z + Box.Size.Z;
			const FVector Corners[4] = { FVector(Box.Min.X, Y1, Z0), FVector(Box.Min.X, Y1, Z1), FVector(Box.Min.X, Y0, Z1), FVector(Box.Min.X, Y0, Z0) };
			// Hanging down from a shoulder the top of the picture is towards -y; mirrored, it stands up with its top towards +y.
			const FVector2D UVs[4] = { FVector2D(0, Box.bMirror ? 0 : 1), FVector2D(1, Box.bMirror ? 0 : 1), FVector2D(1, Box.bMirror ? 1 : 0), FVector2D(0, Box.bMirror ? 1 : 0) };
			for (const bool bBack : { false, true })
			{
				const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(FVector(bBack ? -1.0 : 1.0, 0.0, 0.0)));
				const FColor Color = Shade(Normal, Hurt);
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector Model = ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(Corners[Corner])) * WholeScale;
					Out.Positions.Add(FVector(Model.X, Model.Z, Model.Y) * (McCoords::UnitsPerBlock / 16.0));
					Out.Normals.Add(FVector(Normal.X, Normal.Z, Normal.Y));
					Out.UV0.Add(UVs[Corner]);
					Out.Colors.Add(Color);
				}
				if (bBack)
				{
					Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
				}
				else
				{
					Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
				}
			}
		}

		void AddBox(const FBox& Box, const FPart& Part, const FQuat& PartRotation, const FVector& Pivot, float BodyYaw, FIntPoint TextureSize, float Hurt, bool bGlow, float LocalScale, float WholeScale, McPlayerModel::FMeshData& Out)
		{
			const FVector Min = (Box.Min - FVector(Box.Inflate)) * LocalScale;
			const FVector Max = (Box.Min + FVector(Box.Size.X, Box.Size.Y, Box.Size.Z) + FVector(Box.Inflate)) * LocalScale;
			const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
			const FVector BendPivot = Box.BoxPivot * LocalScale;
			const auto Place = [&](const FVector& Local)
			{
				const FVector Bent = BendPivot + Box.BoxRotation.RotateVector(Local - BendPivot);
				return ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(Bent * Part.Scale)) * WholeScale;
			};

			struct FFace { EBoxFace Face; FVector Normal; FVector Corners[4]; };
			const FFace Faces[] = {
				{ EBoxFace::South, FVector(0, 0, 1),  { FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z) } },
				{ EBoxFace::North, FVector(0, 0, -1), { FVector(Max.X, Min.Y, Min.Z), FVector(Min.X, Min.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z) } },
				{ EBoxFace::East,  FVector(1, 0, 0),  { FVector(Max.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Max.Z) } },
				{ EBoxFace::West,  FVector(-1, 0, 0), { FVector(Min.X, Min.Y, Min.Z), FVector(Min.X, Min.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Min.Z) } },
				{ EBoxFace::Up,    FVector(0, 1, 0),  { FVector(Min.X, Max.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z) } },
				{ EBoxFace::Down,  FVector(0, -1, 0), { FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Max.Z) } },
			};
			for (const FFace& Face : Faces)
			{
				EBoxFace PictureFace = Face.Face;
				if (Box.bMirror)
				{
					// A mirrored box wears its twin's net backwards: east and west trade places, and every picture flips.
					PictureFace = Face.Face == EBoxFace::East ? EBoxFace::West : Face.Face == EBoxFace::West ? EBoxFace::East : Face.Face;
				}
				const FIntRect Rect = McPlayerModel::GetFaceRect(Box.UV, Box.Size, PictureFace);
				const double Left = Box.bMirror ? Rect.Max.X : Rect.Min.X;
				const double Right = Box.bMirror ? Rect.Min.X : Rect.Max.X;
				const FVector2D UVs[4] = {
					FVector2D(Left, Rect.Max.Y), FVector2D(Right, Rect.Max.Y),
					FVector2D(Right, Rect.Min.Y), FVector2D(Left, Rect.Min.Y),
				};
				const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(Box.BoxRotation.RotateVector(Face.Normal)));
				const FVector UnrealNormal(Normal.X, Normal.Z, Normal.Y);
				const FColor Color = bGlow ? FColor::White : Shade(Normal, Hurt);
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector Model = Place(Face.Corners[Corner]);
					Out.Positions.Add(FVector(Model.X, Model.Z, Model.Y) * (McCoords::UnitsPerBlock / 16.0));
					Out.Normals.Add(UnrealNormal);
					Out.UV0.Add(FVector2D(UVs[Corner].X / TextureSize.X, UVs[Corner].Y / TextureSize.Y));
					Out.Colors.Add(Color);
				}
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
		}

		// The swing of a limb at a walk position: a cosine, or for the golem a triangle wave (13 steps to a cycle) of the same reach.
		float Swing(const FModel& Model, float WalkPos, float WalkAmount, float Phase, float Reach)
		{
			if (Model.bTriangleWalk)
			{
				const float Cycle = FMath::Frac(WalkPos / 13.0f + 0.41f + Phase / (2.0f * PI));
				return (4.0f * FMath::Abs(Cycle - 0.5f) - 1.0f) * Reach * WalkAmount;
			}
			return FMath::Cos(WalkPos * 0.6662f + Phase) * Reach * WalkAmount;
		}
	}

	const FModel* Find(FName Kind)
	{
		static const TMap<FName, FModel> Models = BuildModels();
		return Models.Find(Kind);
	}

	void BuildBannerMesh(bool bWall, bool bFlag, float BodyYaw, McPlayerModel::FMeshData& Out, float Tilt)
	{
		// Pixels before the two thirds the game scales a banner by; the flag faces south (+z) and hangs from the bar.
		static const FModel Pole = []
		{
			FModel Result;
			Result.TextureSize = FIntPoint(64, 64);
			Result.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(44, 0, -1, 0, -1, 2, 42, 2), Box(0, 42, -10, 42, -1, 20, 2, 2) }));
			return Result;
		}();
		static const FModel WallBar = []
		{
			FModel Result;
			Result.TextureSize = FIntPoint(64, 64);
			Result.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 42, -10, 22, -12, 20, 2, 2) }));
			return Result;
		}();
		static const FModel Flag = []
		{
			FModel Result;
			Result.TextureSize = FIntPoint(64, 64);
			Result.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -10, 4, 1, 20, 40, 1) }));
			return Result;
		}();
		static const FModel WallFlag = []
		{
			FModel Result;
			Result.TextureSize = FIntPoint(64, 64);
			Result.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -10, -18, -10, 20, 40, 1) }));
			return Result;
		}();
		FAnim Anim;
		// The flag swings about the bar it hangs from: built unturned, tilted there, then turned like the rest.
		Anim.BodyYaw = bFlag ? 0.0f : BodyYaw;
		BuildMesh(bFlag ? (bWall ? WallFlag : Flag) : (bWall ? WallBar : Pole), Anim, 0, Out);
		for (FVector& Position : Out.Positions)
		{
			Position *= 2.0 / 3.0;
		}
		if (bFlag)
		{
			// Positive Tilt swings the foot of the flag back (away from its face).
			const FQuat Swing(FVector::XAxisVector, -Tilt);
			const FQuat Turn(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
			const FVector Pivot(0.0, (bWall ? 22.0 : 44.0) * 2.0 / 3.0, 0.0);
			// The mesh holds engine axes (x, model z, model y) in engine units; turn it in the model's own axes and pixels.
			const double Units = McCoords::UnitsPerBlock / 16.0;
			for (FVector& Position : Out.Positions)
			{
				const FVector Model(Position.X / Units, Position.Z / Units, Position.Y / Units);
				const FVector Moved = Turn.RotateVector(Pivot + Swing.RotateVector(Model - Pivot));
				Position = FVector(Moved.X, Moved.Z, Moved.Y) * Units;
			}
			for (FVector& Normal : Out.Normals)
			{
				const FVector Moved = Turn.RotateVector(Swing.RotateVector(FVector(Normal.X, Normal.Z, Normal.Y)));
				Normal = FVector(Moved.X, Moved.Z, Moved.Y);
			}
		}
	}

	float GetBannerTilt(double Ticks)
	{
		const double Phase = FMath::Fmod((Ticks + 32.0) / 100.0, 1.0);
		return 0.058f + 0.033f * static_cast<float>(FMath::Cos(2.0 * PI * Phase));
	}

	TArray<FString> GetVillagerTextures(bool bZombie, FName Variant, FName Profession, int32 Level)
	{
		const FString Folder = bZombie ? TEXT("zombie_villager/") : TEXT("villager/");
		const FString Type = Variant.IsNone() ? FString(TEXT("plains")) : Variant.ToString();
		const FString Job = Profession.IsNone() ? FString() : McCache::PathOf(Profession.ToString());
		// A jobless villager has no clothes picture, and a nitwit has clothes but no badge.
		const bool bClothes = !Job.IsEmpty() && Job != TEXT("none");
		static const TCHAR* Badges[] = { TEXT("stone"), TEXT("iron"), TEXT("gold"), TEXT("emerald"), TEXT("diamond") };
		TArray<FString> Textures;
		Textures.Add(Folder + (bZombie ? TEXT("zombie_villager.png") : TEXT("villager.png")));
		Textures.Add(Folder + TEXT("type/") + Type + TEXT(".png"));
		Textures.Add(bClothes ? Folder + TEXT("profession/") + Job + TEXT(".png") : FString());
		Textures.Add(bClothes && Job != TEXT("nitwit") ? Folder + TEXT("profession_level/") + Badges[FMath::Clamp(Level, 1, 5) - 1] + TEXT(".png") : FString());
		return Textures;
	}

	FColor GetDyeColor(int32 Index)
	{
		static const FColor Colors[16] = {
			FColor(249, 255, 254), FColor(249, 128, 29), FColor(199, 78, 189), FColor(58, 179, 218), FColor(254, 216, 61), FColor(128, 199, 31),
			FColor(243, 139, 170), FColor(71, 79, 82), FColor(157, 157, 151), FColor(22, 156, 156), FColor(137, 50, 184), FColor(60, 68, 170),
			FColor(131, 84, 50), FColor(94, 124, 22), FColor(176, 46, 38), FColor(29, 29, 33) };
		return Colors[FMath::Clamp(Index, 0, 15)];
	}

	namespace
	{
		// The way an item frame or painting faces (the game's directions: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east):
		// U runs to the right of a viewer in front of it, V up its face, N out of it, all in Minecraft's axes.
		void GetDecorBasis(int32 Facing, FVector& U, FVector& V, FVector& N)
		{
			V = FVector(0, 1, 0);
			switch (Facing)
			{
			case 0: N = FVector(0, -1, 0); U = FVector(1, 0, 0); V = FVector(0, 0, 1); break;
			case 1: N = FVector(0, 1, 0); U = FVector(1, 0, 0); V = FVector(0, 0, -1); break;
			case 2: N = FVector(0, 0, -1); U = FVector(-1, 0, 0); break;
			case 4: N = FVector(-1, 0, 0); U = FVector(0, 0, 1); break;
			case 5: N = FVector(1, 0, 0); U = FVector(0, 0, -1); break;
			default: N = FVector(0, 0, 1); U = FVector(1, 0, 0); break;
			}
		}

		// One quad, its corners bottom-left, bottom-right, top-right, top-left seen from outside, in blocks about the entity.
		void AddDecorQuad(const FVector (&Corners)[4], const FVector2D (&UVs)[4], bool bGlow, McPlayerModel::FMeshData& Out)
		{
			const FVector Normal = FVector::CrossProduct(Corners[1] - Corners[0], Corners[3] - Corners[0]).GetSafeNormal();
			const FColor Color = bGlow ? FColor::White : Shade(Normal, 0.0f);
			const int32 First = Out.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Out.Positions.Add(FVector(Corners[Corner].X, Corners[Corner].Z, Corners[Corner].Y) * McCoords::UnitsPerBlock);
				Out.Normals.Add(FVector(Normal.X, Normal.Z, Normal.Y));
				Out.UV0.Add(UVs[Corner]);
				Out.Colors.Add(Color);
			}
			Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}

		// A flat picture in the plane spanned by U (right) and V (up), centred at Centre, Width by Height blocks, showing
		// the rectangle of the texture Rect = [U0, V0, U1, V1]. Seen from behind (bBack) the right-hand side is the other one.
		void AddDecorPicture(const FVector& Centre, const FVector& U, const FVector& V, double Width, double Height, const FVector4& Rect, bool bBack, bool bGlow, McPlayerModel::FMeshData& Out)
		{
			const FVector Right = U * (Width * 0.5);
			const FVector Up = V * (Height * 0.5);
			FVector Corners[4] = { Centre - Right - Up, Centre + Right - Up, Centre + Right + Up, Centre - Right + Up };
			FVector2D UVs[4] = { FVector2D(Rect.X, Rect.W), FVector2D(Rect.Z, Rect.W), FVector2D(Rect.Z, Rect.Y), FVector2D(Rect.X, Rect.Y) };
			if (bBack)
			{
				Swap(Corners[0], Corners[1]);
				Swap(Corners[2], Corners[3]);
				Swap(UVs[0], UVs[1]);
				Swap(UVs[2], UVs[3]);
			}
			AddDecorQuad(Corners, UVs, bGlow, Out);
		}

		// A box of the item frame's model, in pixels of a block, for a frame hung on a wall to the south
		// (assets: models/block/template_item_frame.json), with each face's picture rectangle in sixteenths of its texture;
		// a face with no rectangle (-1) is not drawn. Faces in the order down, up, north, south, west, east.
		struct FFrameElement
		{
			FVector From;
			FVector To;
			bool bBack;
			int32 Faces[6][4];
		};

		const FFrameElement* GetFrameElements(int32& OutNum)
		{
			static const FFrameElement Elements[] = {
				// The back plate, a picture of its own.
				{ FVector(3, 3, 15.5), FVector(13, 13, 16), true, {
					{ -1, -1, -1, -1 }, { -1, -1, -1, -1 }, { 3, 3, 13, 13 }, { 3, 3, 13, 13 }, { -1, -1, -1, -1 }, { -1, -1, -1, -1 } } },
				// The wood: bottom, top, left, right.
				{ FVector(2, 2, 15), FVector(14, 3, 16), false, {
					{ 2, 0, 14, 1 }, { 2, 15, 14, 16 }, { 2, 13, 14, 14 }, { 2, 13, 14, 14 }, { 15, 13, 16, 14 }, { 0, 13, 1, 14 } } },
				{ FVector(2, 13, 15), FVector(14, 14, 16), false, {
					{ 2, 0, 14, 1 }, { 2, 15, 14, 16 }, { 2, 2, 14, 3 }, { 2, 2, 14, 3 }, { 15, 2, 16, 3 }, { 0, 2, 1, 3 } } },
				{ FVector(2, 3, 15), FVector(3, 13, 16), false, {
					{ -1, -1, -1, -1 }, { -1, -1, -1, -1 }, { 13, 3, 14, 13 }, { 2, 3, 3, 13 }, { 15, 3, 16, 13 }, { 0, 3, 1, 13 } } },
				{ FVector(13, 3, 15), FVector(14, 13, 16), false, {
					{ -1, -1, -1, -1 }, { -1, -1, -1, -1 }, { 2, 3, 3, 13 }, { 13, 3, 14, 13 }, { 15, 3, 16, 13 }, { 0, 3, 1, 13 } } },
			};
			OutNum = UE_ARRAY_COUNT(Elements);
			return Elements;
		}
	}

	int32 GetDecorLayers(const McDecor::FDecor& Decor)
	{
		switch (Decor.Kind)
		{
		case McDecor::EKind::Painting: return 2;
		case McDecor::EKind::ArmorStand: return 11;
		default: return Decor.Item.IsEmpty() ? 2 : 3;
		}
	}

	// One armor slot of a stand (layer 1 head, 2 chest, 3 legs, 4 feet): the armor model's parts as the game's model measured
	// them (pivot and cubes in its y-down axes), each turned by the pose of the stand part it covers. A small stand wears
	// them at half size.
	void BuildStandArmor(const McDecor::FDecor& Decor, int32 Layer, McPlayerModel::FMeshData& Out)
	{
		// Layers 5 to 8 are the leather overlay of slots 1 to 4: the same cubes, a hair larger.
		const float Over = Layer > 4 ? 0.02f : 0.0f;
		Layer = Layer > 4 ? Layer - 4 : Layer;
		const float Scale = Decor.bSmall ? 0.5f : 1.0f;
		const auto Make = [&](McDecor::EPart Pose, FVector Pivot, std::initializer_list<FBox> Boxes) -> FPart
		{
			FPart Result;
			const float Down = Decor.bSmall ? 12.0f + Pivot.Y * Scale : Pivot.Y;
			Result.Pivot = FVector(Pivot.X * Scale, 24.0 - Down, -Pivot.Z);
			Result.Scale = Scale;
			Result.Boxes = Boxes;
			for (FBox& Box : Result.Boxes)
			{
				Box.Inflate += Over;
			}
			const FVector3f Angles = Decor.Pose[static_cast<int32>(Pose)];
			Result.RestPitch = FMath::DegreesToRadians(Angles.X);
			Result.RestYaw = -FMath::DegreesToRadians(Angles.Y);
			Result.RestRoll = FMath::DegreesToRadians(Angles.Z);
			return Result;
		};
		FModel Armor;
		Armor.TextureSize = FIntPoint(64, 32);
		switch (Layer)
		{
		case 1:
			Armor.Parts.Add(Make(McDecor::EPart::Head, FVector(0, 1, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8, 1.0f), Box(32, 0, -4, 0, -4, 8, 8, 8, 1.5f) }));
			break;
		case 2:
			Armor.Parts.Add(Make(McDecor::EPart::Body, FVector(0, 0, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4, 1.0f) }));
			Armor.Parts.Add(Make(McDecor::EPart::RightArm, FVector(-5, 2, 0), { Box(40, 16, -3, -10, -2, 4, 12, 4, 1.0f) }));
			Armor.Parts.Add(Make(McDecor::EPart::LeftArm, FVector(5, 2, 0), { Box(40, 16, -1, -10, -2, 4, 12, 4, 1.0f, true) }));
			break;
		case 3:
			Armor.Parts.Add(Make(McDecor::EPart::Body, FVector(0, 0, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4, 0.5f) }));
			Armor.Parts.Add(Make(McDecor::EPart::RightLeg, FVector(-1.9f, 11, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4, 0.4f) }));
			Armor.Parts.Add(Make(McDecor::EPart::LeftLeg, FVector(1.9f, 11, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4, 0.4f, true) }));
			break;
		default:
			Armor.Parts.Add(Make(McDecor::EPart::RightLeg, FVector(-1.9f, 11, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4, 0.9f) }));
			Armor.Parts.Add(Make(McDecor::EPart::LeftLeg, FVector(1.9f, 11, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4, 0.9f, true) }));
			break;
		}
		FAnim Anim;
		Anim.BodyYaw = Decor.Yaw;
		Anim.HeadYaw = Decor.Yaw;
		BuildMesh(Armor, Anim, 0, Out);
	}

	// What a stand holds (layer 9 the main hand, 10 the off hand): a flat item's picture stood one pixel thick, placed from its
	// model's thirdperson display at the end of the arm that holds it, and turned with that arm's pose. The fit of the hold
	// (offset from the arm's tip in pixels, turn in degrees) is -McStandHeld=hx,hy,hz,ex,ey,ez while it is being measured.
	void BuildStandHeld(const McDecor::FDecor& Decor, int32 Layer, McPlayerModel::FMeshData& Out)
	{
		const bool bMain = Layer == 9;
		const FString& Item = Decor.Equipment[bMain ? 4 : 5];
		if (Item.IsEmpty())
		{
			return;
		}
		const McHand::FHeldPictures Pictures = McHand::FindHeldPictures(FName(*Item));
		const bool bCube = Pictures.Kind == McHand::EHeld::Cube;
		McPlayerModel::FMeshData Flat;
		if (bCube)
		{
			// A full block: its sides and top, both from the side picture.
			McPlayerModel::FMeshData Top;
			McHand::BuildCubeSidesMesh(FColor::White, 1.0f, Flat);
			McHand::BuildCubeTopMesh(FColor::White, 1.0f, Top);
			const int32 First = Flat.Positions.Num();
			Flat.Positions.Append(Top.Positions);
			Flat.Normals.Append(Top.Normals);
			Flat.UV0.Append(Top.UV0);
			Flat.Colors.Append(Top.Colors);
			for (const int32 Index : Top.Triangles)
			{
				Flat.Triangles.Add(Index + First);
			}
		}
		else
		{
			TArray<FColor> Pixels;
			int32 Width = 0, Height = 0;
			if (Pictures.Kind != McHand::EHeld::Flat || !McRenderTextures::LoadPngPixels(Pictures.Side, Pixels, Width, Height) || Width <= 0 || Height < Width)
			{
				return;
			}
			McHand::BuildExtrudedMesh(Pixels, Width, Height > Width ? static_cast<float>(Width) / Height : 1.0f, Flat);
		}
		McHand::FHeldDisplay Display = McHand::ReadHeldDisplay(bCube ? FName(TEXT("block/block")) : FName(*Item), bMain ? TEXT("thirdperson_righthand") : TEXT("thirdperson_lefthand"));
		// A model with no left hand display has the right one turned over (the left hand mirrors the right).
		static FString ModeText;
		static bool bModeRead = FParse::Value(FCommandLine::Get(), TEXT("-McStandLeft="), ModeText);
		const int32 LeftMode = ModeText.IsEmpty() ? 1 : FCString::Atoi(*ModeText);
		const bool bMirror = !bMain && (LeftMode == 1 || (LeftMode == 0 && !Display.bFound));
		if (!bMain && LeftMode > 0)
		{
			Display = McHand::ReadHeldDisplay(FName(*Item), TEXT("thirdperson_righthand"));
		}

		static FString FitText;
		static bool bFitRead = FParse::Value(FCommandLine::Get(), TEXT("-McStandHeld="), FitText, false);
		TArray<FString> FitParts;
		FitText.ParseIntoArray(FitParts, TEXT(","));
		const auto Fit = [&](int32 Index, float Default) { return FitParts.IsValidIndex(Index) ? FCString::Atof(*FitParts[Index]) : Default; };
		const FVector Hand(Fit(0, 0.0f), -10.0f + Fit(1, -2.5f), Fit(2, -1.6f));
		const FQuat Hold = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Fit(4, 180.0f))) * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Fit(5, 0.0f)))
			* FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Fit(3, -90.0f)));
		// A model's display turn, as the first-person hand applies it (matched to the game there): about z, then y, then x, in the
		// item model's frame (x right, y up, z toward the viewer).
		const double XSign = Fit(6, -1.0f);
		const auto Turn = [&Display, XSign](const FVector& V)
		{
			const auto Rad = [](double Degrees) { return FMath::DegreesToRadians(Degrees); };
			const FVector AfterZ(V.X * FMath::Cos(Rad(Display.Rotation.Z)) - V.Y * FMath::Sin(Rad(Display.Rotation.Z)), V.X * FMath::Sin(Rad(Display.Rotation.Z)) + V.Y * FMath::Cos(Rad(Display.Rotation.Z)), V.Z);
			const FVector AfterY(AfterZ.X * FMath::Cos(Rad(Display.Rotation.Y)) + AfterZ.Z * FMath::Sin(Rad(Display.Rotation.Y)), AfterZ.Y, -AfterZ.X * FMath::Sin(Rad(Display.Rotation.Y)) + AfterZ.Z * FMath::Cos(Rad(Display.Rotation.Y)));
			const double X = Rad(Display.Rotation.X) * XSign;
			return FVector(AfterY.X, AfterY.Y * FMath::Cos(X) - AfterY.Z * FMath::Sin(X), AfterY.Y * FMath::Sin(X) + AfterY.Z * FMath::Cos(X));
		};

		const McDecor::EPart Arm = bMain ? McDecor::EPart::RightArm : McDecor::EPart::LeftArm;
		const float Scale = Decor.bSmall ? 0.5f : 1.0f;
		const FVector3f Angles = Decor.Pose[static_cast<int32>(Arm)];
		const FQuat PartRotation = FQuat(FVector::YAxisVector, -FMath::DegreesToRadians(Angles.Y)) * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Angles.Z))
			* FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Angles.X));
		const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(Decor.Yaw));
		const float Down = Decor.bSmall ? 13.0f : 2.0f;
		const FVector Pivot((bMain ? -5.0f : 5.0f) * Scale, 24.0f - Down, 0.0f);

		Out = Flat;
		for (int32 Index = 0; Index < Flat.Positions.Num(); ++Index)
		{
			// The mesh (x away, y right, z up, in blocks) into the item model's frame (x right, y up, z toward the viewer), in pixels.
			const FVector Mesh = Flat.Positions[Index] / McCoords::UnitsPerBlock;
			const FVector Model = FVector(Mesh.Y, Mesh.Z, -Mesh.X) * 16.0 * Display.Scale;
			FVector InArm = Hand + Hold.RotateVector(Turn(Model) + Display.Translation);
			if (bMirror)
			{
				InArm.X = -InArm.X;
			}
			const FVector Placed = ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(InArm * Scale));
			Out.Positions[Index] = FVector(Placed.X, Placed.Z, Placed.Y) * (McCoords::UnitsPerBlock / 16.0);
			const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(Hold.RotateVector(Turn(FVector(Flat.Normals[Index].Y, Flat.Normals[Index].Z, -Flat.Normals[Index].X)))));
			Out.Normals[Index] = FVector(Normal.X, Normal.Z, Normal.Y);
		}
	}

	void BuildDecorMesh(const McDecor::FDecor& Decor, int32 Layer, FVector2D PictureBlocks, float ItemVMax, McPlayerModel::FMeshData& Out)
	{
		FVector U, V, N;
		GetDecorBasis(Decor.Facing, U, V, N);
		const bool bGlow = Decor.Kind == McDecor::EKind::GlowItemFrame;
		if (Decor.Kind == McDecor::EKind::Painting)
		{
			// A thin slab about the entity: its picture on the front (layer 0), its back and edges plain (layer 1).
			const double Thickness = 1.0 / 16.0;
			const FVector Front = N * (Thickness * 0.5);
			const FVector4 Whole(0, 0, 1, 1);
			if (Layer == 0)
			{
				AddDecorPicture(Front, U, V, PictureBlocks.X, PictureBlocks.Y, Whole, false, false, Out);
			}
			else
			{
				AddDecorPicture(-Front, U, V, PictureBlocks.X, PictureBlocks.Y, Whole, true, false, Out);
				AddDecorPicture(U * (PictureBlocks.X * 0.5), N, V, Thickness, PictureBlocks.Y, Whole, false, false, Out);
				AddDecorPicture(-U * (PictureBlocks.X * 0.5), N, V, Thickness, PictureBlocks.Y, Whole, true, false, Out);
				AddDecorPicture(V * (PictureBlocks.Y * 0.5), U, N, PictureBlocks.X, Thickness, Whole, false, false, Out);
				AddDecorPicture(-V * (PictureBlocks.Y * 0.5), U, N, PictureBlocks.X, Thickness, Whole, true, false, Out);
			}
			return;
		}
		if (Decor.Kind == McDecor::EKind::ArmorStand)
		{
			// The stand's parts as the game's model measured them (pivot and cubes in its y-down, z-back axes; here y up and
			// z forward), a small stand's parts scaled about their own pivots. Poses turn a part by the game's angles.
			if (Layer > 8)
			{
				BuildStandHeld(Decor, Layer, Out);
				return;
			}
			if (Layer > 0)
			{
				BuildStandArmor(Decor, Layer, Out);
				return;
			}
			const float Scale = Decor.bSmall ? 0.5f : 1.0f;
			const float HeadScale = Decor.bSmall ? 0.75f : 1.0f;
			const auto Make = [&](McDecor::EPart Pose, bool bHasPose, FVector Pivot, float PartScale, std::initializer_list<FBox> Boxes) -> FPart
			{
				FPart Result;
				Result.Pivot = FVector(Pivot.X, 24.0 - Pivot.Y, -Pivot.Z);
				Result.Scale = PartScale;
				Result.Boxes = Boxes;
				if (bHasPose)
				{
					const FVector3f Angles = Decor.Pose[static_cast<int32>(Pose)];
					Result.RestPitch = FMath::DegreesToRadians(Angles.X);
					Result.RestYaw = -FMath::DegreesToRadians(Angles.Y);
					Result.RestRoll = FMath::DegreesToRadians(Angles.Z);
				}
				return Result;
			};
			static const McDecor::EPart None = McDecor::EPart::Head;
			FModel Stand;
			Stand.TextureSize = FIntPoint(64, 64);
			const auto Rest = [&](float Big, float Small) { return Decor.bSmall ? Small : Big; };
			// Box(U, V, minX, -maxY, -maxZ, W, H, D) from the cubes' x, y and z extents.
			Stand.Parts.Add(Make(McDecor::EPart::Body, true, FVector(0, Rest(0, 12), 0), Scale, { Box(16, 0, -3, -10, -1, 2, 7, 2), Box(48, 16, 1, -10, -1, 2, 7, 2), Box(0, 48, -4, -12, -1, 8, 2, 2) }));
			if (!Decor.bInvisible)
			{
				Stand.Parts.Add(Make(McDecor::EPart::Body, true, FVector(0, Rest(0, 12), 0), Scale, { Box(0, 26, -6, -3, -1.5f, 12, 3, 3) }));
				Stand.Parts.Add(Make(McDecor::EPart::RightLeg, true, FVector(-1.9f * Scale, Rest(12, 18), 0), Scale, { Box(8, 0, -1, -11, -1, 2, 11, 2) }));
				Stand.Parts.Add(Make(McDecor::EPart::LeftLeg, true, FVector(1.9f * Scale, Rest(12, 18), 0), Scale, { Box(40, 16, -1, -11, -1, 2, 11, 2) }));
				if (Decor.bArms)
				{
					Stand.Parts.Add(Make(McDecor::EPart::RightArm, true, FVector(-5.0f * Scale, Rest(2, 13), 0), Scale, { Box(24, 0, -2, -10, -1, 2, 12, 2) }));
					Stand.Parts.Add(Make(McDecor::EPart::LeftArm, true, FVector(5.0f * Scale, Rest(2, 13), 0), Scale, { Box(32, 16, 0, -10, -1, 2, 12, 2) }));
				}
			}
			Stand.Parts.Add(Make(McDecor::EPart::Head, true, FVector(0, Decor.bSmall ? 12.75f : 1.0f, 0), HeadScale, { Box(0, 0, -1, 0, -1, 2, 7, 2) }));
			if (Decor.bBasePlate)
			{
				Stand.Parts.Add(Make(None, false, FVector(0, Decor.bSmall ? 18.0f : 12.0f, 0), Scale, { Box(0, 32, -6, -12, -6, 12, 1, 12) }));
			}
			FAnim Anim;
			Anim.BodyYaw = Decor.Yaw;
			Anim.HeadYaw = Decor.Yaw;
			BuildMesh(Stand, Anim, 0, Out);
			return;
		}
		if (Decor.Kind != McDecor::EKind::ItemFrame && Decor.Kind != McDecor::EKind::GlowItemFrame)
		{
			return;
		}
		if (Layer == 2)
		{
			// The item stands flat in the frame, half a block across, turned by eighths of a turn.
			const double Angle = FMath::DegreesToRadians(-45.0 * (Decor.ItemRotation & 7));
			const FVector RotU = U * FMath::Cos(Angle) + V * FMath::Sin(Angle);
			const FVector RotV = V * FMath::Cos(Angle) - U * FMath::Sin(Angle);
			const FVector4 Rect(0, 0, 1, ItemVMax);
			AddDecorPicture(N * (1.0 / 32.0), RotU, RotV, 0.5, 0.5, Rect, false, bGlow, Out);
			AddDecorPicture(N * (1.0 / 32.0), RotU, RotV, 0.5, 0.5, Rect, true, bGlow, Out);
			return;
		}
		if (Decor.bInvisible)
		{
			return;
		}
		int32 NumElements = 0;
		const FFrameElement* Elements = GetFrameElements(NumElements);
		// The model hangs on a wall to the south and the plate's centre is the entity: block pixels (x, y, z) become
		// U to the viewer's right, V up and N out of the wall.
		const auto Place = [&](double X, double Y, double Z)
		{
			return U * ((8.0 - X) / 16.0) + V * ((Y - 8.0) / 16.0) - N * ((Z - 15.5) / 16.0);
		};
		for (int32 Index = 0; Index < NumElements; ++Index)
		{
			const FFrameElement& Element = Elements[Index];
			if (Element.bBack != (Layer == 1))
			{
				continue;
			}
			const double MinX = Element.From.X, MinY = Element.From.Y, MinZ = Element.From.Z;
			const double MaxX = Element.To.X, MaxY = Element.To.Y, MaxZ = Element.To.Z;
			const FVector Corners[6][4] = {
				{ Place(MinX, MinY, MinZ), Place(MaxX, MinY, MinZ), Place(MaxX, MinY, MaxZ), Place(MinX, MinY, MaxZ) },  // down
				{ Place(MinX, MaxY, MaxZ), Place(MaxX, MaxY, MaxZ), Place(MaxX, MaxY, MinZ), Place(MinX, MaxY, MinZ) },  // up
				{ Place(MaxX, MinY, MinZ), Place(MinX, MinY, MinZ), Place(MinX, MaxY, MinZ), Place(MaxX, MaxY, MinZ) },  // north
				{ Place(MinX, MinY, MaxZ), Place(MaxX, MinY, MaxZ), Place(MaxX, MaxY, MaxZ), Place(MinX, MaxY, MaxZ) },  // south
				{ Place(MinX, MinY, MinZ), Place(MinX, MinY, MaxZ), Place(MinX, MaxY, MaxZ), Place(MinX, MaxY, MinZ) },  // west
				{ Place(MaxX, MinY, MaxZ), Place(MaxX, MinY, MinZ), Place(MaxX, MaxY, MinZ), Place(MaxX, MaxY, MaxZ) },  // east
			};
			for (int32 Face = 0; Face < 6; ++Face)
			{
				const int32* Rect = Element.Faces[Face];
				if (Rect[0] < 0)
				{
					continue;
				}
				const FVector2D UVs[4] = {
					FVector2D(Rect[0] / 16.0, Rect[3] / 16.0), FVector2D(Rect[2] / 16.0, Rect[3] / 16.0),
					FVector2D(Rect[2] / 16.0, Rect[1] / 16.0), FVector2D(Rect[0] / 16.0, Rect[1] / 16.0),
				};
				AddDecorQuad(Corners[Face], UVs, false, Out);
			}
		}
	}

	void BuildMesh(const FModel& Model, const FAnim& Anim, int32 Layer, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		for (const FPart& Part : Model.Parts)
		{
			if (Part.Layer != Layer || ((Part.ShowPoses >> FMath::Clamp(Anim.Pose, 0, 31)) & 1u) == 0)
			{
				continue;
			}
			// A young one is half the size, with its head only a third smaller than a grown one's, so the head is big for it.
			const bool bHeadBig = Anim.bBaby && Part.Role == ERole::Head;
			float Pitch = Part.RestPitch;
			float Yaw = Part.RestYaw;
			float Roll = Part.RestRoll;
			const float RestPitch = Pitch;
			FVector Pivot = Part.Pivot;
			const bool bGlow = Part.bGlow || ((Model.GlowLayers >> Layer) & 1u) != 0 || Anim.bFlash;
			const float Pos = Anim.WalkPos;
			const float Amount = Anim.WalkAmount;
			if (Model.bFlutter && Anim.Pose == 1 && Part.Role == ERole::Static)
			{
				// A charging vex's body stands upright (logged: 0 against its 0.1571 lean at rest).
				Pitch = 0.0f;
			}
			switch (Model.bHops && Part.Hop >= 4 ? ERole::Static : Part.Role)
			{
			case ERole::Head:
				Pitch += FMath::DegreesToRadians(Anim.HeadPitch);
				Yaw -= FMath::DegreesToRadians(FMath::Clamp(FMath::UnwindDegrees(Anim.HeadYaw - Anim.BodyYaw), -75.0f, 75.0f));
				break;
			case ERole::LegFrontRight:
			case ERole::LegBackLeft:
			case ERole::LegRight:
				Pitch += Swing(Model, Pos, Amount, 0.0f, (Part.Role == ERole::LegFrontRight && Model.FrontLegSwing >= 0.0f) ? Model.FrontLegSwing : Model.LegSwing);
				break;
			case ERole::LegFrontLeft:
			case ERole::LegBackRight:
			case ERole::LegLeft:
				Pitch += Swing(Model, Pos, Amount, PI, (Part.Role == ERole::LegFrontLeft && Model.FrontLegSwing >= 0.0f) ? Model.FrontLegSwing : Model.LegSwing);
				break;
			case ERole::ArmRight:
			case ERole::ArmLeft:
			{
				const bool bRight = Part.Role == ERole::ArmRight;
				if (Model.IllagerStyle != 0 && Anim.Pose != 0)
				{
					// Armed, the arms leave the folded pair: forward on a crossbow (one drawn back as it charges), up with an axe,
					// up and out as a spell is cast.
					const float Wave = FMath::Sin(Anim.Age * 9.0f);
					if (Anim.Pose == 3)
					{
						// Both arms out and up to the sides, mirror images, swinging together (vanilla's 135 degrees of roll, its
						// sign flipped in this model space).
						Pitch += FMath::Cos(Anim.Age * 13.0f * 0.6662f) * 0.25f;
						Roll += (bRight ? -2.3562f : 2.3562f);
					}
					else if (Model.IllagerStyle == 1)
					{
						// Both straight forward at the target, the right a little inward and the left more, as the crossbow is held.
						Pitch += (bRight ? -1.5708f : (Anim.Pose == 2 ? -0.9f : -1.5708f));
						Yaw += (bRight ? 0.3f : -0.6f);
					}
					else
					{
						Pitch += (bRight ? -2.3f : -1.0f) + Wave * 0.35f * Amount + Wave * 0.15f;
					}
					break;
				}
				if (Model.bBowAim && Anim.Pose == 1)
				{
					// Aiming (logged, 3 samples a skeleton): both arms straight out at -pi/2 with the hands turned 0.1 in, the pair swaying
					// 0.05 opposite each other forward and back, and the roll easing between 0 and 0.1 (the same sway the zombie's arms have;
					// the phase is not pinned by the samples, so each mob starts where its age puts it).
					const float Side = bRight ? 1.0f : -1.0f;
					Pitch += -HALF_PI + Side * FMath::Sin(Anim.Age * 1.34f) * 0.05f;
					Yaw += Side * 0.1f;
					Roll -= Side * (0.05f + FMath::Cos(Anim.Age * 1.8f) * 0.05f);
					break;
				}
				if (Anim.bOffering && bRight)
				{
					// Held out in front, a little up.
					Pitch += -HALF_PI * 0.9f + FMath::Sin(Anim.Age * 2.0f) * 0.05f;
					break;
				}
				if (Model.bFlutter)
				{
					// A vex's arms hang out to the sides at 0.6283, swaying 0.1 either way every 65 ticks (logged). Charging (pose 1), the
					// sword arm goes up and over (3.6652 forward, 15 degrees in) and sways half as far about 0.4205, the other arm unchanged.
					if (Anim.Pose == 1 && bRight)
					{
						Pitch += 3.6652f;
						Yaw -= 0.2618f;
						Roll += 0.4205f - 0.05f * FMath::Sin(Anim.Age * 1.92f + 0.4988f);
						break;
					}
					Roll += (bRight ? -1.0f : 1.0f) * (0.6283f - 0.1f * FMath::Sin(Anim.Age * 1.92f + 0.4988f));
					break;
				}
				if (Model.bArmsForward)
				{
					// Out in front at 80 degrees, not swinging with the legs, only swaying a little (measured: 0.05 radians, the two arms
					// in opposite phase, forward and back every 4.7 seconds and in and out every 3.5).
					const float Side = bRight ? 1.0f : -1.0f;
					Pitch += -1.396f - Side * FMath::Sin(Anim.Age * 1.34f) * 0.05f;
					Roll -= Side * (0.05f + FMath::Sin(Anim.Age * 1.8f) * 0.05f);
				}
				else
				{
					Pitch += Swing(Model, Pos, Amount, bRight ? PI : 0.0f, Model.ArmSwing);
				}
				break;
			}
			case ERole::WingRight:
			case ERole::WingLeft:
			{
				// Flat against the sides at rest (chicken_fall.txt and spawner_display.txt's log model: no roll at all), and
				// beating when it runs.
				const float Side = Part.Role == ERole::WingRight ? 1.0f : -1.0f;
				if (Model.bFlutter)
				{
					// A vex's wings, from the logged model: tipped 0.4712 and 0.4712 about the length, turning 1.1 (and 0.28 either side of that,
					// 0.8 radians a tick) about the up axis.
					Pitch += 0.4712f;
					Yaw += Side * (1.1008f + 0.2808f * FMath::Sin(Anim.Age * 16.0f - 0.037f));
					Roll -= Side * 0.4712f;
					break;
				}
				if (Model.bBeatWings)
				{
					// A bee's wings beat all the time: 0.4712 either side of flat, 2.1 radians a tick (logged).
					Roll -= Side * 0.4712f * FMath::Cos(Anim.Age * 42.0f);
					break;
				}
				// A bird's wings, as logged: a little out at rest, and in the air (a chicken falling) swinging between 0.06 and 2.0
				// about 1.02 at 1.8 radians a tick.
				Roll -= Side * (Anim.bAirborne ? 1.0205f + 0.9606f * FMath::Sin(Anim.Age * 35.96f - 0.3076f) : Model.WingRest);
				break;
			}
			case ERole::PaddleLeft:
			case ERole::PaddleRight:
			{
				// paddle_log.txt (log model, a player rowing): in vanilla's model the paddle turns about x by -5pi/24 - pi/8 sin(stroke),
				// about y by pi/4 sin(1 - stroke) (pi less that on the right) and about z by pi/16, z then y then x; at rest the
				// stroke is 0. Its model is ours with x and z swapped and y down (the hull's sides and ends say so), so the
				// turn is that one seen through the swap, and through the boxes' half turn (AddPaddles).
				const bool bLeft = Part.Role == ERole::PaddleLeft;
				const float Stroke = bLeft ? Anim.PaddleLeft : Anim.PaddleRight;
				const float TurnX = -5.0f * PI / 24.0f - PI / 8.0f * FMath::Sin(Stroke);
				const float TurnY = bLeft ? PI / 4.0f * FMath::Sin(1.0f - Stroke) : PI - PI / 4.0f * FMath::Sin(1.0f - Stroke);
				const FMatrix Vanilla = FRotationMatrix::Make(FQuat(FVector::ZAxisVector, PI / 16.0f) * FQuat(FVector::YAxisVector, TurnY) * FQuat(FVector::XAxisVector, TurnX));
				const FVector Swap[3] = { FVector(0, 0, 1), FVector(0, -1, 0), FVector(1, 0, 0) };
				const FVector Half[3] = { FVector(-1, 0, 0), FVector(0, -1, 0), FVector(0, 0, 1) };
				// Column i of ours: Swap * Vanilla * Half applied to axis i.
				FVector Column[3];
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					const FVector Turned = Vanilla.TransformVector(Half[Axis]);
					Column[Axis] = Swap[0] * Turned.X + Swap[1] * Turned.Y + Swap[2] * Turned.Z;
				}
				// As yaw (y), then roll (z), then pitch (x): row y of the matrix is (sin roll, cos roll cos pitch, -cos roll sin pitch).
				Roll = FMath::Asin(FMath::Clamp(Column[0].Y, -1.0f, 1.0f));
				Pitch = FMath::Atan2(-Column[2].Y, Column[1].Y);
				Yaw = FMath::Atan2(-Column[0].Z, Column[0].X);
				break;
			}
			case ERole::ShulkerLid:
			{
				// Up half a block and three quarters of a turn, easing in and out.
				const float Open = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Anim.Lid, 0.0f, 1.0f));
				Pivot.Y += Open * 8.0f;
				Yaw += Open * PI * 1.5f;
				break;
			}
			case ERole::BellBody:
				Pitch += Anim.SwingPitch;
				Roll += Anim.SwingRoll;
				break;
			case ERole::ConduitCage:
				// About a turn every eight seconds, tipped and rocking a little.
				Yaw += Anim.Age * 0.8f;
				Pitch += 0.3f + FMath::Sin(Anim.Age * 0.6f) * 0.15f;
				break;
			case ERole::ConduitWind:
				// Tumbles with the cage: the ring tips as the cage does across ref_scene_conduits and ref_conduits_b/c.
				Yaw += Anim.Age * 0.8f;
				Pitch += 0.3f + FMath::Sin(Anim.Age * 0.6f) * 0.15f;
				break;
			case ERole::ConduitEye:
				// The model is never turned (BodyYaw 0), so the eye turns by the whole look.
				Yaw -= FMath::DegreesToRadians(Anim.HeadYaw);
				Pitch += FMath::DegreesToRadians(Anim.HeadPitch);
				break;
			case ERole::BookLeft:
			case ERole::BookRight:
			case ERole::BookPage:
			{
				// Shut, each half is turned a quarter towards +z: the left (-x) one forward, the right one back. A loose page
				// goes over the top (through +z) from the right half's angle to the left's.
				const float Shut = 1.0f - FMath::Clamp(Anim.BookOpen, 0.0f, 1.0f);
				if (Part.Role == ERole::BookLeft)
				{
					Yaw += Shut * HALF_PI;
				}
				else if (Part.Role == ERole::BookRight)
				{
					Yaw -= Shut * HALF_PI;
				}
				else
				{
					const float Flip = FMath::Clamp(Anim.BookFlip[Part.Index & 1], 0.0f, 1.0f);
					Yaw += FMath::Lerp(-Shut * HALF_PI, -PI + Shut * HALF_PI, Flip);
				}
				break;
			}
			case ERole::ChestLid:
			{
				// It eases open fast and slows at the top.
				const float Open = 1.0f - FMath::Cube(1.0f - FMath::Clamp(Anim.Lid, 0.0f, 1.0f));
				Pitch -= Open * HALF_PI;
				break;
			}
			case ERole::SpiderLeg:
			{
				const int32 Row = Part.Index / 2;
				const bool bLeft = (Part.Index & 1) != 0;
				const float Phase = (Row & 1 ? PI : 0.0f) + (bLeft ? PI * 0.5f : 0.0f);
				const float Side = bLeft ? -1.0f : 1.0f;
				Yaw += Side * -FMath::Cos(Pos * 1.3324f + Phase) * 0.4f * Amount;
				Roll += Side * -FMath::Abs(FMath::Sin(Pos * 0.6662f + Phase)) * 0.4f * Amount;
				break;
			}
			default:
				break;
			}
			if (Model.MaxLimbSwing > 0.0f && (Part.Role == ERole::LegRight || Part.Role == ERole::LegLeft || Part.Role == ERole::ArmRight || Part.Role == ERole::ArmLeft))
			{
				Pitch = RestPitch + FMath::Clamp(Pitch - RestPitch, -Model.MaxLimbSwing, Model.MaxLimbSwing);
			}
			// Lean (x) first, then turn (y), then tip (z): the order the logged spider legs and vex wings are drawn in (a spider's
			// outer legs, 0.7854 turned and 0.7854 tipped, only come to rest on the ground that way round).
			FQuat Rotation = FQuat(FVector::ZAxisVector, Roll) * FQuat(FVector::YAxisVector, Yaw) * FQuat(FVector::XAxisVector, Pitch);
			if (Model.bHops && Part.Hop != 0 && Anim.HopPos >= 0.0f)
			{
				ApplyHop(Anim.HopPos, Part.Hop, Pivot, Rotation);
			}
			const FIntPoint Size = Model.LayerTextureSizes.IsValidIndex(Layer) && Model.LayerTextureSizes[Layer].X > 0 ? Model.LayerTextureSizes[Layer] : Model.TextureSize;
			for (const FBox& Box : Part.Boxes)
			{
				if (Part.Role == ERole::ConduitWind)
				{
					// The picture's frames go down the column, one each 3 ticks (wind.png.mcmeta), and the two pictures take
					// turns, a run of all 22 frames each: in conduit_frames.txt a run of wind.png starts 20 ticks into a
					// 66-tick run of the conduit's tick count (its first streak in ref_cf_07, count 221, is frame 1).
					const int32 NumFrames = FMath::Max(1, Size.Y / 32);
					const int32 Ticks = FMath::Max(0, FMath::FloorToInt32(Anim.Age * 20.0f) - 20 + 66 * 2);
					if ((Ticks / (NumFrames * 3)) % 2 != Part.Layer - 2)
					{
						continue;
					}
					FBox Frame = Box;
					Frame.UV.Y += ((Ticks / 3) % NumFrames) * 32;
					const int32 FirstTriangle = Out.Triangles.Num();
					AddBox(Frame, Part, Rotation, Pivot, Anim.BodyYaw, Size, Anim.Hurt, bGlow, 1.0f, Model.Scale * (Anim.bBaby ? BabyScale : 1.0f), Out);
					for (int32 Index = Out.Triangles.Num() - 3; Index >= FirstTriangle; Index -= 3)
					{
						Out.Triangles.Append({ Out.Triangles[Index], Out.Triangles[Index + 2], Out.Triangles[Index + 1] });
					}
					continue;
				}
				if (Box.bFlat)
				{
					AddFlat(Box, Rotation, Pivot, Anim.BodyYaw, Anim.Hurt, Model.Scale * (Anim.bBaby ? BabyScale : 1.0f), Out);
					continue;
				}
				FBox Drawn = Box;
				if (Box.bNoseWobble)
				{
					// Measured from the logged model: a smooth circle, 0.0785 radians about x and 0.0436 about z (its sign flipped here),
					// a quarter of a cycle apart, 0.0898 radians a tick.
					const float Angle = Anim.Age * 20.0f * 0.0898f; // Age is in seconds
					Drawn.BoxRotation = FQuat(FVector::ZAxisVector, -0.0436f * FMath::Cos(Angle)) * FQuat(FVector::XAxisVector, 0.0785f * FMath::Sin(Angle));
				}
				AddBox(Drawn, Part, Rotation, Pivot, Anim.BodyYaw, Size, Anim.Hurt, bGlow, bHeadBig ? BabyHeadScale : 1.0f, Model.Scale * (Anim.bBaby ? BabyScale : 1.0f), Out);
			}
		}
		if (((Model.TintedLayers >> Layer) & 1u) != 0)
		{
			for (FColor& Color : Out.Colors)
			{
				Color.R = static_cast<uint8>(Color.R * Anim.Tint.R / 255);
				Color.G = static_cast<uint8>(Color.G * Anim.Tint.G / 255);
				Color.B = static_cast<uint8>(Color.B * Anim.Tint.B / 255);
			}
		}
	}

	void BuildBlockMesh(const FModel& Model, FMcStateId State, float BodyYaw, bool bFlash, McPlayerModel::FMeshData& Out, TArray<FVector2D>& UV1)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		UV1.Reset();
		const FMcBlockModels& Models = FMcBlockModels::Get();
		if (!Models.IsBaked() || State >= Models.NumStates())
		{
			return;
		}
		const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
		for (const FMcBakedQuad& Quad : Models.GetModel(State).Quads)
		{
			FVector Normal = FVector::ZeroVector;
			switch (Quad.Face)
			{
			case EMcDirection::Down: Normal = FVector(0, -1, 0); break;
			case EMcDirection::Up: Normal = FVector(0, 1, 0); break;
			case EMcDirection::North: Normal = FVector(0, 0, -1); break;
			case EMcDirection::South: Normal = FVector(0, 0, 1); break;
			case EMcDirection::West: Normal = FVector(-1, 0, 0); break;
			case EMcDirection::East: Normal = FVector(1, 0, 0); break;
			default: break;
			}
			Normal = ModelRotation.RotateVector(Normal);
			const FColor Color = bFlash ? FColor::White : Shade(Normal, 0.0f);
			const float Layer = static_cast<float>(FMcBlockTextures::Get().GetLayer(Quad.Texture));
			const int32 First = Out.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const FVector Local = (FVector(Quad.Positions[Corner]) - FVector(0.5)) * 16.0 * Model.BlockScale + Model.BlockOffset;
				const FVector Placed = ModelRotation.RotateVector(Local);
				Out.Positions.Add(FVector(Placed.X, Placed.Z, Placed.Y) * (McCoords::UnitsPerBlock / 16.0));
				Out.Normals.Add(FVector(Normal.X, Normal.Z, Normal.Y));
				Out.UV0.Add(bFlash ? FVector2D(0.2, 0.6) : FVector2D(Quad.UVs[Corner]));
				Out.Colors.Add(Color);
				UV1.Add(FVector2D(bFlash ? 0.0f : Layer, 0.0f));
			}
			Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
	}

	int32 GetItemCopies(int32 Count)
	{
		return Count > 48 ? 5 : Count > 32 ? 4 : Count > 16 ? 3 : Count > 1 ? 2 : 1;
	}

	FVector2D GetSwellScale(float Swelling)
	{
		const float S = FMath::Clamp(Swelling, 0.0f, 1.0f);
		// A little shake that grows with the fuse, and the swelling itself, slow at first and quick at the end.
		const float Shake = 1.0f + FMath::Sin(S * 100.0f) * S * 0.01f;
		const float Grow = S * S * S * S;
		return FVector2D((1.0f + Grow * 0.4f) * Shake, (1.0f + Grow * 0.1f) / Shake);
	}

	bool IsSwellFlash(float Swelling)
	{
		return FMath::FloorToInt32(FMath::Clamp(Swelling, 0.0f, 1.0f) * 10.0f) % 2 == 1;
	}

	void FSquish::Tick(float Rise)
	{
		if (Rise > 0.05f && LastRise <= 0.01f)
		{
			Target = 1.0f;
		}
		else if (Rise > -0.01f && LastRise < -0.05f)
		{
			Target = -0.5f;
		}
		LastRise = Rise;
		Previous = Value;
		Value += (Target - Value) * 0.5f;
		Target *= 0.6f;
	}

	FVector2D FSquish::GetScale(float Alpha, float Size) const
	{
		const float Amount = FMath::Lerp(Previous, Value, FMath::Clamp(Alpha, 0.0f, 1.0f)) / (Size * 0.5f + 1.0f);
		const float Across = 1.0f / (Amount + 1.0f);
		return FVector2D(Across, 1.0f / Across);
	}

	void BuildArrowMesh(float BodyYaw, float Pitch, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		// The arrow lies along z (its tip at +z), turned an eighth so the two shafts make an X from the front. Pixels: the
		// game draws it 16 units long at 0.05625 of a block a unit, 0.9 of a pixel. The picture is 32 square: the
		// fletching at the top left (5 square) and the shaft below it (16 by 5).
		constexpr float Unit = 0.9f;
		constexpr float Length = 8.0f * Unit;
		constexpr float Half = 2.0f * Unit;
		const FQuat Pose = FQuat(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw)) * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Pitch))
			* FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(45.0f));
		// Half a hitbox up from the feet.
		const FVector Centre(0.0, 4.0, 0.0);
		const auto AddQuad = [&](const FVector (&Corners)[4], const FVector2D (&Pixels)[4], const FVector& Normal)
		{
			for (const bool bBack : { false, true })
			{
				const FVector FaceNormal = Pose.RotateVector(bBack ? -Normal : Normal);
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector Model = Centre + Pose.RotateVector(Corners[Corner]);
					Out.Positions.Add(FVector(Model.X, Model.Z, Model.Y) * (McCoords::UnitsPerBlock / 16.0));
					Out.Normals.Add(FVector(FaceNormal.X, FaceNormal.Z, FaceNormal.Y));
					Out.UV0.Add(Pixels[Corner] / 32.0);
					Out.Colors.Add(FColor::White);
				}
				if (bBack)
				{
					Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
				}
				else
				{
					Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
				}
			}
		};
		// The shaft: one plane up and down, one across.
		const FVector2D ShaftPixels[4] = { FVector2D(0, 10), FVector2D(16, 10), FVector2D(16, 5), FVector2D(0, 5) };
		const FVector Up[4] = { FVector(0, -Half, -Length), FVector(0, -Half, Length), FVector(0, Half, Length), FVector(0, Half, -Length) };
		AddQuad(Up, ShaftPixels, FVector(1, 0, 0));
		const FVector Across[4] = { FVector(-Half, 0, -Length), FVector(-Half, 0, Length), FVector(Half, 0, Length), FVector(Half, 0, -Length) };
		AddQuad(Across, ShaftPixels, FVector(0, 1, 0));
		// The fletching at the tail.
		const FVector2D FletchPixels[4] = { FVector2D(0, 5), FVector2D(5, 5), FVector2D(5, 0), FVector2D(0, 0) };
		const float Tail = -7.0f * Unit;
		const FVector Fletch[4] = { FVector(-Half, -Half, Tail), FVector(Half, -Half, Tail), FVector(Half, Half, Tail), FVector(-Half, Half, Tail) };
		AddQuad(Fletch, FletchPixels, FVector(0, 0, -1));
	}

	// A thrown item: its picture 0.25 across on two squares crossed at the middle of the 0.25 body, seen from any side.
	void BuildTntMesh(int32 Layer, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Half = 0.49 * McCoords::UnitsPerBlock;
		const double Tall = 0.98 * McCoords::UnitsPerBlock;
		// Faces as (normal, right, up) with the cube centred over the feet; both sides of each are drawn so the winding is moot.
		struct FFace { FVector Normal, Right, Up; double Lift; };
		TArray<FFace> Faces;
		if (Layer == 0)
		{
			Faces = { { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), 0.0 }, { FVector(-1, 0, 0), FVector(0, -1, 0), FVector(0, 0, 1), 0.0 },
				{ FVector(0, 1, 0), FVector(-1, 0, 0), FVector(0, 0, 1), 0.0 }, { FVector(0, -1, 0), FVector(1, 0, 0), FVector(0, 0, 1), 0.0 } };
		}
		else
		{
			Faces = { { FVector(0, 0, Layer == 1 ? 1 : -1), FVector(1, 0, 0), FVector(0, 1, 0), 0.0 } };
		}
		const FVector2D UVs[4] = { FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) };
		for (const FFace& Face : Faces)
		{
			const bool bSide = Layer == 0;
			const FVector Centre = Face.Normal * Half + FVector(0, 0, Tall * 0.5) + (bSide ? FVector::ZeroVector : Face.Normal * (Tall * 0.5 - Half));
			const double Across = Half, Up = bSide ? Tall * 0.5 : Half;
			const FVector Corners[4] = { Centre - Face.Right * Across - Face.Up * Up, Centre + Face.Right * Across - Face.Up * Up,
				Centre + Face.Right * Across + Face.Up * Up, Centre - Face.Right * Across + Face.Up * Up };
			for (const bool bBack : { false, true })
			{
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const int32 Index = bBack ? 3 - Corner : Corner;
					Out.Positions.Add(Corners[Index]);
					Out.Normals.Add(bBack ? -Face.Normal : Face.Normal);
					Out.UV0.Add(UVs[Index]);
					Out.Colors.Add(FColor::White);
				}
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
		}
	}

	void BuildThrownMesh(float VMax, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Units = McCoords::UnitsPerBlock;
		const double Half = 0.125 * Units;
		const FVector2D UVs[4] = { FVector2D(0, VMax), FVector2D(1, VMax), FVector2D(1, 0), FVector2D(0, 0) };
		for (const FVector& Across : { FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0) })
		{
			const FVector Normal(Across.Y, Across.X, 0.0);
			const FVector Centre(0.0, 0.0, Half);
			const FVector Corners[4] = { Centre - Across * Half - FVector(0, 0, Half), Centre + Across * Half - FVector(0, 0, Half),
				Centre + Across * Half + FVector(0, 0, Half), Centre - Across * Half + FVector(0, 0, Half) };
			for (const bool bBack : { false, true })
			{
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Out.Positions.Add(Corners[bBack ? 3 - Corner : Corner]);
					Out.Normals.Add(bBack ? -Normal : Normal);
					Out.UV0.Add(UVs[bBack ? 3 - Corner : Corner]);
					Out.Colors.Add(FColor::White);
				}
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
		}
	}

	void BuildBlockMesh(bool bTopPicture, int32 Layer, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Half = 0.5 * McCoords::UnitsPerBlock;
		const FVector Faces[6] = {
			FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0), FVector(0, 0, 1), FVector(0, 0, -1),
		};
		for (const FVector& Normal : Faces)
		{
			if (bTopPicture && (Normal.Z > 0.5) != (Layer == 1))
			{
				continue;
			}
			// Across runs along the face and Up the other way, so that Across x Up = -Normal, the winding the item picture uses.
			const FVector Across = FMath::Abs(Normal.Z) > 0.5 ? FVector(1, 0, 0) : FVector::CrossProduct(Normal, FVector(0, 0, 1));
			const FVector Up = FVector::CrossProduct(Across, Normal);
			const FVector Centre = FVector(0.0, 0.0, Half) + Normal * Half;
			const FVector Corners[4] = {
				Centre - Across * Half - Up * Half, Centre + Across * Half - Up * Half,
				Centre + Across * Half + Up * Half, Centre - Across * Half + Up * Half,
			};
			const FVector2D UVs[4] = { FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) };
			const int32 First = Out.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Out.Positions.Add(Corners[Corner]);
				Out.Normals.Add(Normal);
				Out.UV0.Add(UVs[Corner]);
				Out.Colors.Add(FColor::White);
			}
			Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
	}

	void BuildFlatItemMesh(float Width, float Height, float VMax, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Units = McCoords::UnitsPerBlock;
		const double Half = Width * 0.5 * Units;
		const FVector Centre(0.0, 0.0, Height * Units);
		// The picture's top edge towards -Y (north), its left towards -X, as it reads looking down on it.
		const FVector Corners[4] = { Centre + FVector(-Half, Half, 0), Centre + FVector(Half, Half, 0), Centre + FVector(Half, -Half, 0), Centre + FVector(-Half, -Half, 0) };
		const FVector2D UVs[4] = { FVector2D(0, VMax), FVector2D(1, VMax), FVector2D(1, 0), FVector2D(0, 0) };
		for (const bool bBack : { false, true })
		{
			const FVector Normal(0.0, 0.0, bBack ? -1.0 : 1.0);
			const int32 First = Out.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Out.Positions.Add(Corners[Corner]);
				Out.Normals.Add(Normal);
				Out.UV0.Add(UVs[Corner]);
				Out.Colors.Add(FColor::White);
			}
			if (bBack)
			{
				Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
			}
			else
			{
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
		}
	}

	// The dropped item's numbers, fitted from 16 same-camera shots 4 ticks apart (itemdrop1): the width of a diamond's picture followed
	// |cos| of a turn of 0.05 radian a tick; its middle rose and fell 0.096 of a block about 0.385 above the floor with a period of
	// 63 ticks. -McDrop=lift,radius,cubescale overrides the base height, the spread of a stack and a block's size while they are being fitted.
	namespace
	{
		float GetDropArg(int32 Index, float Default)
		{
			static FString Text;
			static bool bRead = FParse::Value(FCommandLine::Get(), TEXT("-McDrop="), Text, false);
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, TEXT(","));
			return Parts.IsValidIndex(Index) ? FCString::Atof(*Parts[Index]) : Default;
		}
	}

	float GetDroppedHeight(float Age, float Lift)
	{
		return GetDropArg(0, 0.26f) + 0.096f * FMath::Sin(Age * 2.0f) + Lift;
	}

	void BuildDroppedMesh(const McPlayerModel::FMeshData& Unit, float Age, int32 Count, float Scale, float Lift, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Units = McCoords::UnitsPerBlock;
		const FQuat Turn(FVector::ZAxisVector, Age * 1.0f);
		const FVector Up(0.0, 0.0, GetDroppedHeight(Age, Lift));
		const int32 Copies = GetItemCopies(Count);
		const double Spread = GetDropArg(1, 0.12f);
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			// The copies are scattered a little around the middle, and turn with it.
			const FVector Offset = Copies == 1 ? FVector::ZeroVector : FVector(FMath::Sin(Copy * 2.4) * Spread, FMath::Cos(Copy * 2.4) * Spread, (FMath::Frac(Copy * 0.618) - 0.5) * 0.2);
			const int32 First = Out.Positions.Num();
			for (int32 Index = 0; Index < Unit.Positions.Num(); ++Index)
			{
				Out.Positions.Add(Up * Units + Turn.RotateVector(Unit.Positions[Index] * Scale + Offset * Units));
				Out.Normals.Add(Turn.RotateVector(Unit.Normals[Index]));
				Out.UV0.Add(Unit.UV0[Index]);
				Out.Colors.Add(Unit.Colors[Index]);
			}
			for (const int32 Index : Unit.Triangles)
			{
				Out.Triangles.Add(First + Index);
			}
		}
	}

	void BuildItemMesh(float Age, int32 Count, float Width, float VMax, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Units = McCoords::UnitsPerBlock;
		// Floats a little above the ground, bobbing, and turns.
		const double Height = (0.2 + FMath::Sin(Age * 2.0f) * 0.05 + 0.05 + Width * 0.5) * Units;
		const double Spin = Age * FMath::DegreesToRadians(70.0f);
		const FQuat Turn(FVector::ZAxisVector, Spin);
		const int32 Copies = GetItemCopies(Count);
		const double Half = Width * 0.5 * Units;
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			// The copies are scattered a little around the middle.
			const FVector Offset = Copies == 1 ? FVector::ZeroVector
				: FVector(FMath::Sin(Copy * 2.4) * 0.07, FMath::Cos(Copy * 2.4) * 0.07, 0.0) * Units;
			for (const bool bBack : { false, true })
			{
				const FVector Normal = Turn.RotateVector(FVector(0.0, bBack ? -1.0 : 1.0, 0.0));
				const FVector Across = Turn.RotateVector(FVector(bBack ? -1.0 : 1.0, 0.0, 0.0));
				const FVector Centre = FVector(0.0, 0.0, Height) + Turn.RotateVector(Offset);
				const FVector Corners[4] = {
					Centre - Across * Half - FVector(0, 0, Half), Centre + Across * Half - FVector(0, 0, Half),
					Centre + Across * Half + FVector(0, 0, Half), Centre - Across * Half + FVector(0, 0, Half),
				};
				const FVector2D UVs[4] = { FVector2D(0, VMax), FVector2D(1, VMax), FVector2D(1, 0), FVector2D(0, 0) };
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Out.Positions.Add(Corners[Corner]);
					Out.Normals.Add(Normal);
					Out.UV0.Add(UVs[Corner]);
					Out.Colors.Add(FColor::White);
				}
				if (bBack)
				{
					Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
				}
				else
				{
					Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
				}
			}
		}
	}
}

namespace
{
	// Pictures stay loaded for as long as the game runs; there are few of them.
	TMap<FString, TStrongObjectPtr<UTexture2DArray>>& TextureCache()
	{
		static TMap<FString, TStrongObjectPtr<UTexture2DArray>> Cache;
		return Cache;
	}

	struct FLoaded
	{
		UTexture2DArray* Texture = nullptr;
		int32 Width = 0;
		int32 Height = 0;
	};

	TMap<FString, FIntPoint>& SizeCache()
	{
		static TMap<FString, FIntPoint> Cache;
		return Cache;
	}

	// Loads (once) the picture at Path; null if it isn't there.
	UTexture2DArray* LoadCached(const FString& Path, FIntPoint& OutSize, bool bWhite = false)
	{
		const FString Key = bWhite ? Path + TEXT("#white") : Path;
		if (const TStrongObjectPtr<UTexture2DArray>* Found = TextureCache().Find(Key))
		{
			OutSize = SizeCache().FindRef(Key);
			return Found->Get();
		}
		int32 Width = 0, Height = 0;
		// Kept for every world that comes after, so not inside this one.
		UTexture2DArray* Texture = McRenderTextures::LoadPngAsArray(GetTransientPackage(), Path, Width, Height, bWhite);
		TextureCache().Add(Key, TStrongObjectPtr<UTexture2DArray>(Texture));
		SizeCache().Add(Key, FIntPoint(Width, Height));
		OutSize = FIntPoint(Width, Height);
		return Texture;
	}

	// The pictures laid over each other, kept like LoadCached's (a villager's look is the same for every villager that has it).
	UTexture2DArray* LoadCompositeCached(const TArray<FString>& Paths, FIntPoint& OutSize)
	{
		const FString Key = FString::Join(Paths, TEXT("|")) + TEXT("#composite");
		if (const TStrongObjectPtr<UTexture2DArray>* Found = TextureCache().Find(Key))
		{
			OutSize = SizeCache().FindRef(Key);
			return Found->Get();
		}
		int32 Width = 0, Height = 0;
		UTexture2DArray* Texture = McRenderTextures::LoadPngLayersAsArray(GetTransientPackage(), Paths, Width, Height);
		TextureCache().Add(Key, TStrongObjectPtr<UTexture2DArray>(Texture));
		SizeCache().Add(Key, FIntPoint(Width, Height));
		OutSize = FIntPoint(Width, Height);
		return Texture;
	}

	// The texture of an item: its own picture, or its block's side, top or front.
	FString FindItemTexture(FName Item)
	{
		const FString Name = McCache::PathOf(McItemFrames::Resolve(Item).ToString());
		for (const FString& Candidate : { TEXT("item/") + Name, TEXT("block/") + Name, TEXT("block/") + Name + TEXT("_side"), TEXT("block/") + Name + TEXT("_top"), TEXT("block/") + Name + TEXT("_front") })
		{
			const FString Path = McCache::Asset(TEXT("textures/") + Candidate + TEXT(".png"));
			if (IFileManager::Get().FileExists(*Path))
			{
				return Path;
			}
		}
		return FString();
	}

	// The picture on top of an item's block where it has one of its own (an anvil), else empty.
	FString FindTopTexture(FName Item)
	{
		const FString Path = McCache::Asset(TEXT("textures/block/") + McCache::PathOf(Item.ToString()) + TEXT("_top.png"));
		return Item.IsNone() || !IFileManager::Get().FileExists(*Path) || Path == FindItemTexture(Item) ? FString() : Path;
	}
}

UMcEntityModelComponent::UMcEntityModelComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetUsingAbsoluteRotation(true);
	bUseAsyncCooking = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cutout(TEXT("/Game/Mc/Materials/M_McBlockCutout.M_McBlockCutout"));
	BaseMaterial = Cutout.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Translucent(TEXT("/Game/Mc/Materials/M_McBlockTranslucent.M_McBlockTranslucent"));
	TranslucentMaterial = Translucent.Object;
}

void UMcEntityModelComponent::SetKind(FName InKind, FName InItem, int32 InCount)
{
	if (Kind == InKind && Item == InItem)
	{
		Count = InCount;
		return;
	}
	Kind = InKind;
	Item = InItem;
	Count = InCount;
	TextureOverrides.Reset();
	LookKey = NAME_None;
	VariantSeed = INT32_MIN;
	WolfTextureState = 0;
	bBabyLook = false;
	bIsGrillFood = Kind == TEXT("mc:grillfood");
	bIsItem = bIsGrillFood || Kind == TEXT("minecraft:item") || Kind == TEXT("minecraft:snowball") || Kind == TEXT("minecraft:egg") || Kind == TEXT("minecraft:ender_pearl") || Kind == TEXT("minecraft:ender_eye");
	bIsPotItem = bIsItem && Item.ToString().StartsWith(TEXT("minecraft:decorated_pot"));
	PotSides = NAME_None;
	if (bIsPotItem)
	{
		FString Sides;
		if (Item.ToString().Split(TEXT("="), nullptr, &Sides))
		{
			PotSides = FName(*Sides);
		}
		bIsItem = false;
	}
	bIsArrow = Kind == TEXT("minecraft:arrow");
	bIsBobber = Kind == TEXT("minecraft:fishing_bobber");
	bIsFalling = Kind == TEXT("minecraft:falling_block");
	bIsThrown = Kind == TEXT("minecraft:snowball") || Kind == TEXT("minecraft:egg") || Kind == TEXT("minecraft:ender_pearl") || Kind == TEXT("minecraft:ender_eye");
	bIsDropped = bIsItem && !bIsThrown && !bIsGrillFood;
	bDropReady = false;
	bIsBanner = Kind == TEXT("mc:banner");
	bIsTnt = Kind == TEXT("minecraft:tnt");
	bIsDecor = Kind == TEXT("mc:decor") && McDecor::Decode(Item.ToString(), Decor);
	if (bIsBanner)
	{
		McBanner::Decode(Item.ToString(), BannerBase, bBannerWall, BannerLayers);
	}
	Model = bIsItem || bIsArrow || bIsBobber || bIsBanner || bIsTnt || bIsFalling || bIsDecor ? nullptr : McEntityModel::Find(bIsPotItem ? FName(TEXT("mc:decorated_pot")) : Kind);
	Materials.Reset();
	BlockMaterial = nullptr;
	FlashMaterial = nullptr;
	BlockText.Reset();
	BlockVertices = 0;
	ClearAllMeshSections();
	NumSections = 0;
	bItemFailed = false;
	bFlashing = false;
	bDirty = true;
}

void UMcEntityModelComponent::SetBaby(bool bInBaby)
{
	if (bInBaby == bBabyLook)
	{
		return;
	}
	bBabyLook = bInBaby;
	bDirty = true;
	if (bIsItem || bIsArrow)
	{
		return;
	}
	const McEntityModel::FModel* Own = bInBaby ? McEntityModel::Find(FName(*(Kind.ToString() + TEXT("_baby")))) : nullptr;
	const McEntityModel::FModel* Target = Own ? Own : McEntityModel::Find(Kind);
	if (Target != Model)
	{
		Model = Target;
		TextureOverrides.Reset();
		Materials.Reset();
		ClearAllMeshSections();
		NumSections = 0;
		if (Model && !Model->Variants.IsEmpty() && VariantSeed != INT32_MIN)
		{
			ApplyVariant();
		}
	}
}

void UMcEntityModelComponent::SetVillagerLook(FName InVariant, FName InProfession, int32 InLevel, bool bInBaby)
{
	const bool bVillager = Kind == TEXT("minecraft:villager");
	const bool bZombie = Kind == TEXT("minecraft:zombie_villager");
	if (!bVillager && !bZombie)
	{
		return;
	}
	const FName Key(*FString::Printf(TEXT("%s|%s|%d"), *InVariant.ToString(), *InProfession.ToString(), InLevel));
	if (Key == LookKey)
	{
		return;
	}
	LookKey = Key;
	TextureOverrides = McEntityModel::GetVillagerTextures(bZombie, InVariant, InProfession, InLevel);
	// The layers are made again from the new pictures.
	Materials.Reset();
	ClearAllMeshSections();
	NumSections = 0;
	bFlashing = false;
}

void UMcEntityModelComponent::SetVariantSeed(int32 InSeed)
{
	if (!Model || Model->Variants.IsEmpty() || InSeed == VariantSeed)
	{
		return;
	}
	VariantSeed = InSeed;
	ApplyVariant();
}

void UMcEntityModelComponent::ApplyVariant()
{
	TextureOverrides = Model->Variants[FMath::Abs(VariantSeed) % Model->Variants.Num()];
	if (Kind == TEXT("minecraft:wolf") && WolfTextureState != 0 && !TextureOverrides.IsEmpty())
	{
		// wolf_ashen.png -> wolf_ashen_tame.png, wolf_ashen_angry.png
		FString& Base = TextureOverrides[0];
		const bool bBabyPicture = Base.EndsWith(TEXT("_baby.png"));
		Base = Base.LeftChop(bBabyPicture ? 9 : 4) + (WolfTextureState == 2 ? TEXT("_tame") : TEXT("_angry")) + (bBabyPicture ? TEXT("_baby.png") : TEXT(".png"));
	}
	Materials.Reset();
	ClearAllMeshSections();
	NumSections = 0;
	bFlashing = false;
}

void UMcEntityModelComponent::SetHeldItem(FName InItem)
{
	if (InItem == HeldLook)
	{
		return;
	}
	HeldLook = InItem;
	if (Model && Model->HeldLayer >= 0)
	{
		// The picture in the hand changed: the materials are made again, and the mesh with them.
		Materials.Reset();
		ClearAllMeshSections();
		NumSections = 0;
		bFlashing = false;
	}
}

void UMcEntityModelComponent::SetPose(int32 InPose)
{
	PoseLook = InPose;
	if (Kind == TEXT("minecraft:wolf"))
	{
		// Tame wins over angry (a tamed wolf is never drawn angry); the picture changes only when that does.
		const int32 State = (InPose & 4) ? 2 : (InPose & 2) ? 1 : 0;
		if (State != WolfTextureState)
		{
			WolfTextureState = State;
			if (Model && !Model->Variants.IsEmpty() && VariantSeed != INT32_MIN)
			{
				ApplyVariant();
			}
		}
	}
}

bool UMcEntityModelComponent::HasModel() const
{
	return bIsItem || bIsArrow || bIsBobber || bIsBanner || bIsTnt || bIsFalling || bIsDecor || Model != nullptr;
}

void UMcEntityModelComponent::BuildBobber(McPlayerModel::FMeshData& Out) const
{
	const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Player || !Player->PlayerCameraManager)
	{
		return;
	}
	const FVector CameraAt = Player->PlayerCameraManager->GetCameraLocation();
	const FVector Origin = GetComponentLocation();
	const double Units = McCoords::UnitsPerBlock;
	// A quad from four corners (relative to the component), both sides, its picture spanning UVMin to UVMax.
	const auto Quad = [&Out](const FVector (&Corners)[4], const FVector2D& UVMin, const FVector2D& UVMax, const FColor& Colour)
	{
		const FVector Normal = FVector::CrossProduct(Corners[1] - Corners[0], Corners[2] - Corners[0]).GetSafeNormal();
		const FVector2D UVs[4] = { FVector2D(UVMin.X, UVMin.Y), FVector2D(UVMin.X, UVMax.Y), FVector2D(UVMax.X, UVMax.Y), FVector2D(UVMax.X, UVMin.Y) };
		for (const double Side : { 1.0, -1.0 })
		{
			const int32 First = Out.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Out.Positions.Add(Corners[Corner]);
				Out.Normals.Add(Normal * Side);
				Out.UV0.Add(UVs[Corner]);
				Out.Colors.Add(Colour);
			}
			if (Side > 0.0)
			{
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
			else
			{
				Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
			}
		}
	};

	// The hook's picture, half a block square, turned to the camera, centred on the hitbox.
	const FVector Centre = FVector(0.0, 0.0, HitboxHeight * 0.5 * Units);
	const FVector ToCamera = (CameraAt - (Origin + Centre)).GetSafeNormal();
	// The camera's right and up as it sees the bobber (Unreal's axes turn the left-handed way).
	FVector Right = FVector::CrossProduct(ToCamera, FVector::UpVector).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector;
	}
	const FVector Up = FVector::CrossProduct(Right, ToCamera).GetSafeNormal();
	const double Half = 0.25 * Units;
	const FVector Sprite[4] = { Centre - Right * Half + Up * Half, Centre - Right * Half - Up * Half, Centre + Right * Half - Up * Half, Centre + Right * Half + Up * Half };
	Quad(Sprite, FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), FColor::White);

	if (!RodTip.IsSet())
	{
		return;
	}
	// The line: from the rod tip to the top of the bobber, hanging slack, as a thin strip turned to the camera and a hair
	// over a pixel wide at any distance. Its colour comes from a black pixel of the hook's picture.
	const FVector From = McCoords::BlockToUnreal(*RodTip) - Origin;
	const FVector To = Centre + FVector(0.0, 0.0, 0.1 * Units);
	const FVector2D Black(4.5 / 8.0, 4.5 / 8.0);
	constexpr int32 Segments = 16;
	const auto At = [&](int32 Index)
	{
		// Straight out across, but it drops steeply from the rod and runs in flat to the bobber: the height above the
		// bobber goes as the square of the way along, so the line never dips below the lower end.
		const double T = static_cast<double>(Index) / Segments;
		const double S = 1.0 - T;
		FVector Point = FMath::Lerp(From, To, T);
		Point.Z = To.Z + (From.Z - To.Z) * (S * S + S) * 0.5;
		return Point;
	};
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const FVector A = At(Index);
		const FVector B = At(Index + 1);
		const FVector Mid = (A + B) * 0.5;
		const FVector Across = FVector::CrossProduct(B - A, CameraAt - (Origin + Mid)).GetSafeNormal();
		const double Width = FVector::Dist(CameraAt, Origin + Mid) * 0.0012;
		const FVector Strip[4] = { A - Across * Width, B - Across * Width, B + Across * Width, A + Across * Width };
		Quad(Strip, Black, Black, FColor::Black);
	}
}

void UMcEntityModelComponent::SetHitboxWidth(float InWidth)
{
	HitboxWidth = InWidth;
}

void UMcEntityModelComponent::SetSwelling(float InSwelling)
{
	TargetSwelling = FMath::Clamp(InSwelling, 0.0f, 1.0f);
}

void UMcEntityModelComponent::SetLook(float InBodyYaw, float InHeadYaw, float InHeadPitch)
{
	TargetBodyYaw = InBodyYaw;
	TargetHeadYaw = InHeadYaw;
	TargetHeadPitch = InHeadPitch;
	if (!bHasLook)
	{
		bHasLook = true;
		Anim.BodyYaw = InBodyYaw;
		Anim.HeadYaw = InHeadYaw;
		Anim.HeadPitch = InHeadPitch;
	}
}

void UMcEntityModelComponent::SetHurtCount(int32 InHurtCount)
{
	// The first count seen is how it already was; each rise after that is a hit.
	if (HurtCount >= 0 && InHurtCount != HurtCount)
	{
		Anim.Hurt = 1.0f;
	}
	HurtCount = InHurtCount;
}

FString UMcEntityModelComponent::GetTexturePath(int32 Layer) const
{
	if (bIsFalling && Layer == 1)
	{
		return FindTopTexture(Item);
	}
	if (bIsDropped && bDropCube)
	{
		const McHand::FHeldPictures Pictures = McHand::FindHeldPictures(Item);
		return Layer == 1 ? Pictures.Top : Pictures.Side;
	}
	if (bIsItem || bIsFalling)
	{
		return FindItemTexture(Item);
	}
	if (bIsArrow)
	{
		return McCache::Asset(TEXT("textures/entity/projectiles/arrow.png"));
	}
	if (bIsTnt)
	{
		static const TCHAR* Names[3] = { TEXT("tnt_side"), TEXT("tnt_top"), TEXT("tnt_bottom") };
		return McCache::Asset(FString(TEXT("textures/block/")) + Names[FMath::Clamp(Layer, 0, 2)] + TEXT(".png"));
	}
	if (bIsBobber)
	{
		return McCache::Asset(TEXT("textures/entity/fishing/fishing_hook.png"));
	}
	if (bIsDecor)
	{
		switch (Decor.Kind)
		{
		case McDecor::EKind::Painting:
			return McCache::Asset(TEXT("textures/painting/") + (Layer == 0 ? Decor.Variant : FString(TEXT("back"))) + TEXT(".png"));
		case McDecor::EKind::ArmorStand:
			if (Layer > 8)
			{
				// What is held: the picture of its item, flat.
				const FString& Held = Decor.Equipment[Layer - 5];
				return Held.IsEmpty() ? FString() : McHand::FindHeldPictures(FName(*Held)).Side;
			}
			if (Layer > 0)
			{
				// The worn piece's material names its picture; leggings have a sheet of their own.
				const bool bOverlay = Layer > 4;
				const int32 Slot = bOverlay ? Layer - 5 : Layer - 1;
				FString Name = Decor.Equipment[Slot];
				if (Name.IsEmpty())
				{
					return FString();
				}
				Name.RemoveFromStart(TEXT("minecraft:"));
				for (const TCHAR* Suffix : { TEXT("_helmet"), TEXT("_chestplate"), TEXT("_leggings"), TEXT("_boots") })
				{
					Name.RemoveFromEnd(Suffix);
				}
				Name = Name == TEXT("golden") ? FString(TEXT("gold")) : Name == TEXT("turtle") ? FString(TEXT("turtle_scute")) : Name;
				if (bOverlay && Name != TEXT("leather"))
				{
					return FString();
				}
				return McCache::Asset(FString(TEXT("textures/entity/equipment/")) + (Slot == 2 ? TEXT("humanoid_leggings/") : TEXT("humanoid/")) + Name + (bOverlay ? TEXT("_overlay") : TEXT("")) + TEXT(".png"));
			}
			return McCache::Asset(TEXT("textures/entity/armorstand/armorstand.png"));
		default:
			if (Layer == 2)
			{
				return FindItemTexture(FName(*Decor.Item));
			}
			return McCache::Asset(Layer == 0 ? FString(TEXT("textures/block/birch_planks.png"))
				: FString(Decor.Kind == McDecor::EKind::GlowItemFrame ? TEXT("textures/block/glow_item_frame.png") : TEXT("textures/block/item_frame.png")));
		}
	}
	if (bIsBanner)
	{
		// The pole and bar have a picture of their own; the flag has the base and then one for each pattern.
		if (Layer == 0)
		{
			return McCache::Asset(TEXT("textures/entity/banner/banner_base.png"));
		}
		return McCache::Asset(TEXT("textures/entity/banner/") + (Layer < 2 ? FString(TEXT("base")) : BannerLayers.IsValidIndex(Layer - 2) ? BannerLayers[Layer - 2].Pattern : FString(TEXT("base"))) + TEXT(".png"));
	}
	if (Kind == TEXT("minecraft:boat") || Kind == TEXT("minecraft:chest_boat") || Kind == TEXT("minecraft:bamboo_raft") || Kind == TEXT("minecraft:bamboo_chest_raft"))
	{
		FString Wood = Item.IsNone() ? FString(TEXT("oak")) : Item.ToString();
		Wood.RemoveFromStart(TEXT("minecraft:"));
		const bool bChest = Kind == TEXT("minecraft:chest_boat") || Kind == TEXT("minecraft:bamboo_chest_raft");
		return McCache::Asset((bChest ? TEXT("textures/entity/chest_boat/") : TEXT("textures/entity/boat/")) + Wood + TEXT(".png"));
	}
	const FName PotItem = bIsPotItem ? PotSides : Item;
	if ((Kind == TEXT("mc:decorated_pot") || bIsPotItem) && Layer >= 1 && !PotItem.IsNone())
	{
		// The item names the sides back, left, right and front; the model's side layers go front, right, back, left.
		static const int32 ItemSide[4] = { 3, 2, 0, 1 };
		TArray<FString> Sides;
		PotItem.ToString().ParseIntoArray(Sides, TEXT(","));
		FString Side = Sides.IsValidIndex(ItemSide[(Layer - 1) & 3]) ? Sides[ItemSide[(Layer - 1) & 3]] : FString();
		Side.RemoveFromStart(TEXT("minecraft:"));
		if (Side.RemoveFromEnd(TEXT("_pottery_sherd")))
		{
			return McCache::Asset(TEXT("textures/entity/decorated_pot/") + Side + TEXT("_pottery_pattern.png"));
		}
	}
	if (Model && Layer == Model->HeldLayer)
	{
		return HeldLook.IsNone() ? FString() : FindItemTexture(HeldLook);
	}
	if (TextureOverrides.IsValidIndex(Layer))
	{
		return TextureOverrides[Layer].IsEmpty() ? FString() : McCache::Asset(TEXT("textures/entity/") + TextureOverrides[Layer]);
	}
	if (Model && Model->Textures.IsValidIndex(Layer))
	{
		return McCache::Asset(TEXT("textures/entity/") + Model->Textures[Layer]);
	}
	return FString();
}

UMaterialInstanceDynamic* UMcEntityModelComponent::GetMaterial(int32 Layer)
{
	if (Materials.IsValidIndex(Layer) && Materials[Layer])
	{
		return Materials[Layer];
	}
	const bool bTranslucent = Model && ((Model->TranslucentLayers >> Layer) & 1u) != 0;
	UMaterialInterface* Parent = bTranslucent && TranslucentMaterial ? TranslucentMaterial.Get() : BaseMaterial.Get();
	if (!Parent)
	{
		return nullptr;
	}
	const FString Path = GetTexturePath(Layer);
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	FIntPoint Size;
	UTexture2DArray* Texture = nullptr;
	if (Model && Model->bComposite && Layer != Model->HeldLayer)
	{
		TArray<FString> Paths;
		for (int32 Index = 0; Index < FMath::Max(Model->Textures.Num(), TextureOverrides.Num()); ++Index)
		{
			// A held item's picture is its own layer on top, not part of the coat.
			Paths.Add(Index == Model->HeldLayer ? FString() : GetTexturePath(Index));
		}
		Texture = LoadCompositeCached(Paths, Size);
	}
	else
	{
		Texture = LoadCached(Path, Size);
	}
	if (!Texture)
	{
		return nullptr;
	}
	if (bIsItem || (bIsDecor && Layer == 2 && Decor.Kind != McDecor::EKind::Painting))
	{
		// An animated item is a column of frames; the first one is the square on top.
		ItemVMax = Size.Y > Size.X && Size.X > 0 ? static_cast<float>(Size.X) / Size.Y : 1.0f;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(McRenderMode::Pick(Parent), this);
	MaterialGeneration = McRenderMode::GetGeneration();
	Material->SetTextureParameterValue(TEXT("Blocks"), Texture);
	McRenderMode::ApplyHdPicture(Material, Path);
	if (Materials.Num() <= Layer)
	{
		Materials.SetNum(Layer + 1);
	}
	Materials[Layer] = Material;
	return Material;
}

void UMcEntityModelComponent::PrepareDropped()
{
	bDropReady = true;
	McEntityModel::FDroppedUnit Unit;
	McEntityModel::BuildDroppedUnit(Item, Unit);
	bDropCube = Unit.bCube;
	bDropModel = Unit.bModel;
	DropScale = Unit.Scale;
	DropLift = Unit.Lift;
	DropUnit = MoveTemp(Unit.Sides);
	DropTop = MoveTemp(Unit.Top);
	DropLayerUV = MoveTemp(Unit.LayerUV);
}

bool McEntityModel::BuildDroppedUnit(FName Item, FDroppedUnit& Out)
{
	Out = FDroppedUnit();
	const McHand::FHeldPictures Pictures = McHand::FindHeldPictures(Item);
	Out.bCube = Pictures.Kind == McHand::EHeld::Cube;
	// The model's own ground display where the cache has it, else what the shots gave: a picture half a block, 0.125 up; a block a quarter.
	const auto Place = [&Out](FName Model, bool bBlock)
	{
		const McHand::FHeldDisplay Display = McHand::ReadHeldDisplay(Model, TEXT("ground"));
		Out.Scale = Display.bFound ? static_cast<float>(Display.Scale) : (bBlock ? McEntityModel::GetDropArg(2, 0.25f) : 0.5f);
		Out.Lift = (Display.bFound ? static_cast<float>(Display.Translation.Y) : (bBlock ? 3.0f : 2.0f)) / 16.0f;
	};
	if (Out.bCube)
	{
		Place(FName(TEXT("block/block")), true);
		McHand::BuildCubeSidesMesh(Pictures.SideTint, 1.0f, Out.Sides);
		McHand::BuildCubeTopMesh(Pictures.TopTint, 1.0f, Out.Top);
		return true;
	}
	// An item whose definition names a block's model (stairs, slabs, fences, ...) is drawn as that model, as the game does.
	FString ModelId;
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Asset(TEXT("items/") + McCache::PathOf(Item.ToString()) + TEXT(".json")));
	const TSharedPtr<FJsonObject>* Model = nullptr;
	if (Json.IsValid() && Json->TryGetObjectField(TEXT("model"), Model))
	{
		(*Model)->TryGetStringField(TEXT("model"), ModelId);
	}
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (ModelId.StartsWith(TEXT("minecraft:block/")) && Registry.EnsureLoaded() && FMcBlockModels::Get().EnsureBaked() && FMcBlockTextures::Get().EnsureLoaded())
	{
		const TOptional<FMcStateId> State = Registry.FindDefaultState(Item);
		const FMcBlockModels& Models = FMcBlockModels::Get();
		if (State.IsSet() && Models.IsBaked() && State.GetValue() < Models.NumStates() && !Models.GetModel(State.GetValue()).Quads.IsEmpty())
		{
			Out.bModel = true;
			Place(FName(TEXT("block/block")), true);
			const double Units = McCoords::UnitsPerBlock;
			for (const FMcBakedQuad& Quad : Models.GetModel(State.GetValue()).Quads)
			{
				FVector Normal = FVector::ZeroVector;
				switch (Quad.Face)
				{
				case EMcDirection::Down: Normal = FVector(0, 0, -1); break;
				case EMcDirection::Up: Normal = FVector(0, 0, 1); break;
				case EMcDirection::North: Normal = FVector(0, -1, 0); break;
				case EMcDirection::South: Normal = FVector(0, 1, 0); break;
				case EMcDirection::West: Normal = FVector(-1, 0, 0); break;
				case EMcDirection::East: Normal = FVector(1, 0, 0); break;
				default: break;
				}
				const float Layer = static_cast<float>(FMcBlockTextures::Get().GetLayer(Quad.Texture));
				const int32 First = Out.Sides.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector Local = FVector(Quad.Positions[Corner]) - FVector(0.5);
					Out.Sides.Positions.Add(FVector(Local.X, Local.Z, Local.Y) * Units);
					Out.Sides.Normals.Add(Normal);
					Out.Sides.UV0.Add(FVector2D(Quad.UVs[Corner]));
					Out.Sides.Colors.Add(FColor::White);
					Out.LayerUV.Add(FVector2D(Layer, 0.0f));
				}
				Out.Sides.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
			return true;
		}
	}
	Place(Item, false);
	TArray<FColor> Pixels;
	int32 Width = 0, Height = 0;
	const FString Path = Pictures.Kind == McHand::EHeld::Flat ? Pictures.Side : FindItemTexture(Item);
	if (Path.IsEmpty() || !McRenderTextures::LoadPngPixels(Path, Pixels, Width, Height) || Width <= 0 || Height < Width)
	{
		// No picture to cut out: the two-sided flat picture.
		// Still nothing (a chest, a pot, a bar whose item model is not a picture): a plain cube of the block's own picture or, failing that,
		// of planks, so that nothing a player drops is ever invisible.
		FMcBlockTextures& Pictures2 = FMcBlockTextures::Get();
		if (Item == FName(TEXT("minecraft:air")) || !Pictures2.EnsureLoaded())
		{
			return false;
		}
		FName Texture(*(TEXT("minecraft:block/") + McCache::PathOf(Item.ToString())));
		if (Pictures2.GetLayerId(Pictures2.GetLayer(Texture)) != Texture)
		{
			Texture = FName(TEXT("minecraft:block/oak_planks"));
		}
		const float Layer = static_cast<float>(Pictures2.GetLayer(Texture));
		Out = FDroppedUnit();
		Out.bModel = true;
		Place(FName(TEXT("block/block")), true);
		static const FVector Normals[6] = { FVector(0, 0, -1), FVector(0, 0, 1), FVector(0, -1, 0), FVector(0, 1, 0), FVector(-1, 0, 0), FVector(1, 0, 0) };
		const double Units = McCoords::UnitsPerBlock;
		for (int32 Face = 0; Face < 6; ++Face)
		{
			const FVector N = Normals[Face];
			const FVector U = Face < 2 ? FVector(1, 0, 0) : (Face < 4 ? FVector(1, 0, 0) : FVector(0, 1, 0));
			const FVector V = FVector::CrossProduct(N, U);
			const int32 First = Out.Sides.Positions.Num();
			const FVector Corner[4] = { N * 0.5 - U * 0.5 + V * 0.5, N * 0.5 - U * 0.5 - V * 0.5, N * 0.5 + U * 0.5 - V * 0.5, N * 0.5 + U * 0.5 + V * 0.5 };
			const FVector2D Uv[4] = { FVector2D(0, 0), FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0) };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Out.Sides.Positions.Add(Corner[Index] * Units);
				Out.Sides.Normals.Add(N);
				Out.Sides.UV0.Add(Uv[Index]);
				Out.Sides.Colors.Add(FColor::White);
				Out.LayerUV.Add(FVector2D(Layer, 0.0f));
			}
			Out.Sides.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
		return true;
	}
	McHand::BuildExtrudedMesh(Pixels, Width, Height > Width ? static_cast<float>(Width) / Height : 1.0f, Out.Sides);
	return true;
}

void UMcEntityModelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	MC_PERF_SCOPE(Models);
	MC_PERF_SCOPE(ModelsEntity);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Owner = GetOwner();
	if (!Owner || Kind.IsNone() || ((bIsItem || bIsArrow || bIsBobber || bIsFalling) && bItemFailed))
	{
		return;
	}
	if (!HasModel())
	{
		TickStandIn();
		return;
	}
	if (bIsFalling)
	{
		// The position comes 20 times a second; ease the picture along between, a tick behind at most.
		const FVector Target = Owner->GetActorLocation();
		FallingAt = bHasFallingAt ? FMath::VInterpTo(FallingAt, Target, DeltaTime, 30.0f) : Target;
		bHasFallingAt = true;
		SetWorldLocation(FallingAt);
	}
	Anim.Age += DeltaTime;
	if (Materials.Num() > 0 && MaterialGeneration != McRenderMode::GetGeneration())
	{
		// The look changed: the materials are made again, and the mesh with them.
		Materials.Reset();
		BlockMaterial = nullptr;
		FlashMaterial = nullptr;
		BlockVertices = 0;
		ClearAllMeshSections();
		NumSections = 0;
		bFlashing = false;
	}

	// Walking, from how far the entity moved: the same 20 ticks a second as the game.
	const FVector Feet = McCoords::UnrealToBlock(Owner->GetActorLocation());
	const FVector2D Position(Feet.X, Feet.Z);
	if (!bHasPosition)
	{
		LastPosition = Position;
		bHasPosition = true;
	}
	TickAccumulator += DeltaTime;
	int32 Ticks = 0;
	while (TickAccumulator >= 0.05f)
	{
		TickAccumulator -= 0.05f;
		if (++Ticks > 5)
		{
			continue;
		}
		double Moved = FVector2D::Distance(Position, LastPosition);
		if (Moved > 8.0)
		{
			Moved = 0.0;
		}
		// The distance was over every tick since last time: spread it.
		LastPosition = Position;
		const float Dropped = bHasY ? static_cast<float>(Feet.Y - LastY) : 0.0f;
		Squish.Tick(Dropped);
		const int32 WasAir = AirTicks;
		AirTicks = FMath::Abs(Dropped) > 0.03f ? 6 : FMath::Max(0, AirTicks - 1);
		if (HopTicks >= 0 && ++HopTicks >= McEntityModel::HopSteps - 1)
		{
			HopTicks = -1;
		}
		if (HopTicks < 0 && WasAir == 0 && AirTicks > 0)
		{
			HopTicks = 0;
		}
		LastY = Feet.Y;
		bHasY = true;
		// A boat's paddles go round the stroke while they row (Count's bits), and start again from the top.
		// Development: -McPaddling rows every boat's paddles (for a -McShot of them).
		static const bool bAlwaysPaddling = FParse::Param(FCommandLine::Get(), TEXT("McPaddling"));
		Anim.bPaddleLeft = bAlwaysPaddling || (Count & 1) != 0;
		Anim.bPaddleRight = bAlwaysPaddling || (Count & 2) != 0;
		PaddlePhase[0] = Anim.bPaddleLeft ? PaddlePhase[0] + PI / 8.0f : 0.0f;
		PaddlePhase[1] = Anim.bPaddleRight ? PaddlePhase[1] + PI / 8.0f : 0.0f;
		Anim.PaddleLeft = PaddlePhase[0];
		Anim.PaddleRight = PaddlePhase[1];
		// A chest's lid moves a tenth of the way each tick.
		PreviousLid = Lid;
		Lid = FMath::Clamp(Lid + (bOpen ? 0.1f : -0.1f), 0.0f, 1.0f);
		Walking +=(FMath::Min(static_cast<float>(Moved) * 4.0f, 1.0f) - Walking) * 0.4f;
		Anim.WalkAmount = Walking;
		Anim.bAirborne = AirTicks > 0;
		Anim.WalkPos += Walking;
	}
	Anim.HopPos = HopTicks >= 0 ? HopTicks + FMath::Clamp(TickAccumulator / 0.05f, 0.0f, 1.0f) : -1.0f;
	Anim.Hurt = FMath::Max(0.0f, Anim.Hurt - DeltaTime * 2.5f);
	Anim.Lid = FMath::Lerp(PreviousLid, Lid, TickAccumulator / 0.05f);

	// Turn towards where the entity is told to face.
	const float Turn = FMath::Min(1.0f, DeltaTime * 12.0f);
	Anim.BodyYaw += FMath::UnwindDegrees(TargetBodyYaw - Anim.BodyYaw) * Turn;
	Anim.HeadYaw += FMath::UnwindDegrees(TargetHeadYaw - Anim.HeadYaw) * Turn;
	Anim.HeadPitch += (TargetHeadPitch - Anim.HeadPitch) * Turn;

	// A creeper's fuse swells it about its feet; a slime grows with its hitbox and squishes as it hops.
	Swelling += (TargetSwelling - Swelling) * FMath::Min(1.0f, DeltaTime * 20.0f);
	if (TargetSwelling == 0.0f && Swelling < 0.01f)
	{
		Swelling = 0.0f;
	}
	{
		FVector2D Swell = bIsTnt ? FVector2D(1.0, 1.0) : McEntityModel::GetSwellScale(Swelling);
		const bool bGrows = Model && Model->BaseWidth > 0.0f && HitboxWidth > 0.0f;
		const float Grow = bGrows ? HitboxWidth / Model->BaseWidth : 1.0f;
		if (bGrows)
		{
			Swell *= Squish.GetScale(TickAccumulator / 0.05f, Grow);
		}
		SetRelativeScale3D(FVector(Swell.X * Grow, Swell.X * Grow, Swell.Y * Grow) * ModelScale);
	}
	if (bIsPotItem)
	{
		// Floats and turns as a dropped item's picture does (BuildItemMesh), at a quarter of its size.
		static float Scale = 0.25f, Lift = 0.0f;
		static bool bParsed = false;
		if (!bParsed)
		{
			bParsed = true;
			FString Arg;
			if (FParse::Value(FCommandLine::Get(), TEXT("-McPotDrop="), Arg, false))
			{
				TArray<FString> Parts;
				Arg.ParseIntoArray(Parts, TEXT(","));
				Scale = Parts.IsValidIndex(0) ? FCString::Atof(*Parts[0]) : Scale;
				Lift = Parts.IsValidIndex(1) ? FCString::Atof(*Parts[1]) : Lift;
			}
		}
		const double Height = (0.25 + FMath::Sin(Anim.Age * 2.0f) * 0.05 + Lift) * McCoords::UnitsPerBlock;
		SetRelativeLocationAndRotation(FVector(0.0, 0.0, Height), FRotator(0.0, FMath::RadiansToDegrees(Anim.Age * FMath::DegreesToRadians(70.0f)), 0.0));
		SetRelativeScale3D(FVector(Scale));
	}

	if (bIsDropped && !bDropReady)
	{
		PrepareDropped();
	}
	if (bIsDropped && bDropModel)
	{
		// A block that is not a full cube: its baked model, drawn with the world's block pictures.
		FMcBlockTextures& BlockPictures = FMcBlockTextures::Get();
		if (!BlockPictures.IsLoaded() || !BaseMaterial)
		{
			return;
		}
		if (!BlockMaterial)
		{
			BlockMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
			BlockMaterial->SetTextureParameterValue(TEXT("Blocks"), BlockPictures.GetTextureArray());
		}
		BlockMaterial->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		BlockMaterial->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
		McPlayerModel::FMeshData DropMesh;
		McEntityModel::BuildDroppedMesh(DropUnit, Anim.Age, Count, DropScale, DropLift, DropMesh);
		TArray<FVector2D> ModelUV1, ModelUV2;
		const int32 Copies = McEntityModel::GetItemCopies(Count);
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			ModelUV1.Append(DropLayerUV);
		}
		ModelUV2.Init(FVector2D(1.0, 0.0), DropMesh.Positions.Num());
		if (NumSections == 0)
		{
			CreateMeshSection(0, DropMesh.Positions, DropMesh.Triangles, DropMesh.Normals, DropMesh.UV0, ModelUV1, ModelUV2, TArray<FVector2D>(), DropMesh.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(0, BlockMaterial);
			NumSections = 1;
		}
		else
		{
			UpdateMeshSection(0, DropMesh.Positions, DropMesh.Normals, DropMesh.UV0, ModelUV1, ModelUV2, TArray<FVector2D>(), DropMesh.Colors, TArray<FProcMeshTangent>());
		}
		return;
	}
	if (UMaterialInstanceDynamic* Base = GetMaterial(0))
	{
		Base->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		Base->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	}
	else if (!Model || !Model->Textures.IsEmpty())
	{
		// (A model that is only a block, as a worn pumpkin, has no picture of its own.)
		if (bIsItem || bIsArrow || bIsBobber || bIsFalling)
		{
			bItemFailed = true;
		}
		return;
	}
	if (bIsBobber)
	{
		// Its quads change in number as the line comes and goes, so the section is made again each frame.
		McPlayerModel::FMeshData Data;
		BuildBobber(Data);
		TArray<FVector2D> UV1, UV2;
		UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
		CreateMeshSection(0, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
		if (NumSections == 0)
		{
			SetMaterial(0, Materials[0]);
			NumSections = 1;
		}
		return;
	}
	if (bIsItem || bIsArrow || bIsFalling)
	{
		McPlayerModel::FMeshData Data;
		if (bIsFalling)
		{
			// A block with a picture of its own on top (an anvil) is two sections: the sides, then the top.
			const bool bTopPicture = !FindTopTexture(Item).IsEmpty();
			if (bTopPicture)
			{
				if (UMaterialInstanceDynamic* Top = GetMaterial(1))
				{
					Top->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
					Top->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
					McPlayerModel::FMeshData TopData;
					McEntityModel::BuildBlockMesh(true, 1, TopData);
					TArray<FVector2D> TopUV1, TopUV2;
					TopUV1.Init(FVector2D::ZeroVector, TopData.Positions.Num());
					TopUV2.Init(FVector2D(1.0, 0.0), TopData.Positions.Num());
					if (NumSections < 2)
					{
						if (NumSections == 0)
						{
							// (the sides come first; the code below makes them)
							McPlayerModel::FMeshData SideData;
							McEntityModel::BuildBlockMesh(true, 0, SideData);
							TArray<FVector2D> SideUV1, SideUV2;
							SideUV1.Init(FVector2D::ZeroVector, SideData.Positions.Num());
							SideUV2.Init(FVector2D(1.0, 0.0), SideData.Positions.Num());
							CreateMeshSection(0, SideData.Positions, SideData.Triangles, SideData.Normals, SideData.UV0, SideUV1, SideUV2, TArray<FVector2D>(), SideData.Colors, TArray<FProcMeshTangent>(), false);
							SetMaterial(0, Materials[0]);
						}
						CreateMeshSection(1, TopData.Positions, TopData.Triangles, TopData.Normals, TopData.UV0, TopUV1, TopUV2, TArray<FVector2D>(), TopData.Colors, TArray<FProcMeshTangent>(), false);
						SetMaterial(1, Top);
						NumSections = 2;
					}
				}
				return;
			}
			McEntityModel::BuildBlockMesh(false, 0, Data);
		}
		else if (bIsArrow)
		{
			McEntityModel::BuildArrowMesh(TargetBodyYaw, TargetHeadPitch, Data);
		}
		else if (bIsThrown)
		{
			McEntityModel::BuildThrownMesh(ItemVMax, Data);
		}
		else if (bIsGrillFood)
		{
			McEntityModel::BuildFlatItemMesh(0.375f, 0.0f, ItemVMax, Data);
		}
		else if (bIsDropped && bDropCube)
		{
			// A block: the sides, then the top, each its own picture.
			for (int32 Part = 0; Part < 2; ++Part)
			{
				UMaterialInstanceDynamic* Material = GetMaterial(Part);
				if (!Material)
				{
					bItemFailed = true;
					return;
				}
				Material->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
				Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
				McPlayerModel::FMeshData Cube;
				McEntityModel::BuildDroppedMesh(Part == 0 ? DropUnit : DropTop, Anim.Age, Count, DropScale, DropLift, Cube);
				TArray<FVector2D> CubeUV1, CubeUV2;
				CubeUV1.Init(FVector2D::ZeroVector, Cube.Positions.Num());
				CubeUV2.Init(FVector2D(1.0, 0.0), Cube.Positions.Num());
				if (NumSections <= Part)
				{
					CreateMeshSection(Part, Cube.Positions, Cube.Triangles, Cube.Normals, Cube.UV0, CubeUV1, CubeUV2, TArray<FVector2D>(), Cube.Colors, TArray<FProcMeshTangent>(), false);
					SetMaterial(Part, Material);
					NumSections = Part + 1;
				}
				else
				{
					UpdateMeshSection(Part, Cube.Positions, Cube.Normals, Cube.UV0, CubeUV1, CubeUV2, TArray<FVector2D>(), Cube.Colors, TArray<FProcMeshTangent>());
				}
			}
			return;
		}
		else if (bIsDropped)
		{
			McEntityModel::BuildDroppedMesh(DropUnit, Anim.Age, Count, DropScale, DropLift, Data);
		}
		else
		{
			McEntityModel::BuildItemMesh(Anim.Age, Count, 0.45f, ItemVMax, Data);
		}
		TArray<FVector2D> UV1, UV2;
		UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
		if (NumSections == 0)
		{
			CreateMeshSection(0, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(0, Materials[0]);
			NumSections = 1;
		}
		else
		{
			UpdateMeshSection(0, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>());
		}
		return;
	}
	if (bIsTnt)
	{
		// Flashes white every other five ticks of the fuse.
		const int32 Fuse = FMath::RoundToInt32(TargetSwelling * 80.0f);
		const bool bFlash = (Fuse / 5) % 2 == 0;
		for (int32 Layer = 0; Layer < 3; ++Layer)
		{
			UMaterialInstanceDynamic* Material = GetMaterial(Layer);
			if (!Material)
			{
				continue;
			}
			Material->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
			Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
			if (bFlash != bFlashing || NumSections <= Layer)
			{
				FIntPoint Size;
				if (UTexture2DArray* Texture = LoadCached(GetTexturePath(Layer), Size, bFlash))
				{
					Material->SetTextureParameterValue(TEXT("Blocks"), Texture);
					McRenderMode::ApplyHdPicture(Material, GetTexturePath(Layer), bFlash);
				}
			}
			if (NumSections <= Layer)
			{
				McPlayerModel::FMeshData Data;
				McEntityModel::BuildTntMesh(Layer, Data);
				TArray<FVector2D> UV1, UV2;
				UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
				UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
				CreateMeshSection(Layer, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
				SetMaterial(Layer, Material);
				NumSections = Layer + 1;
			}
		}
		bFlashing = bFlash;
		return;
	}
	if (bIsDecor)
	{
		const int32 NumDecorLayers = McEntityModel::GetDecorLayers(Decor);
		FVector2D Picture(1.0, 1.0);
		if (Decor.Kind == McDecor::EKind::Painting)
		{
			// A painting is as many blocks across as its picture has sixteenths.
			FIntPoint Size;
			if (!LoadCached(GetTexturePath(0), Size))
			{
				return;
			}
			Picture = FVector2D(Size.X / 16.0, Size.Y / 16.0);
		}
		for (int32 Layer = 0; Layer < NumDecorLayers; ++Layer)
		{
			UMaterialInstanceDynamic* Material = GetMaterial(Layer);
			if (!Material)
			{
				continue;
			}
			const bool bGlow = Decor.Kind == McDecor::EKind::GlowItemFrame && Layer == 2;
			Material->SetScalarParameterValue(TEXT("SkyFactor"), bGlow ? 1.0f : McSky::GetSkyLightFactor());
			Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
			if (NumSections > Layer)
			{
				continue;
			}
			McPlayerModel::FMeshData Data;
			McEntityModel::BuildDecorMesh(Decor, Layer, Picture, ItemVMax, Data);
			if (Decor.Kind == McDecor::EKind::ArmorStand && Layer > 0 && Layer < 5 && Decor.Equipment[Layer - 1].Contains(TEXT("leather")))
			{
				// Leather takes its dye (the game's brown when it has none) on the base picture; the overlay stays as drawn.
				const int32 Rgb = Decor.Dye[Layer - 1] >= 0 ? Decor.Dye[Layer - 1] : 0xA06540;
				for (FColor& Color : Data.Colors)
				{
					Color = FColor(static_cast<uint8>(Color.R * ((Rgb >> 16) & 255) / 255), static_cast<uint8>(Color.G * ((Rgb >> 8) & 255) / 255), static_cast<uint8>(Color.B * (Rgb & 255) / 255), 255);
				}
			}
			if (Data.Positions.IsEmpty())
			{
				continue;
			}
			TArray<FVector2D> UV1, UV2;
			UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
			UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
			CreateMeshSection(Layer, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(Layer, Material);
			NumSections = Layer + 1;
		}
		return;
	}
	if (bIsBanner)
	{
		const int32 NumBannerLayers = 2 + BannerLayers.Num();
		for (int32 Layer = 0; Layer < NumBannerLayers; ++Layer)
		{
			UMaterialInstanceDynamic* Material = GetMaterial(Layer);
			if (!Material)
			{
				continue;
			}
			Material->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
			Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
			// The pole and bar are built once; the flag's layers again each frame, as it swings.
			if (NumSections > Layer && Layer == 0)
			{
				continue;
			}
			const float Tilt = McEntityModel::GetBannerTilt(GetWorld() ? GetWorld()->GetTimeSeconds() * 20.0 : 0.0);
			McPlayerModel::FMeshData Data;
			McEntityModel::BuildBannerMesh(bBannerWall, Layer > 0, TargetBodyYaw, Data, Tilt);
			if (Layer > 0)
			{
				// The flag takes the dye's colour (the pole and bar keep the wood).
				const FColor Tint = McBanner::GetDyeColour(Layer == 1 ? BannerBase : BannerLayers[Layer - 2].Colour).ToFColor(true);
				for (FColor& Color : Data.Colors)
				{
					Color = FColor(static_cast<uint8>(Color.R * Tint.R / 255), static_cast<uint8>(Color.G * Tint.G / 255), static_cast<uint8>(Color.B * Tint.B / 255), 255);
				}
				// Each layer lies a hair above the one below, on both faces, so they do not fight over the same depth.
				for (int32 V = 0; V < Data.Positions.Num(); ++V)
				{
					Data.Positions[V] += FVector(Data.Normals[V]) * (0.1 * (Layer - 1));
				}
			}
			TArray<FVector2D> UV1, UV2;
			UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
			UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
			if (NumSections > Layer)
			{
				UpdateMeshSection(Layer, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>());
				continue;
			}
			CreateMeshSection(Layer, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(Layer, Material);
			NumSections = Layer + 1;
		}
		return;
	}
	// A composite model (the villager's pictures laid over each other) is one layer, plus its held item's own.
	const int32 NumLayers = Model->bComposite ? FMath::Max(1, Model->HeldLayer + 1) : Model->Textures.Num();
	// Every other tenth of the fuse the creeper goes white.
	const bool bFlash = Swelling > 0.0f && McEntityModel::IsSwellFlash(Swelling);
	Anim.bFlash = bFlash;
	Anim.bBaby = bBabyLook && !Model->bBabyModel;
	Anim.bOffering = bOfferingLook;
	Anim.Pose = PoseLook & 31;
	if (Model->TintedLayers != 0)
	{
		Anim.Tint = Model->bFixedTint ? Model->FixedTint : McEntityModel::GetDyeColor((PoseLook >> 8) & 15);
	}
	// A mob the camera has not drawn lately keeps its last mesh: the pose above still moves on, and the mesh is posed
	// again on the first frame it is drawn. Building and handing over every mob's mesh every frame was the biggest
	// part of the game thread (197 mobs, 5.8-9.1 ms on the 4060, 13 Nov).
	bool bUnseen = NumSections > 0 && CVarMcModelsSkipUnseen.GetValueOnGameThread() && !WasRecentlyRendered(0.25f);
	if (bUnseen)
	{
		// Off screen, but its shadow may not be (Unreal Mode's sun): posed as if seen.
		const FVector Towards = McSky::GetShadowLightTowards();
		bUnseen = Towards.IsZero() || !CastShadow || !ShadowMayBeSeen(GetWorld(), Bounds, Towards);
	}
	{
		// Counted per frame for the recorder: the previous frame's totals are published when a new frame starts.
		static uint64 CountedFrame = 0;
		static int64 Posed = 0, Kept = 0;
		if (CountedFrame != GFrameCounter)
		{
			McPerf::SetCounter(EMcPerfCounter::ModelsPosed, Posed);
			McPerf::SetCounter(EMcPerfCounter::ModelsKept, Kept);
			CountedFrame = GFrameCounter;
			Posed = Kept = 0;
		}
		++(bUnseen ? Kept : Posed);
	}
	for (int32 Layer = 0; Layer < (bUnseen ? 0 : NumLayers); ++Layer)
	{
		if (Model->bComposite && Layer != 0 && Layer != Model->HeldLayer)
		{
			continue;
		}
		UMaterialInstanceDynamic* Material = GetMaterial(Layer);
		if (!Material)
		{
			continue;
		}
		const bool bGlow = ((Model->GlowLayers >> Layer) & 1u) != 0;
		Material->SetScalarParameterValue(TEXT("SkyFactor"), bGlow ? 1.0f : McSky::GetSkyLightFactor());
		Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
		if (Layer == 0 && bFlash != bFlashing)
		{
			// The picture swapped for its white silhouette, and back.
			FIntPoint Size;
			if (UTexture2DArray* Texture = LoadCached(GetTexturePath(0), Size, bFlash))
			{
				Material->SetTextureParameterValue(TEXT("Blocks"), Texture);
				McRenderMode::ApplyHdPicture(Material, GetTexturePath(0), bFlash);
				bFlashing = bFlash;
			}
		}
		McPlayerModel::FMeshData Data;
		McEntityModel::BuildMesh(*Model, Anim, Layer, Data);
		TArray<FVector2D> UV1, UV2;
		UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
		if (NumSections <= Layer)
		{
			CreateMeshSection(Layer, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(Layer, Material);
			NumSections = Layer + 1;
		}
		else
		{
			UpdateMeshSection(Layer, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>());
		}
	}
	if (Model->MaskLayer != INDEX_NONE && !bUnseen)
	{
		if (!Mask)
		{
			Mask = NewObject<UProceduralMeshComponent>(GetOwner());
			Mask->SetupAttachment(this);
			Mask->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mask->SetCastShadow(false);
			Mask->SetRenderInMainPass(false);
			Mask->SetRenderInDepthPass(false);
			Mask->SetRenderCustomDepth(true);
			Mask->SetCustomDepthStencilValue(McEntityModel::HullStencil);
			Mask->RegisterComponent();
		}
		McPlayerModel::FMeshData Data;
		McEntityModel::BuildMesh(*Model, Anim, Model->MaskLayer, Data);
		if (Mask->GetNumSections() == 0)
		{
			Mask->CreateMeshSection(0, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, Data.Colors, TArray<FProcMeshTangent>(), false);
			Mask->SetMaterial(0, BaseMaterial);
		}
		else
		{
			Mask->UpdateMeshSection(0, Data.Positions, Data.Normals, Data.UV0, Data.Colors, TArray<FProcMeshTangent>());
		}
	}
	else if (Mask && Model->MaskLayer == INDEX_NONE)
	{
		Mask->DestroyComponent();
		Mask = nullptr;
	}
	if (!Model->Block.IsEmpty())
	{
		TickBlock(NumLayers);
	}
}

void UMcEntityModelComponent::TickStandIn()
{
	if (Materials.Num() > 0 && MaterialGeneration != McRenderMode::GetGeneration())
	{
		Materials.Reset();
		ClearAllMeshSections();
		NumSections = 0;
	}
	if (Materials.Num() == 0)
	{
		// Drawn with the white silhouette of the minecart's picture, from a corner that is opaque, and tinted magenta: a
		// lit engine material would be black here, where the light is in the pictures' vertex colours.
		FIntPoint Size;
		UTexture2DArray* White = LoadCached(McCache::Asset(TEXT("textures/entity/minecart/minecart.png")), Size, true);
		if (!White || !BaseMaterial)
		{
			return;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
		MaterialGeneration = McRenderMode::GetGeneration();
		Material->SetTextureParameterValue(TEXT("Blocks"), White);
		Materials = { Material };
	}
	Materials[0]->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
	Materials[0]->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	const FVector2D Hitbox(FMath::Max(HitboxWidth, 0.25f), FMath::Max(HitboxHeight, 0.25f));
	if (NumSections > 0 && StandInSize == Hitbox)
	{
		return;
	}
	StandInSize = Hitbox;
	McEntityModel::FModel Box;
	const int32 Width = FMath::Max(1, FMath::RoundToInt32(Hitbox.X * 16.0f));
	const int32 Height = FMath::Max(1, FMath::RoundToInt32(Hitbox.Y * 16.0f));
	McEntityModel::FPart Part;
	Part.Boxes.Add({ FIntPoint::ZeroValue, FVector(-Width * 0.5f, 0.0f, -Width * 0.5f), FIntVector(Width, Height, Width) });
	Box.Parts.Add(Part);
	McPlayerModel::FMeshData Data;
	McEntityModel::BuildMesh(Box, McEntityModel::FAnim(), 0, Data);
	for (int32 Index = 0; Index < Data.Positions.Num(); ++Index)
	{
		Data.UV0[Index] = FVector2D(0.2, 0.6);
		Data.Colors[Index] = FColor(Data.Colors[Index].R, 0, Data.Colors[Index].R, 255);
	}
	TArray<FVector2D> UV1, UV2;
	UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
	UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
	ClearAllMeshSections();
	CreateMeshSection(0, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
	SetMaterial(0, Materials[0]);
	NumSections = 1;
}

void UMcEntityModelComponent::TickBlock(int32 Section)
{
	// A lit furnace burns; a TNT cart (its count is the fuse left) flashes white every five ticks.
	FString Text = Model->Block;
	if (Kind == TEXT("minecraft:furnace_minecart") && Count > 0)
	{
		Text.ReplaceInline(TEXT("lit=false"), TEXT("lit=true"));
	}
	const bool bBlockFlash = Kind == TEXT("minecraft:tnt_minecart") && Count > 0 && (Count / 5) % 2 == 0;
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	if (!FMcBlockModels::Get().EnsureBaked() || !Textures.EnsureLoaded())
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	TOptional<FMcStateId> State = Registry.ParseState(Text);
	if (!State.IsSet())
	{
		FString Name = Text;
		Name.Split(TEXT("["), &Name, nullptr);
		State = Registry.FindDefaultState(FName(*Name));
	}
	if (!State.IsSet())
	{
		return;
	}
	if (!BlockMaterial)
	{
		BlockMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
		BlockMaterial->SetTextureParameterValue(TEXT("Blocks"), Textures.GetTextureArray());
	}
	if (bBlockFlash && !FlashMaterial)
	{
		FIntPoint Size;
		if (UTexture2DArray* White = LoadCached(GetTexturePath(0), Size, true))
		{
			FlashMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
			FlashMaterial->SetTextureParameterValue(TEXT("Blocks"), White);
		}
	}
	UMaterialInstanceDynamic* Material = bBlockFlash && FlashMaterial ? FlashMaterial.Get() : BlockMaterial.Get();
	Material->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
	Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	McPlayerModel::FMeshData Data;
	TArray<FVector2D> UV1;
	McEntityModel::BuildBlockMesh(*Model, State.GetValue(), Anim.BodyYaw, bBlockFlash && FlashMaterial, Data, UV1);
	TArray<FVector2D> UV2;
	UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
	if (Data.Positions.Num() == BlockVertices && BlockText == Text)
	{
		UpdateMeshSection(Section, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>());
	}
	else
	{
		CreateMeshSection(Section, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
		BlockVertices = Data.Positions.Num();
		BlockText = Text;
	}
	SetMaterial(Section, Material);
}
