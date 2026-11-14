#include "McPlayerModel.h"
#include "McBlockRegistry.h"
#include "McChestComponent.h"
#include "McEntityModel.h"
#include "McRenderMode.h"
#include "McCache.h"
#include "McChunk.h"
#include "McRender.h"
#include "McRenderTextures.h"
#include "McSkyComponent.h"
#include "Engine/Texture2DArray.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "McPerf.h"

namespace McPlayerModel
{
	namespace
	{
		constexpr float SecondsPerTick = 0.05f;
		constexpr int32 MaxTicksPerFrame = 5;
		// Further than this in a tick is a teleport, not a run.
		constexpr double TeleportBlocks = 8.0;

		// Where the parts sit standing, in pixels above the feet; Minecraft's player model, 24 pixels up from the
		// ground at the shoulders. The four limbs hang from their pivots.
		const FPartDef PartDefs[McNumPlayerParts] = {
			// Head
			{ FIntPoint(0, 0), FIntPoint(32, 0), FIntVector(8, 8, 8), FVector(0.0, 24.0, 0.0), FVector(-4.0, 0.0, -4.0) },
			// Body
			{ FIntPoint(16, 16), FIntPoint(16, 32), FIntVector(8, 12, 4), FVector(0.0, 24.0, 0.0), FVector(-4.0, -12.0, -2.0) },
			// Right arm (the wide, 4 pixel one), on the -x side
			{ FIntPoint(40, 16), FIntPoint(40, 32), FIntVector(4, 12, 4), FVector(-5.0, 22.0, 0.0), FVector(-3.0, -10.0, -2.0) },
			// Left arm
			{ FIntPoint(32, 48), FIntPoint(48, 48), FIntVector(4, 12, 4), FVector(5.0, 22.0, 0.0), FVector(-1.0, -10.0, -2.0) },
			// Right leg
			{ FIntPoint(0, 16), FIntPoint(0, 32), FIntVector(4, 12, 4), FVector(-1.9, 12.0, 0.0), FVector(-2.0, -12.0, -2.0) },
			// Left leg
			{ FIntPoint(16, 48), FIntPoint(0, 48), FIntVector(4, 12, 4), FVector(1.9, 12.0, 0.0), FVector(-2.0, -12.0, -2.0) },
		};

		float WrapDegrees(float Angle)
		{
			return FMath::UnwindDegrees(Angle);
		}
	}

	const FPartDef& GetPartDef(EMcPlayerPart Part)
	{
		return PartDefs[static_cast<int32>(Part)];
	}

	FIntRect GetFaceRect(FIntPoint Origin, FIntVector Size, EBoxFace Face)
	{
		const int32 W = Size.X, H = Size.Y, D = Size.Z;
		switch (Face)
		{
		case EBoxFace::Up:    return FIntRect(Origin.X + D, Origin.Y, Origin.X + D + W, Origin.Y + D);
		case EBoxFace::Down:  return FIntRect(Origin.X + D + W, Origin.Y, Origin.X + D + W * 2, Origin.Y + D);
		case EBoxFace::West:  return FIntRect(Origin.X, Origin.Y + D, Origin.X + D, Origin.Y + D + H);
		case EBoxFace::South: return FIntRect(Origin.X + D, Origin.Y + D, Origin.X + D + W, Origin.Y + D + H);
		case EBoxFace::East:  return FIntRect(Origin.X + D + W, Origin.Y + D, Origin.X + D * 2 + W, Origin.Y + D + H);
		default:              return FIntRect(Origin.X + D * 2 + W, Origin.Y + D, Origin.X + D * 2 + W * 2, Origin.Y + D + H);
		}
	}

	float GetLegSwing(float WalkPos, float WalkAmount, bool bRight)
	{
		return FMath::Cos(WalkPos * 0.6662f + (bRight ? 0.0f : PI)) * 1.4f * WalkAmount;
	}

	float GetArmSwing(float WalkPos, float WalkAmount, bool bRight)
	{
		return FMath::Cos(WalkPos * 0.6662f + (bRight ? PI : 0.0f)) * 2.0f * WalkAmount * 0.5f;
	}

