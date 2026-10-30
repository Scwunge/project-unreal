#include "McHand.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McCache.h"
#include "McChunk.h"
#include "McRender.h"
#include "McRenderMode.h"
#include "McRenderTextures.h"
#include "McSkyComponent.h"
#include "Engine/Texture2DArray.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

namespace McHand
{
	namespace
	{
		// The hand's places, in blocks of the game's size before Scale: forward, right, up of the camera.
		const FVector RestTip(0.74, 0.40, -0.28);
		const FVector RestDirection = FVector(0.55, -0.30, 0.80).GetSafeNormal();
		// 10 pixels of the arm's 12 hang below its pivot (the model scale makes a pixel 0.9375 / 16 of a block).
		constexpr double TipLength = 10.0 * McPlayerModel::ModelScale / 16.0;
		constexpr double Units = McCoords::UnitsPerBlock;
		// Where the held thing's centre is before its display transform: forward, right, up of the camera, in blocks.
		const FVector HeldAnchor(0.72, 0.38, -0.25);

		FQuat MakeBasis(const FVector& X, const FVector& Y, const FVector& Z)
		{
			return FQuat(FMatrix(FPlane(X, 0.0), FPlane(Y, 0.0), FPlane(Z, 0.0), FPlane(0, 0, 0, 1)));
		}

