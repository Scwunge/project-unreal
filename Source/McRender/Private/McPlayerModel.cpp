#include "McPlayerModel.h"
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

		void AddBox(const FPartDef& Def, const FPartPose& Part, float BodyYaw, FIntPoint UVOrigin, float Inflate, FMeshData& Out)
		{
			const FVector Min = Def.BoxMin - FVector(Inflate);
			const FVector Max = Def.BoxMin + FVector(Def.Size.X, Def.Size.Y, Def.Size.Z) + FVector(Inflate);

			// Turn the part about its pivot (tip, then twist), put it there, and turn the whole model about the feet.
			const FQuat PartRotation = FQuat(FVector::YAxisVector, Part.YawRad) * FQuat(FVector::XAxisVector, Part.PitchRad);
			const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
			auto Place = [&](const FVector& Local)
			{
				return ModelRotation.RotateVector(Part.Pivot + PartRotation.RotateVector(Local));
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
				const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(Face.Normal));
				const FVector UnrealNormal(Normal.X, Normal.Z, Normal.Y);
				const FColor Color = Shade(Normal);

				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Out.Positions.Add(ToUnreal(Place(Face.Corners[Corner])));
					Out.Normals.Add(UnrealNormal);
					Out.UV0.Add(UVs[Corner] / static_cast<double>(SkinSize));
					Out.Colors.Add(Color);
				}
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
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
			AddBox(Def, Pose.Parts[Index], Pose.BodyYaw, Def.BaseUV, 0.0f, Out);
			AddBox(Def, Pose.Parts[Index], Pose.BodyYaw, Def.OuterUV, OuterInflate, Out);
		}
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

	void FAnimState::Tick(const FVector& FeetPos, float McYaw, float McPitch, bool bSneaking)
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

		// Minecraft smooths the speed and adds it to the swing position every tick.
		const float Target = FMath::Min(static_cast<float>(Moved) * 4.0f, 1.0f);
		Current.WalkAmount += (Target - Current.WalkAmount) * 0.4f;
		Current.WalkPos += Current.WalkAmount;
		Current.HeadYaw = McYaw;
		Current.HeadPitch = McPitch;
		Current.bSneaking = bSneaking;
		Current.BodyYaw = TurnBody(Current.BodyYaw, McYaw, Moved > 0.001);
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

void UMcPlayerModelComponent::SetLook(float InYaw, float InPitch, bool bInSneaking)
{
	Yaw = InYaw;
	Pitch = InPitch;
	bSneaking = bInSneaking;
}

void UMcPlayerModelComponent::SetShown(bool bInShown)
{
	if (bShown != bInShown)
	{
		bShown = bInShown;
		SetVisibility(bShown);
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
		State.Tick(Feet, Yaw, Pitch, bSneaking);
	}
	while (TickAccumulator >= 0.05f)
	{
		TickAccumulator -= 0.05f;
		if (++Ticks <= 5)
		{
			State.Tick(Feet, Yaw, Pitch, bSneaking);
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
	Material->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());

	McPlayerModel::FMeshData Data;
	McPlayerModel::BuildMesh(McPlayerModel::ComputePose(State.Blend(TickAccumulator / 0.05f)), Data);
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
}