	FPose ComputePose(const FAnim& Anim)
	{
		FPose Pose;
		Pose.BodyYaw = Anim.BodyYaw;

		// Crouching moves the parts as Minecraft does: the torso drops 3.2 pixels and tips forward half a radian,
		// the head follows it down, the arms swing a little forward and the legs step back.
		const bool bSneak = Anim.bSneaking;
		const float ArmTip = bSneak ? 0.4f : 0.0f;

		FPartPose& Head = Pose.Parts[static_cast<int32>(EMcPlayerPart::Head)];
		Head.Pivot = GetPartDef(EMcPlayerPart::Head).Pivot - FVector(0.0, bSneak ? 4.2 : 0.0, 0.0);
		Head.PitchRad = FMath::DegreesToRadians(Anim.HeadPitch);
		Head.YawRad = -FMath::DegreesToRadians(WrapDegrees(Anim.HeadYaw - Anim.BodyYaw));

		FPartPose& Body = Pose.Parts[static_cast<int32>(EMcPlayerPart::Body)];
		Body.Pivot = GetPartDef(EMcPlayerPart::Body).Pivot - FVector(0.0, bSneak ? 3.2 : 0.0, 0.0);
		Body.PitchRad = bSneak ? 0.5f : 0.0f;

		for (const bool bRight : { true, false })
		{
			const EMcPlayerPart ArmPart = bRight ? EMcPlayerPart::RightArm : EMcPlayerPart::LeftArm;
			FPartPose& Arm = Pose.Parts[static_cast<int32>(ArmPart)];
			Arm.Pivot = GetPartDef(ArmPart).Pivot - FVector(0.0, bSneak ? 3.2 : 0.0, 0.0);
			Arm.PitchRad = GetArmSwing(Anim.WalkPos, Anim.WalkAmount, bRight) + ArmTip;

			const EMcPlayerPart LegPart = bRight ? EMcPlayerPart::RightLeg : EMcPlayerPart::LeftLeg;
			FPartPose& Leg = Pose.Parts[static_cast<int32>(LegPart)];
			Leg.Pivot = GetPartDef(LegPart).Pivot + (bSneak ? FVector(0.0, -0.2, -4.0) : FVector::ZeroVector);
			Leg.PitchRad = GetLegSwing(Anim.WalkPos, Anim.WalkAmount, bRight);
		}

		// Gliding lies the body along the look, head first, with the head bent back to see ahead.
		if (Anim.bGliding)
		{
			Pose.TiltRad = FMath::DegreesToRadians(90.0f + Anim.HeadPitch);
			Head.PitchRad = -PI / 4.0f;
		}

		// The wings hang from the back of the shoulders and follow the torso when it bends.
		const FQuat BodyBend(FVector::XAxisVector, Body.PitchRad);
		Pose.Wing.Pivot = Body.Pivot + BodyBend.RotateVector(FVector(5.0, 0.0, -2.0));
		Pose.Wing.PitchRad = Body.PitchRad + (Anim.bGliding ? FMath::DegreesToRadians(20.0f) : FMath::DegreesToRadians(15.0f));
		Pose.Wing.RollRad = Anim.bGliding ? FMath::DegreesToRadians(60.0f) : FMath::DegreesToRadians(15.0f);
		return Pose;
	}

	FVector ToUnreal(const FVector& ModelPx)
	{
		return FVector(ModelPx.X, ModelPx.Z, ModelPx.Y) * (ModelScale / 16.0 * McCoords::UnitsPerBlock);
	}

	namespace
	{
		struct FCorner
		{
			FVector Pos;
			FVector2D UV;
		};