		void AddQuad(McPlayerModel::FMeshData& Out, const FVector (&P)[4], const FVector& Normal, const FVector2D (&UV)[4], const FColor& Color)
		{
			// Both windings, so the picture shows from either side.
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Out.Positions.Add(P[Corner] * Units);
					Out.Normals.Add(Side == 0 ? Normal : -Normal);
					Out.UV0.Add(UV[Corner]);
					Out.Colors.Add(Color);
				}
				if (Side == 0)
				{
					Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
				}
				else
				{
					Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
				}
			}
		}

		FColor Multiply(const FColor& Tint, float Shade)
		{
			return FColor(static_cast<uint8>(Tint.R * Shade), static_cast<uint8>(Tint.G * Shade), static_cast<uint8>(Tint.B * Shade), 255);
		}

		void BuildCube(bool bTop, const FColor& Tint, float VMax, McPlayerModel::FMeshData& Out)
		{
			Out.Positions.Reset();
			Out.Normals.Reset();
			Out.UV0.Reset();
			Out.Colors.Reset();
			Out.Triangles.Reset();
			const double H = 0.5;
			const FVector2D UV[4] = { FVector2D(0, VMax), FVector2D(1, VMax), FVector2D(1, 0), FVector2D(0, 0) };
			if (bTop)
			{
				const FVector Corners[4] = { FVector(-H, -H, H), FVector(-H, H, H), FVector(H, H, H), FVector(H, -H, H) };
				AddQuad(Out, Corners, FVector::UpVector, UV, Multiply(Tint, 1.0f));
				return;
			}
			// Sides: x and y faces, in the game's shading (0.6 for one pair, 0.8 for the other).
			const FVector Dirs[4] = { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0) };
			for (int32 Face = 0; Face < 4; ++Face)
			{
				const FVector N = Dirs[Face];
				const FVector Across = FVector::CrossProduct(FVector::UpVector, N);
				const FVector Corners[4] = { N * H - Across * H - FVector(0, 0, H), N * H + Across * H - FVector(0, 0, H), N * H + Across * H + FVector(0, 0, H), N * H - Across * H + FVector(0, 0, H) };
				AddQuad(Out, Corners, N, UV, Multiply(Tint, Face < 2 ? 0.6f : 0.8f));
			}
		}

		FString FindPicture(const TArray<FString>& Candidates)
		{
			for (const FString& Candidate : Candidates)
			{
				const FString Path = McCache::Asset(TEXT("textures/") + Candidate + TEXT(".png"));
				if (IFileManager::Get().FileExists(*Path))
				{
					return Path;
				}
			}
			return FString();
		}
	}

	float GetSwingStroke(float Progress)
	{
		return FMath::Sin(FMath::Sqrt(FMath::Clamp(Progress, 0.0f, 1.0f)) * PI);
	}

	FVector2D GetBob(float WalkPos, float WalkAmount)
	{
		return FVector2D(FMath::Sin(WalkPos * PI) * 0.5, -FMath::Abs(FMath::Cos(WalkPos * PI))) * (0.06 * WalkAmount);
	}

	FVector GetTipInArm()
	{
		return FVector(0.0, 0.0, -TipLength * Units);
	}

	FTransform GetArmTransform(float Stroke, float Equip, const FVector2D& Bob)
	{
		// A stroke pushes the fist forward and across and down, and turns the arm to point lower.
		FVector Tip = RestTip + FVector(0.18 * Stroke, -0.34 * Stroke, -0.08 * Stroke);
		Tip += FVector(0.0, Bob.X, Bob.Y);
		Tip.Z -= 0.9 * FMath::Clamp(Equip, 0.0f, 1.0f);
		const FVector Direction = (RestDirection + FVector(0.0, -0.15, -0.25) * Stroke).GetSafeNormal();
		const FVector Pivot = Tip - Direction * TipLength;

		// The arm's own axes: -z is the fingertips' way, -x its outer side (turned to the right and up).
		const FVector Out = (FVector(0.0, 0.6, 0.8) - Direction * FVector::DotProduct(FVector(0.0, 0.6, 0.8), Direction)).GetSafeNormal();
		const FVector AxisX = -Out;
		const FVector AxisZ = -Direction;
		const FVector AxisY = FVector::CrossProduct(AxisZ, AxisX);
		return FTransform(MakeBasis(AxisX, AxisY, AxisZ), Pivot * (Scale * Units), FVector(Scale));
	}

	FTransform GetHeldRestTransform(EHeld Kind)
	{
		// The item models' display.firstperson_righthand: item/generated and item/handheld give a flat item
		// rotation (0, -90, 25), translation (1.13, 3.2, 1.13) pixels, scale 0.68; block/block a block rotation
		// (0, 45, 0), no translation, scale 0.4. The model (x right, y up, z to the viewer, centred, 16 pixels a block)
		// is scaled, rotated about z then y (the other way round from a right-handed y: the camera looks along the model's -z) then x, moved by the translation, and put at the hand's anchor.
		const bool bCube = Kind == EHeld::Cube;
		const FVector Rotation = bCube ? FVector(0.0, 45.0, 0.0) : FVector(0.0, -90.0, 25.0);
		const FVector Translation = bCube ? FVector::ZeroVector : FVector(1.13, 3.2, 1.13);
		// How much smaller than the model's own scale the picture is drawn, found by matching the game's held block
		// (about 200 pixels across at 1280x720) and item (about 150).
		const double ItemScale = bCube ? 0.4 * 0.5 : 0.68 * 0.37;

		auto RotateZ = [](const FVector& V, double Degrees) { const double C = FMath::Cos(FMath::DegreesToRadians(Degrees)), S = FMath::Sin(FMath::DegreesToRadians(Degrees)); return FVector(V.X * C - V.Y * S, V.X * S + V.Y * C, V.Z); };
		auto RotateY = [](const FVector& V, double Degrees) { const double C = FMath::Cos(FMath::DegreesToRadians(Degrees)), S = FMath::Sin(FMath::DegreesToRadians(Degrees)); return FVector(V.X * C + V.Z * S, V.Y, -V.X * S + V.Z * C); };
		auto RotateX = [](const FVector& V, double Degrees) { const double C = FMath::Cos(FMath::DegreesToRadians(Degrees)), S = FMath::Sin(FMath::DegreesToRadians(Degrees)); return FVector(V.X, V.Y * C - V.Z * S, V.Y * S + V.Z * C); };
		// A model vector into the camera's space: model x is right, y up, z toward the camera.
		auto ToCamera = [](const FVector& Model) { return FVector(-Model.Z, Model.X, Model.Y); };
		// A mesh vector (x away from the viewer, y right, z up) into the model's space.
		auto MeshToModel = [](const FVector& Mesh) { return FVector(Mesh.Y, Mesh.Z, -Mesh.X); };
		auto Image = [&](const FVector& Mesh)
		{
			const FVector Model = MeshToModel(Mesh) * ItemScale;
			return ToCamera(RotateX(RotateY(RotateZ(Model, Rotation.Z), -Rotation.Y), Rotation.X));
		};
		const FVector Origin = (HeldAnchor + ToCamera(Translation / 16.0)) * Units;
		return FTransform(FMatrix(Image(FVector(1, 0, 0)), Image(FVector(0, 1, 0)), Image(FVector(0, 0, 1)), Origin));
	}

	FHeldPictures FindHeldPictures(FName Item)
	{
		FHeldPictures Result;
		if (Item.IsNone())
		{
			return Result;
		}
		const FString Name = McCache::PathOf(Item.ToString());
		const FString Own = FindPicture({ TEXT("item/") + Name });
		if (!Own.IsEmpty())
		{
			Result.Kind = EHeld::Flat;
			Result.Side = Own;
			return Result;
		}
		bool bCube = false;
		FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (Registry.IsLoaded() && FMcBlockShapes::Get().IsLoaded())
		{
			const TOptional<FMcStateId> State = Registry.FindDefaultState(Item);
			if (State.IsSet())
			{
				const TConstArrayView<FBox3f> Boxes = FMcBlockShapes::Get().GetCollision(State.GetValue());
				bCube = Boxes.Num() == 1 && Boxes[0].Min.GetMax() < 0.01f && Boxes[0].Max.GetMin() > 0.99f;
			}
		}
		const FString Side = FindPicture({ TEXT("block/") + Name + TEXT("_side"), TEXT("block/") + Name, TEXT("block/") + Name + TEXT("_front") });
		const FString Top = FindPicture({ TEXT("block/") + Name + TEXT("_top"), TEXT("block/") + Name });
		if (bCube && !Side.IsEmpty() && !Top.IsEmpty())
		{
			Result.Kind = EHeld::Cube;
			Result.Side = Side;
			Result.Top = Top;
			if (Name.Contains(TEXT("grass_block")))
			{
				Result.TopTint = FColor(0x91, 0xBD, 0x59);
			}
			if (Name.EndsWith(TEXT("_leaves")))
			{
				Result.SideTint = Result.TopTint = FColor(0x48, 0xB5, 0x18);
			}
			return Result;
		}
		const FString Flat = FindPicture({ TEXT("block/") + Name, TEXT("block/") + Name + TEXT("_side"), TEXT("block/") + Name + TEXT("_top"), TEXT("block/") + Name + TEXT("_front") });
		if (!Flat.IsEmpty())
		{
			Result.Kind = EHeld::Flat;
			Result.Side = Flat;
		}
		return Result;
	}

	void BuildArmMesh(McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		McPlayerModel::BuildPartMesh(EMcPlayerPart::RightArm, Out);
		// The model's arm is off to one side of its pivot by a pixel.
		const FVector Centre = McPlayerModel::ToUnreal(FVector(1.0, 0.0, 0.0));
		for (FVector& Position : Out.Positions)
		{
			Position += Centre;
		}
		// Seen from either side: the arm's faces are wound for the player model, which is a mirror of this.
		const int32 Count = Out.Triangles.Num();
		for (int32 Index = 0; Index + 2 < Count; Index += 3)
		{
			Out.Triangles.Append({ Out.Triangles[Index], Out.Triangles[Index + 2], Out.Triangles[Index + 1] });
		}
	}

	void BuildFlatMesh(float VMax, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double H = 0.5;
		const FVector2D UV[4] = { FVector2D(0, VMax), FVector2D(1, VMax), FVector2D(1, 0), FVector2D(0, 0) };
		// A few layers a hair apart, for the thickness the game gives an item.
		for (int32 Layer = -1; Layer <= 1; ++Layer)
		{
			const double X = Layer * 0.02;
			const FVector Corners[4] = { FVector(X, -H, -H), FVector(X, H, -H), FVector(X, H, H), FVector(X, -H, H) };
			AddQuad(Out, Corners, FVector::ForwardVector, UV, Layer == 0 ? FColor::White : FColor(215, 215, 215, 255));
		}
	}

	void BuildCubeSidesMesh(const FColor& Tint, float VMax, McPlayerModel::FMeshData& Out)
	{
		BuildCube(false, Tint, VMax, Out);
	}

	void BuildCubeTopMesh(const FColor& Tint, float VMax, McPlayerModel::FMeshData& Out)
	{
		BuildCube(true, Tint, VMax, Out);
	}
}