		// Minecraft's shading: 1 from above, 0.5 from below, 0.8 from north and south, 0.6 from east and west.
		FColor Shade(const FVector& Normal)
		{
			const double Y = Normal.Y;
			const double Value = Normal.X * Normal.X * 0.6 + Normal.Z * Normal.Z * 0.8 + Y * Y * (Y > 0.0 ? 1.0 : 0.5);
			const uint8 Byte = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Value * 255.0), 0, 255));
			return FColor(Byte, Byte, Byte, 255);
		}

		// bMirror draws the part's mirror image across the middle of the body (the right wing from the left).
		void AddBox(const FPartDef& Def, const FPartPose& Part, const FPose& Pose, FIntPoint UVOrigin, float Inflate, FMeshData& Out,
			FVector2D TextureSize = FVector2D(SkinSize, SkinSize), bool bMirror = false)
		{
			const FVector Min = Def.BoxMin - FVector(Inflate);
			const FVector Max = Def.BoxMin + FVector(Def.Size.X, Def.Size.Y, Def.Size.Z) + FVector(Inflate);

			// Turn the part about its pivot (tip, twist, then roll: a wing tipped back is then swung out), put it there,
			// and lean and turn the whole model about the feet.
			const FQuat PartRotation = FQuat(FVector::ZAxisVector, Part.RollRad) * FQuat(FVector::YAxisVector, Part.YawRad) * FQuat(FVector::XAxisVector, Part.PitchRad);
			const FQuat ModelRotation = FQuat(FVector::YAxisVector, -FMath::DegreesToRadians(Pose.BodyYaw)) * FQuat(FVector::XAxisVector, Pose.TiltRad);
			const FVector Flip(bMirror ? -1.0 : 1.0, 1.0, 1.0);
			auto Place = [&](const FVector& Local)
			{
				return ModelRotation.RotateVector((Part.Pivot + PartRotation.RotateVector(Local)) * Flip);
			};

			// The corners go anticlockwise seen from outside, bottom left first; the picture's corners go with them.
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
				// The outer layer's picture is the same size as the part's: only the box grows.
				const FIntRect Rect = GetFaceRect(UVOrigin, Def.Size, Face.Face);
				const FVector2D UVs[4] = {
					FVector2D(Rect.Min.X, Rect.Max.Y), FVector2D(Rect.Max.X, Rect.Max.Y),
					FVector2D(Rect.Max.X, Rect.Min.Y), FVector2D(Rect.Min.X, Rect.Min.Y),
				};
				const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(Face.Normal) * Flip);
				const FVector UnrealNormal(Normal.X, Normal.Z, Normal.Y);
				const FColor Color = Shade(Normal);

				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Out.Positions.Add(ToUnreal(Place(Face.Corners[Corner])));
					Out.Normals.Add(UnrealNormal);
					Out.UV0.Add(UVs[Corner] / TextureSize);
					Out.Colors.Add(Color);
				}
				// A mirror image turns the corners clockwise: wind them the other way to keep the outside out.
				if (bMirror)
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

	void BuildMesh(const FPose& Pose, FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		for (int32 Index = 0; Index < McNumPlayerParts; ++Index)
		{
			const FPartDef& Def = PartDefs[Index];
			AddBox(Def, Pose.Parts[Index], Pose, Def.BaseUV, 0.0f, Out);
			AddBox(Def, Pose.Parts[Index], Pose, Def.OuterUV, Index == static_cast<int32>(EMcPlayerPart::Head) ? OuterInflate : OuterInflateBody, Out);
		}
	}

	void BuildWings(const FPose& Pose, FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		// The left wing spans the middle of the back from its pivot over the left shoulder, behind the torso.
		static const FPartDef Wing = { FIntPoint(22, 0), FIntPoint(22, 0), FIntVector(10, 20, 2), FVector::ZeroVector, FVector(-10.0, -20.0, -2.0) };
		for (const bool bMirror : { false, true })
		{
			AddBox(Wing, Pose.Wing, Pose, Wing.BaseUV, 1.0f, Out, FVector2D(WingsWidth, WingsHeight), bMirror);
		}
	}

	void BuildArmor(const FPose& Pose, int32 Slot, FMeshData& Out, float Grow)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const FVector2D Size(ArmorWidth, ArmorHeight);
		const auto Add = [&](EMcPlayerPart Part, FIntPoint UV, float Inflate)
		{
			// A left limb is drawn as the right one's mirror image: its pose mirrored across the middle, then flipped back.
			const bool bLeft = Part == EMcPlayerPart::LeftArm || Part == EMcPlayerPart::LeftLeg;
			const EMcPlayerPart Shape = Part == EMcPlayerPart::LeftArm ? EMcPlayerPart::RightArm : Part == EMcPlayerPart::LeftLeg ? EMcPlayerPart::RightLeg : Part;
			FPartPose Placed = Pose.Parts[static_cast<int32>(Part)];
			if (bLeft)
			{
				Placed.Pivot.X = -Placed.Pivot.X;
				Placed.YawRad = -Placed.YawRad;
				Placed.RollRad = -Placed.RollRad;
			}
			AddBox(GetPartDef(Shape), Placed, Pose, UV, Inflate + Grow, Out, Size, bLeft);
		};
		switch (Slot)
		{
		case 0:
			Add(EMcPlayerPart::Head, FIntPoint(0, 0), 1.0f);
			Add(EMcPlayerPart::Head, FIntPoint(32, 0), 1.5f);
			break;
		case 1:
			Add(EMcPlayerPart::Body, FIntPoint(16, 16), 1.0f);
			// The arms are grown as far as the body, so their fronts meet it at one depth where they overlap; the game draws
			// them after the body, over it. A hair more puts them in front here too (else the body's edge shows as a dark line).
			Add(EMcPlayerPart::RightArm, FIntPoint(40, 16), 1.05f);
			Add(EMcPlayerPart::LeftArm, FIntPoint(40, 16), 1.05f);
			break;
		case 2:
			Add(EMcPlayerPart::Body, FIntPoint(16, 16), 0.5f);
			Add(EMcPlayerPart::RightLeg, FIntPoint(0, 16), 0.4f);
			Add(EMcPlayerPart::LeftLeg, FIntPoint(0, 16), 0.4f);
			break;
		case 3:
			Add(EMcPlayerPart::RightLeg, FIntPoint(0, 16), 0.9f);
			Add(EMcPlayerPart::LeftLeg, FIntPoint(0, 16), 0.9f);
			break;
		default:
			break;
		}
	}

	FString GetArmorTexture(FName Item, int32 Slot, bool bOverlay)
	{
		static const TCHAR* const Suffixes[] = { TEXT("_helmet"), TEXT("_chestplate"), TEXT("_leggings"), TEXT("_boots") };
		if (Item.IsNone() || Slot < 0 || Slot > 3)
		{
			return FString();
		}
		FString Name = Item.ToString();
		Name.RemoveFromStart(TEXT("minecraft:"));
		if (!Name.RemoveFromEnd(Suffixes[Slot]))
		{
			return FString();
		}
		Name = Name == TEXT("golden") ? FString(TEXT("gold")) : Name == TEXT("turtle") ? FString(TEXT("turtle_scute")) : Name;
		if (bOverlay && Name != TEXT("leather"))
		{
			return FString();
		}
		return McCache::Asset(FString(TEXT("textures/entity/equipment/")) + (Slot == 2 ? TEXT("humanoid_leggings/") : TEXT("humanoid/")) + Name
			+ (bOverlay ? TEXT("_overlay") : TEXT("")) + TEXT(".png"));
	}

	void BuildPartMesh(EMcPlayerPart Part, FMeshData& Out)
	{
		const FPartDef& Def = GetPartDef(Part);
		FPartPose Pose;
		Pose.Pivot = FVector::ZeroVector;
		const FPose Still;
		AddBox(Def, Pose, Still, Def.BaseUV, 0.0f, Out);
		AddBox(Def, Pose, Still, Def.OuterUV, OuterInflateBody, Out);
	}

	float TurnBody(float BodyYaw, float HeadYaw, bool bMoving)
	{
		float Diff = WrapDegrees(HeadYaw - BodyYaw);
		if (bMoving)
		{
			// Walking faces the way you look.
			BodyYaw += Diff * 0.3f;
			Diff = WrapDegrees(HeadYaw - BodyYaw);
		}
		if (FMath::Abs(Diff) > MaxHeadTurn)
		{
			BodyYaw += Diff - FMath::Sign(Diff) * MaxHeadTurn;
		}
		return WrapDegrees(BodyYaw);
	}

	void FAnimState::Tick(const FVector& FeetPos, float McYaw, float McPitch, bool bSneaking, bool bGliding)
	{
		const FVector2D Position(FeetPos.X, FeetPos.Z);
		double Moved = bHasPosition ? FVector2D::Distance(Position, LastPosition) : 0.0;
		if (Moved > TeleportBlocks)
		{
			Moved = 0.0;
			bHasPosition = false;
		}
		LastPosition = Position;

		if (!bHasPosition)
		{
			Current.BodyYaw = McYaw;
			Current.WalkAmount = 0.0f;
			Previous = Current;
			bHasPosition = true;
		}
		Previous = Current;

		// Minecraft smooths the speed and adds it to the swing position every tick. Gliding legs don't walk.
		const float Target = bGliding ? 0.0f : FMath::Min(static_cast<float>(Moved) * 4.0f, 1.0f);
		Current.WalkAmount += (Target - Current.WalkAmount) * 0.4f;
		Current.WalkPos += Current.WalkAmount;
		Current.HeadYaw = McYaw;
		Current.HeadPitch = McPitch;
		Current.bSneaking = bSneaking;
		Current.bGliding = bGliding;
		// A glider's body points where they look.
		Current.BodyYaw = bGliding ? McYaw : TurnBody(Current.BodyYaw, McYaw, Moved > 0.001);
	}

	FAnim FAnimState::Blend(float Alpha) const
	{
		FAnim Result = Current;
		Result.WalkPos = FMath::Lerp(Previous.WalkPos, Current.WalkPos, Alpha);
		Result.WalkAmount = FMath::Lerp(Previous.WalkAmount, Current.WalkAmount, Alpha);
		Result.HeadPitch = FMath::Lerp(Previous.HeadPitch, Current.HeadPitch, Alpha);
		Result.HeadYaw = Previous.HeadYaw + WrapDegrees(Current.HeadYaw - Previous.HeadYaw) * Alpha;
		Result.BodyYaw = Previous.BodyYaw + WrapDegrees(Current.BodyYaw - Previous.BodyYaw) * Alpha;
		return Result;
	}

	FName GetWornHeadModel(FName Item, float& OutScale)
	{
		if (Item == TEXT("minecraft:carved_pumpkin"))
		{
			OutScale = 1.0f;
			return FName(TEXT("mc:worn/carved_pumpkin"));
		}
		// A head item is named as its floor block, so the placed head's model is the one.
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const TOptional<FMcStateId> State = Item.IsNone() ? TOptional<FMcStateId>() : Registry.FindDefaultState(Item);
		FVector Centre;
		float Yaw;
		const FName Model = State.IsSet() ? UMcChestComponent::GetHeadModel(State.GetValue(), Centre, Yaw) : NAME_None;
		// 76 pixels to the bare head's 64 from the front, which, the worn face being 0.7 pixels nearer the camera 4
		// blocks off, is 1.175 (ref_wh_zombie against ref_wh_bare; its top 1.4 pixels over the head's, so its bottom
		// on the neck).
		OutScale = 1.175f;
		return Model;
	}

	FTransform GetWornHeadTransform(const FPose& Pose, float Scale)
	{
		const FPartPose& Head = Pose.Parts[static_cast<int32>(EMcPlayerPart::Head)];
		const FQuat PartRotation = FQuat(FVector::ZAxisVector, Head.RollRad) * FQuat(FVector::YAxisVector, Head.YawRad) * FQuat(FVector::XAxisVector, Head.PitchRad);
		const FQuat ModelRotation = FQuat(FVector::YAxisVector, -FMath::DegreesToRadians(Pose.BodyYaw)) * FQuat(FVector::XAxisVector, Pose.TiltRad);
		const FQuat Turn = ModelRotation * PartRotation;
		const FQuat Rotation = UMcChestComponent::RotationFromAxes(Turn.RotateVector(FVector::XAxisVector), Turn.RotateVector(FVector::YAxisVector), Turn.RotateVector(FVector::ZAxisVector));
		return FTransform(Rotation, ToUnreal(ModelRotation.RotateVector(Head.Pivot)), FVector(Scale * ModelScale));
	}
}

UMcPlayerModelComponent::UMcPlayerModelComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The actor turns to face where the player looks; the model does its own turning.
	SetUsingAbsoluteRotation(true);
	bUseAsyncCooking = false;

	// The block cutout material, whose texture array is swapped for the skin: it takes the same vertex data.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cutout(TEXT("/Game/Mc/Materials/M_McBlockCutout.M_McBlockCutout"));
	BaseMaterial = Cutout.Object;
}

void UMcPlayerModelComponent::SetLook(float InYaw, float InPitch, bool bInSneaking, bool bInGliding)
{
	Yaw = InYaw;
	Pitch = InPitch;
	bSneaking = bInSneaking;
	bGliding = bInGliding;
}

UMaterialInstanceDynamic* UMcPlayerModelComponent::EnsureWingsMaterial()
{
	if (WingsMaterial && WingsMaterial->Parent == McRenderMode::Pick(BaseMaterial.Get()))
	{
		return WingsMaterial;
	}
	if (bWingsFailed || !BaseMaterial)
	{
		return nullptr;
	}
	const FString Path = McCache::Asset(TEXT("textures/entity/equipment/wings/elytra.png"));
	if (!WingsPicture)
	{
		int32 Width = 0, Height = 0;
		WingsPicture = McRenderTextures::LoadPngAsArray(this, Path, Width, Height);
		if (!WingsPicture || Width != McPlayerModel::WingsWidth || Height != McPlayerModel::WingsHeight)
		{
			UE_LOG(LogMcRender, Error, TEXT("Can't use the elytra picture %s (a 64x32 png); worn elytra stay invisible"), *Path);
			bWingsFailed = true;
			return nullptr;
		}
	}
	WingsMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial.Get()), this);
	WingsMaterial->SetTextureParameterValue(TEXT("Blocks"), WingsPicture);
	McRenderMode::ApplyHdPicture(WingsMaterial, Path);
	if (bHasWingsSection)
	{
		SetMaterial(1, WingsMaterial);
	}
	return WingsMaterial;
}