namespace
{
	void MakeSection(UProceduralMeshComponent& Mesh, int32 Index, const McPlayerModel::FMeshData& Data)
	{
		TArray<FVector2D> UV1, UV2;
		UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
		const TArray<FVector2D> NoUV;
		const TArray<FProcMeshTangent> NoTangents;
		Mesh.CreateMeshSection(Index, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, NoUV, Data.Colors, NoTangents, false);
	}
}

UMcHandComponent::UMcHandComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bAffectDistanceFieldLighting = false;
	bAffectDynamicIndirectLighting = false;
	bUseAsyncCooking = false;
	// Drawn with the camera's first person scale, so it is never cut by the near plane or pushed into a wall.
	SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cutout(TEXT("/Game/Mc/Materials/M_McBlockCutout.M_McBlockCutout"));
	BaseMaterial = Cutout.Object;

	HeldMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Held"));
	HeldMesh->SetupAttachment(this);
	HeldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldMesh->SetCastShadow(false);
	HeldMesh->bAffectDistanceFieldLighting = false;
	HeldMesh->bAffectDynamicIndirectLighting = false;
	HeldMesh->bUseAsyncCooking = false;
	HeldMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);

	ArmRest = McHand::GetArmTransform(0.0f, 0.0f, FVector2D::ZeroVector);
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("-McHandSwing="), FrozenSwing);
#endif
}