UMaterialInstanceDynamic* UMcPlayerModelComponent::EnsureArmorMaterial(int32 Layer)
{
	const FString Path = McPlayerModel::GetArmorTexture(ArmorItems[Layer % 4], Layer % 4, Layer >= 4);
	if (Path.IsEmpty() || !BaseMaterial)
	{
		return nullptr;
	}
	UMaterialInterface* Parent = McRenderMode::Pick(BaseMaterial.Get());
	if (ArmorMaterials[Layer] && ArmorPaths[Layer] == Path && ArmorMaterials[Layer]->Parent == Parent)
	{
		return ArmorMaterials[Layer];
	}
	UTexture2DArray* Picture = nullptr;
	if (const TObjectPtr<UTexture2DArray>* Found = ArmorPictures.Find(Path))
	{
		Picture = Found->Get();
	}
	else
	{
		int32 Width = 0, Height = 0;
		Picture = McRenderTextures::LoadPngAsArray(this, Path, Width, Height);
		if (!Picture || Width != McPlayerModel::ArmorWidth || Height != McPlayerModel::ArmorHeight)
		{
			UE_LOG(LogMcRender, Warning, TEXT("Can't use the armor picture %s (a 64x32 png); that armor stays invisible"), *Path);
			Picture = nullptr;
		}
		ArmorPictures.Add(Path, Picture);
	}
	if (!Picture)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Made = UMaterialInstanceDynamic::Create(Parent, this);
	Made->SetTextureParameterValue(TEXT("Blocks"), Picture);
	McRenderMode::ApplyHdPicture(Made, Path);
	ArmorMaterials[Layer] = Made;
	ArmorPaths[Layer] = Path;
	if (bHasArmorSection[Layer])
	{
		SetMaterial(2 + Layer, Made);
	}
	return Made;
}

void UMcPlayerModelComponent::SetShown(bool bInShown)
{
	if (bShown != bInShown)
	{
		bShown = bInShown;
		SetVisibility(bShown, true);
	}
}

bool UMcPlayerModelComponent::EnsureMaterial()
{
	if (Material)
	{
		return true;
	}
	if (bFailed)
	{
		return false;
	}
	bFailed = true;

	const FString Path = McCache::Asset(TEXT("textures/entity/player/wide/steve.png"));
	int32 Width = 0, Height = 0;
	UTexture2DArray* Array = McRenderTextures::LoadPngAsArray(this, Path, Width, Height);
	if (!Array || Width != McPlayerModel::SkinSize || Height != McPlayerModel::SkinSize)
	{
		UE_LOG(LogMcRender, Error, TEXT("Can't use the player skin %s (a 64x64 png); other players stay invisible"), *Path);
		return false;
	}
	Skin = Array;

	UMaterialInterface* Base = BaseMaterial ? BaseMaterial.Get() : nullptr;
	if (!Base)
	{
		UE_LOG(LogMcRender, Error, TEXT("M_McBlockCutout is missing; run tools/make_materials.py"));
		return false;
	}
	Material = UMaterialInstanceDynamic::Create(McRenderMode::Pick(Base), this);
	Material->SetTextureParameterValue(TEXT("Blocks"), Skin);
	McRenderMode::ApplyHdPicture(Material, Path);
	SkinPath = Path;
	MaterialGeneration = McRenderMode::GetGeneration();
	bFailed = false;
	return true;
}

void UMcPlayerModelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	MC_PERF_SCOPE(Models);
	MC_PERF_SCOPE(ModelsPlayer);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Owner = GetOwner();
	if (!Owner || !bShown || !EnsureMaterial())
	{
		return;
	}

	// The same 20 ticks a second as the game; draw between the last two.
	TickAccumulator += DeltaTime;
	int32 Ticks = 0;
	const FVector Feet = McCoords::UnrealToBlock(Owner->GetActorLocation());
	if (!State.bHasPosition)
	{
		State.Tick(Feet, Yaw, Pitch, bSneaking, bGliding);
	}
	while (TickAccumulator >= 0.05f)
	{
		TickAccumulator -= 0.05f;
		if (++Ticks <= 5)
		{
			State.Tick(Feet, Yaw, Pitch, bSneaking, bGliding);
		}
	}
	if (bPosed)
	{
		for (McPlayerModel::FAnim* Anim : { &State.Current, &State.Previous })
		{
			Anim->BodyYaw = PosedBodyYaw;
			Anim->HeadYaw = Yaw;
			Anim->HeadPitch = Pitch;
			Anim->bSneaking = bSneaking;
			Anim->WalkAmount = 0.0f;
		}
	}

	if (MaterialGeneration != McRenderMode::GetGeneration())
	{
		// The look changed: the same skin on the other mode's material.
		Material = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial.Get()), this);
		Material->SetTextureParameterValue(TEXT("Blocks"), Skin);
		McRenderMode::ApplyHdPicture(Material, SkinPath);
		MaterialGeneration = McRenderMode::GetGeneration();
		if (bHasSection)
		{
			SetMaterial(0, Material);
		}
	}

	// Lit by the sky like the blocks around them.
	Material->SetScalarParameterValue(TEXT("SkyFactor"), bFullyLit ? 1.0f : McSky::GetSkyLightFactor());
	Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());

	McPlayerModel::FMeshData Data;
	const McPlayerModel::FPose Pose = McPlayerModel::ComputePose(State.Blend(TickAccumulator / 0.05f));
	McPlayerModel::BuildMesh(Pose, Data);

	// A mob head or carved pumpkin worn on the head: its own model, turned with the head.
	float WornScale = 1.0f;
	const FName WornModel = McPlayerModel::GetWornHeadModel(WornHead, WornScale);
	if (!WornModel.IsNone() && !Worn)
	{
		Worn = NewObject<UMcEntityModelComponent>(GetOwner());
		Worn->SetupAttachment(this);
		Worn->RegisterComponent();
	}
	if (Worn)
	{
		Worn->SetVisibility(!WornModel.IsNone() && bShown);
		if (!WornModel.IsNone())
		{
			Worn->SetKind(WornModel, NAME_None, 0);
			// The model sets its own scale each frame (a swelling creeper's), times this.
			const FTransform Placed = McPlayerModel::GetWornHeadTransform(Pose, WornScale);
			Worn->SetModelScale(Placed.GetScale3D().X);
			Worn->SetRelativeLocationAndRotation(Placed.GetLocation(), Placed.GetRotation());
		}
	}
	const TArray<FVector2D> Layer = { };
	TArray<FVector2D> UV1, UV2;
	UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
	UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
	const TArray<FProcMeshTangent> NoTangents;
	if (!bHasSection)
	{
		CreateMeshSection(0, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, Layer, Data.Colors, NoTangents, false);
		SetMaterial(0, Material);
		bHasSection = true;
	}
	else
	{
		UpdateMeshSection(0, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, Layer, Data.Colors, NoTangents);
	}

	// Elytra on the back, in their own picture.
	UMaterialInstanceDynamic* Wings = bWings ? EnsureWingsMaterial() : nullptr;
	if (Wings)
	{
		Wings->SetScalarParameterValue(TEXT("SkyFactor"), bFullyLit ? 1.0f : McSky::GetSkyLightFactor());
		Wings->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
		McPlayerModel::BuildWings(Pose, Data);
		UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
		if (!bHasWingsSection)
		{
			CreateMeshSection(1, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, Layer, Data.Colors, NoTangents, false);
			SetMaterial(1, Wings);
			bHasWingsSection = true;
		}
		else
		{
			UpdateMeshSection(1, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, Layer, Data.Colors, NoTangents);
		}
	}
	if (bHasWingsSection)
	{
		SetMeshSectionVisible(1, Wings != nullptr);
	}

	// Armor over the body, a section a piece (and leather's overlay a section more), each in its own picture.
	for (int32 ArmorLayer = 0; ArmorLayer < 8; ++ArmorLayer)
	{
		const int32 Slot = ArmorLayer % 4;
		const bool bOverlay = ArmorLayer >= 4;
		UMaterialInstanceDynamic* Armor = EnsureArmorMaterial(ArmorLayer);
		if (Armor)
		{
			Armor->SetScalarParameterValue(TEXT("SkyFactor"), bFullyLit ? 1.0f : McSky::GetSkyLightFactor());
			Armor->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
			McPlayerModel::BuildArmor(Pose, Slot, Data, bOverlay ? 0.02f : 0.0f);
			if (!bOverlay && ArmorPaths[ArmorLayer].Contains(TEXT("/leather")))
			{
				// Undyed leather takes the game's brown on its base picture; the overlay stays as drawn.
				constexpr int32 Rgb = 0xA06540;
				for (FColor& Color : Data.Colors)
				{
					Color = FColor(static_cast<uint8>(Color.R * ((Rgb >> 16) & 255) / 255), static_cast<uint8>(Color.G * ((Rgb >> 8) & 255) / 255), static_cast<uint8>(Color.B * (Rgb & 255) / 255), 255);
				}
			}
			UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
			UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
			const int32 Section = 2 + ArmorLayer;
			if (!bHasArmorSection[ArmorLayer])
			{
				CreateMeshSection(Section, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, Layer, Data.Colors, NoTangents, false);
				SetMaterial(Section, Armor);
				bHasArmorSection[ArmorLayer] = true;
			}
			else
			{
				UpdateMeshSection(Section, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, Layer, Data.Colors, NoTangents);
			}
		}
		if (bHasArmorSection[ArmorLayer])
		{
			SetMeshSectionVisible(2 + ArmorLayer, Armor != nullptr);
		}
	}
}