void UMcHandComponent::SetHeld(FName InItem)
{
	Wanted = InItem;
}

void UMcHandComponent::SetShown(bool bInShown)
{
	if (bShown != bInShown)
	{
		bShown = bInShown;
		SetVisibility(bShown, true);
	}
}

void UMcHandComponent::Swing()
{
	SwingAge = 0.0f;
}

void UMcHandComponent::ClearMaterials()
{
	Materials.Reset();
	Sizes.Reset();
	MaterialGeneration = McRenderMode::GetGeneration();
}

UMaterialInstanceDynamic* UMcHandComponent::GetMaterial(const FString& Path, FIntPoint& OutSize)
{
	if (!BaseMaterial)
	{
		return nullptr;
	}
	int32 Width = 0, Height = 0;
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(Path))
	{
		OutSize = Sizes.FindRef(Path);
		return *Found;
	}
	UTexture2DArray* Array = McRenderTextures::LoadPngAsArray(this, Path, Width, Height);
	if (!Array)
	{
		return nullptr;
	}
	OutSize = FIntPoint(Width, Height);
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
	Material->SetTextureParameterValue(TEXT("Blocks"), Array);
	McRenderMode::ApplyHdPicture(Material, Path);
	Materials.Add(Path, Material);
	Sizes.Add(Path, OutSize);
	return Material;
}

void UMcHandComponent::EnsureArm()
{
	if (bArmBuilt)
	{
		return;
	}
	FIntPoint Size;
	FString SkinPath = McCache::Asset(TEXT("textures/entity/player/wide/steve.png"));
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("McHandDebug")))
	{
		SkinPath = McCache::Asset(TEXT("textures/block/diamond_block.png"));
	}
#endif
	UMaterialInstanceDynamic* Skin = GetMaterial(SkinPath, Size);
	if (!Skin)
	{
		return;
	}
	McPlayerModel::FMeshData Data;
	McHand::BuildArmMesh(Data);
	ClearAllMeshSections();
	MakeSection(*this, 0, Data);
	SetMaterial(0, Skin);
	bArmBuilt = true;
	}

void UMcHandComponent::ShowHeld(FName Item)
{
	Shown = Item;
	HeldMesh->ClearAllMeshSections();
	ShownKind = McHand::EHeld::Nothing;
	const McHand::FHeldPictures Pictures = McHand::FindHeldPictures(Item);
	if (Pictures.Kind == McHand::EHeld::Nothing)
	{
		return;
	}
	FIntPoint SideSize, TopSize;
	UMaterialInstanceDynamic* Side = GetMaterial(Pictures.Side, SideSize);
	if (!Side)
	{
		return;
	}
	// An animated picture is a column of frames: the first one is the square on top.
	const float SideV = SideSize.Y > SideSize.X && SideSize.X > 0 ? static_cast<float>(SideSize.X) / SideSize.Y : 1.0f;
	McPlayerModel::FMeshData Data;
	if (Pictures.Kind == McHand::EHeld::Flat)
	{
		McHand::BuildFlatMesh(SideV, Data);
		MakeSection(*HeldMesh, 0, Data);
		HeldMesh->SetMaterial(0, Side);
	}
	else
	{
		UMaterialInstanceDynamic* Top = GetMaterial(Pictures.Top, TopSize);
		if (!Top)
		{
			return;
		}
		const float TopV = TopSize.Y > TopSize.X && TopSize.X > 0 ? static_cast<float>(TopSize.X) / TopSize.Y : 1.0f;
		McHand::BuildCubeSidesMesh(Pictures.SideTint, SideV, Data);
		MakeSection(*HeldMesh, 0, Data);
		HeldMesh->SetMaterial(0, Side);
		McHand::BuildCubeTopMesh(Pictures.TopTint, TopV, Data);
		MakeSection(*HeldMesh, 1, Data);
		HeldMesh->SetMaterial(1, Top);
	}
	ShownKind = Pictures.Kind;
	// Fixed in the arm's own space, so it goes where the arm goes.
	HeldMesh->SetRelativeTransform(McHand::GetHeldRestTransform(ShownKind) * ArmRest.Inverse());
}

void UMcHandComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	if (!Owner || !bShown || DeltaTime <= 0.0f)
	{
		return;
	}
	if (MaterialGeneration != McRenderMode::GetGeneration())
	{
		// The look changed: the pictures go onto the other mode's materials.
		ClearMaterials();
		bArmBuilt = false;
		ClearAllMeshSections();
		ShowHeld(Shown);
	}
	// The hand is drawn at the game's own 70 degrees (vertical) whatever the player's field of view setting.
	if (UCameraComponent* Camera = Cast<UCameraComponent>(GetAttachParent()))
	{
		if (GEngine && GEngine->GameViewport)
		{
			FVector2D Size;
			GEngine->GameViewport->GetViewportSize(Size);
			if (Size.X > 0.0 && Size.Y > 0.0)
			{
				Camera->bEnableFirstPersonFieldOfView = true;
				Camera->FirstPersonFieldOfView = static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(35.0)) * Size.X / Size.Y)));
			}
		}
	}
	EnsureArm();
	if (!bArmBuilt)
	{
		return;
	}

	// The buttons swing the arm (breaking repeats the swing while the button is held).
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (const APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
		{
			if (Controller->IsLocalController() && !Controller->bShowMouseCursor && SwingAge >= McHand::SwingSeconds)
			{
				if (Controller->IsInputKeyDown(EKeys::LeftMouseButton) || Controller->WasInputKeyJustPressed(EKeys::RightMouseButton))
				{
					Swing();
				}
			}
		}
	}
	SwingAge += DeltaTime;

	// How fast and how far the player walked this frame, along the ground.
	const FVector Location = Owner->GetActorLocation();
	float Target = 0.0f;
	if (bHasLocation)
	{
		const double Moved = FVector2D::Distance(FVector2D(Location.X, Location.Y), FVector2D(LastLocation.X, LastLocation.Y)) / McCoords::UnitsPerBlock;
		if (Moved < 8.0)
		{
			Target = FMath::Clamp(static_cast<float>(Moved / DeltaTime) / 4.3f, 0.0f, 1.0f);
			WalkPos += static_cast<float>(Moved) * 0.7f;
		}
	}
	LastLocation = Location;
	bHasLocation = true;
	WalkAmount = FMath::FInterpTo(WalkAmount, Target, DeltaTime, 8.0f);

	// A different item in the hand: lower it, change it, bring it up.
	if (Wanted != Shown)
	{
		Equip = FMath::Min(Equip + DeltaTime / McHand::EquipSeconds, 1.0f);
		if (Equip >= 1.0f)
		{
			ShowHeld(Wanted);
		}
	}
	else if (Equip > 0.0f)
	{
		Equip = FMath::Max(Equip - DeltaTime / McHand::EquipSeconds, 0.0f);
	}

	float Stroke = SwingAge < McHand::SwingSeconds ? McHand::GetSwingStroke(SwingAge / McHand::SwingSeconds) : 0.0f;
	if (FrozenSwing >= 0.0f)
	{
		Stroke = McHand::GetSwingStroke(FrozenSwing);
	}
	SetRelativeTransform(McHand::GetArmTransform(Stroke, Equip, McHand::GetBob(WalkPos, WalkAmount)));
	// Only an empty hand shows its arm.
	SetMeshSectionVisible(0, Shown.IsNone());
	// Lit by the sky like the blocks around.
	for (const TPair<FString, TObjectPtr<UMaterialInstanceDynamic>>& Pair : Materials)
	{
		Pair.Value->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		Pair.Value->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	}
}
