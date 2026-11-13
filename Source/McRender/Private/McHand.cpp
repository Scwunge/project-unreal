#include "McHand.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McCache.h"
#include "McItemFrames.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"
#include "McPerf.h"

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
		// Solved from the reference client's idle held stone (the corners of its top face) at the display scale 0.4.
		const FVector HeldAnchor(0.72, 0.567, -0.525);

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

	FTransform GetEatTransform(float Lift, float Time, float Seconds)
	{
		const float Eased = Lift * Lift * (3.0f - 2.0f * Lift);
		// Chewing: a quick bob five times a second, until the last fifth.
		const float Bob = Time < Seconds * 0.8f ? FMath::Abs(FMath::Cos(Time * 5.0f * PI)) * 0.06f : 0.0f;
		const FVector Move(6.0 * Eased, -54.0 * Eased, 4.0 * Eased + 50.0 * Bob * Eased);
		const FRotator Turn(6.0f * Eased, 45.0f * Eased, -8.0f * Eased);
		return FTransform(Turn, Move);
	}

	FQuat GetBowStandRotation()
	{
		// The picture's up-left diagonal (where the arrow points) goes forward, its other diagonal (the limbs) goes up,
		// and its face turns to the right: the bow seen side-on, as it is held to shoot. Mesh x, y, z in the camera's space.
		const double R = FMath::Sqrt(0.5);
		return MakeBasis(FVector(0, 1, 0), FVector(-R, 0, R), FVector(R, 0, R));
	}

	FVector GetBowTremble(float DrawSeconds)
	{
		if (DrawSeconds < 1.0f)
		{
			return FVector::ZeroVector;
		}
		const double Wave = FMath::Sin(2.0 * PI * static_cast<double>(DrawSeconds) * 20.0 / 7.78);
		return FVector(0.0, 0.12, -0.20) * Wave;
	}

	FTransform GetDrawTransform(float Lift, float Seconds)
	{
		const float Eased = Lift * Lift * (3.0f - 2.0f * Lift);
		// Fitted to the reference client's shots at 5 and 12 ticks of draw (overlaid, edges within about 10 pixels): the bow is
		// moved toward the middle of the screen and up from where it rests, in the camera's space (forward, right, up), and turned a
		// little about its own middle. It grows to 1.2 times the idle size (moving it nearer alone made the arrow shaft far too long).
		const FVector Move = FVector(-2.0, -20.0, 20.0) * Eased + GetBowTremble(Seconds);
		const FQuat Turn = FQuat::Slerp(FQuat::Identity, FRotator(0.0f, 22.0f, 7.0f).Quaternion(), Eased);
		return FTransform(Turn, Move, FVector(FMath::Lerp(1.0f, 1.2f, Eased)));
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

	FTransform GetHeldRestTransform(EHeld Kind, const FHeldDisplay& Display)
	{
		// The item models' display.firstperson_righthand: item/generated and item/handheld give a flat item
		// rotation (0, -90, 25), translation (1.13, 3.2, 1.13) pixels, scale 0.68 (drawn with the signs of the rotation turned over: matched to the reference client's held sword, stick, bow and snowball); block/block a block rotation
		// (0, 45, 0), no translation, scale 0.4. The model (x right, y up, z to the viewer, centred, 16 pixels a block)
		// is scaled, rotated about z, y then x (the angles turn the other way from a right-handed model's, all three), moved by the translation, and put at the hand's anchor.
		const bool bCube = Kind == EHeld::Cube;
		// The signs of the rotation's y and z turned over (matched to the reference client's held items).
		const FVector Rotation = bCube ? FVector(0.0, 45.0, 0.0) : FVector(Display.Rotation.X, -Display.Rotation.Y, -Display.Rotation.Z);
		const FVector Translation = bCube ? FVector::ZeroVector : Display.Translation;
		// The display transform's own scale, as the game draws it (a model of 16 pixels is a block).
		const double ItemScale = bCube ? 0.4 : Display.Scale;

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
			return ToCamera(RotateX(RotateY(RotateZ(Model, -Rotation.Z), -Rotation.Y), -Rotation.X));
		};
		const FVector Origin = (HeldAnchor + ToCamera(Translation / 16.0)) * Units;
		return FTransform(FMatrix(Image(FVector(1, 0, 0)), Image(FVector(0, 1, 0)), Image(FVector(0, 0, 1)), Origin));
	}

	FHeldDisplay ReadHeldDisplay(FName Item, const TCHAR* Key)
	{
		FHeldDisplay Result;
		if (FCString::Strcmp(Key, TEXT("firstperson_righthand")) != 0)
		{
			Result.Rotation = FVector::ZeroVector;
			Result.Translation = FVector::ZeroVector;
			Result.Scale = 1.0;
		}
		const FString ItemName = Item.ToString();
		FString Model = ItemName.StartsWith(TEXT("block/")) ? ItemName : TEXT("item/") + McCache::PathOf(ItemName);
		for (int32 Depth = 0; Depth < 6; ++Depth)
		{
			FString Text;
			TSharedPtr<FJsonObject> Json;
			if (!FFileHelper::LoadFileToString(Text, *McCache::Asset(TEXT("models/") + Model + TEXT(".json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
			{
				break;
			}
			const TSharedPtr<FJsonObject>* Display = nullptr;
			const TSharedPtr<FJsonObject>* Displays = nullptr;
			const TSharedPtr<FJsonObject>* Hand = nullptr;
			if (Json->TryGetObjectField(TEXT("display"), Displays) && (*Displays)->TryGetObjectField(Key, Hand))
			{
				auto Triple = [&Hand](const TCHAR* Name, FVector& Out)
				{
					const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
					if ((*Hand)->TryGetArrayField(Name, Values) && Values->Num() == 3)
					{
						Out = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
					}
				};
				FVector ScaleValues = FVector(Result.Scale);
				Triple(TEXT("rotation"), Result.Rotation);
				Triple(TEXT("translation"), Result.Translation);
				Triple(TEXT("scale"), ScaleValues);
				Result.Scale = ScaleValues.X;
				Result.bFound = true;
				break;
			}
			FString Parent;
			if (!Json->TryGetStringField(TEXT("parent"), Parent))
			{
				break;
			}
			Model = Parent.StartsWith(TEXT("minecraft:")) ? Parent.RightChop(10) : Parent;
		}
		return Result;
	}

	FHeldPictures FindHeldPictures(FName Item, bool bCast)
	{
		FHeldPictures Result;
		if (Item.IsNone())
		{
			return Result;
		}
		FString Name = McCache::PathOf(McItemFrames::Resolve(Item).ToString());
		if (bCast && Name == TEXT("fishing_rod"))
		{
			Name = TEXT("fishing_rod_cast");
		}
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

	void BuildExtrudedMesh(const TArray<FColor>& Pixels, int32 Width, float VMax, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Half = 0.5 / 16.0;
		const double Pixel = 1.0 / Width;
		const FVector2D UV[4] = { FVector2D(0, VMax), FVector2D(1, VMax), FVector2D(1, 0), FVector2D(0, 0) };
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const double X = Side * Half;
			const FVector Corners[4] = { FVector(X, -0.5, -0.5), FVector(X, 0.5, -0.5), FVector(X, 0.5, 0.5), FVector(X, -0.5, 0.5) };
			AddQuad(Out, Corners, FVector(Side, 0, 0), UV, FColor::White);
		}
		auto Solid = [&](int32 Column, int32 Row) { return Column >= 0 && Column < Width && Row >= 0 && Row < Width && Pixels.IsValidIndex(Row * Width + Column) && Pixels[Row * Width + Column].A > 0; };
		for (int32 Row = 0; Row < Width; ++Row)
		{
			for (int32 Column = 0; Column < Width; ++Column)
			{
				if (!Solid(Column, Row))
				{
					continue;
				}
				const double Left = Column * Pixel - 0.5, Right = Left + Pixel, Top = 0.5 - Row * Pixel, Bottom = Top - Pixel;
				const FVector2D Centre((Column + 0.5) * Pixel, (Row + 0.5) * Pixel * VMax);
				const FVector2D Same[4] = { Centre, Centre, Centre, Centre };
				// Each way out of this pixel: the edge's two ends, and which way it faces.
				struct FEdge { int32 DC, DR; FVector A, B; FVector Normal; float Shade; };
				const FEdge Edges[4] = {
					{ -1, 0, FVector(-Half, Left, Bottom), FVector(-Half, Left, Top), FVector(0, -1, 0), 0.8f },
					{ 1, 0, FVector(-Half, Right, Top), FVector(-Half, Right, Bottom), FVector(0, 1, 0), 0.8f },
					{ 0, -1, FVector(-Half, Left, Top), FVector(-Half, Right, Top), FVector(0, 0, 1), 1.0f },
					{ 0, 1, FVector(-Half, Right, Bottom), FVector(-Half, Left, Bottom), FVector(0, 0, -1), 0.6f } };
				for (const FEdge& Edge : Edges)
				{
					if (Solid(Column + Edge.DC, Row + Edge.DR))
					{
						continue;
					}
					const FVector Quad[4] = { Edge.A, Edge.B, FVector(Half, Edge.B.Y, Edge.B.Z), FVector(Half, Edge.A.Y, Edge.A.Z) };
					AddQuad(Out, Quad, Edge.Normal, Same, Multiply(FColor::White, Edge.Shade));
				}
			}
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

void UMcHandComponent::SetHeld(FName InItem, int32 InCount)
{
	if (InItem == Wanted && InCount > WantedCount && !InItem.IsNone())
	{
		bReequip = true;
	}
	Wanted = InItem;
	WantedCount = InCount;
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
	// -McRodCast shows a held rod cast, for screenshots.
	static const bool bForceCast = FParse::Param(FCommandLine::Get(), TEXT("McRodCast"));
	bRodCast |= bForceCast;
	Shown = Item;
	HeldMesh->ClearAllMeshSections();
	ShownKind = McHand::EHeld::Nothing;
	const McHand::FHeldPictures Pictures = McHand::FindHeldPictures(Item, bRodCast);
	bHasRodTip = false;
	ShownDisplay = McHand::ReadHeldDisplay(Item);
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
		TArray<FColor> Pixels;
		int32 PixelWidth = 0, PixelHeight = 0;
		if (McRenderTextures::LoadPngPixels(Pictures.Side, Pixels, PixelWidth, PixelHeight, 64) && PixelWidth > 0 && PixelHeight >= PixelWidth)
		{
			McHand::BuildExtrudedMesh(Pixels, PixelWidth, SideV, Data);
			if (McCache::PathOf(Item.ToString()) == TEXT("fishing_rod"))
			{
				// The line leaves from the end of the rod: its opaque pixel farthest toward the top right.
				int32 Best = INT32_MIN;
				for (int32 Row = 0; Row < PixelWidth; ++Row)
				{
					for (int32 Column = 0; Column < PixelWidth; ++Column)
					{
						if (Pixels[Row * PixelWidth + Column].A > 0 && Column - Row > Best)
						{
							Best = Column - Row;
							RodTipMesh = FVector(0.0, ((Column + 0.5) / PixelWidth - 0.5) * McCoords::UnitsPerBlock, (0.5 - (Row + 0.5) / PixelWidth) * McCoords::UnitsPerBlock);
							bHasRodTip = true;
						}
					}
				}
			}
		}
		else
		{
			McHand::BuildFlatMesh(SideV, Data);
		}
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
	HeldMesh->SetRelativeTransform(McHand::GetHeldRestTransform(ShownKind, ShownDisplay) * ArmRest.Inverse());
}

void UMcHandComponent::SetRodCast(bool bInCast)
{
	if (bRodCast != bInCast)
	{
		bRodCast = bInCast;
		if (Shown == Wanted && !Shown.IsNone())
		{
			ShowHeld(Shown);
		}
	}
}

bool UMcHandComponent::GetRodTipWorld(FVector& OutTip) const
{
	if (!bHasRodTip || !HeldMesh)
	{
		return false;
	}
	OutTip = HeldMesh->GetComponentTransform().TransformPosition(RodTipMesh);
	return true;
}

void UMcHandComponent::SetEating(bool bInEating, float InSeconds)
{
	bEating = bInEating;
	EatSeconds = FMath::Max(InSeconds, 0.1f);
}

void UMcHandComponent::SetDrawing(bool bInDrawing, float InSeconds)
{
	bDrawing = bInDrawing;
	DrawSeconds = FMath::Max(InSeconds, 0.0f);
}

void UMcHandComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	MC_PERF_SCOPE(Models);
	MC_PERF_SCOPE(ModelsHand);
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
			// The hand is drawn at the vertical 70 of the game's own view, whatever the FOV option says (the project keeps
			// the vertical angle, r.UseLegacyMaintainYFOVViewMatrix).
			Camera->bEnableFirstPersonFieldOfView = true;
			Camera->FirstPersonFieldOfView = 70.0f;
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
			if (Controller->IsLocalController() && !Controller->bShowMouseCursor && SwingAge >= McHand::SwingSeconds && !bEating)
			{
				if (Controller->IsInputKeyDown(EKeys::LeftMouseButton) || Controller->WasInputKeyJustPressed(EKeys::RightMouseButton))
				{
					Swing();
					bUseSwing = Controller->WasInputKeyJustPressed(EKeys::RightMouseButton);
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
	if (Wanted != Shown || bReequip)
	{
		Equip = FMath::Min(Equip + DeltaTime / McHand::EquipSeconds, 1.0f);
		if (Equip >= 1.0f)
		{
			bReequip = false;
			if (Wanted != Shown)
			{
				ShowHeld(Wanted);
			}
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
	EatLift = FMath::Clamp(EatLift + (bEating ? DeltaTime : -DeltaTime) / 0.25f, 0.0f, 1.0f);
	EatTime = bEating ? EatTime + DeltaTime : 0.0f;
	DrawLift = FMath::Clamp(DrawLift + (bDrawing ? DeltaTime : -DeltaTime) / 0.15f, 0.0f, 1.0f);
	FTransform Pose = McHand::GetArmTransform(Stroke, Equip, McHand::GetBob(WalkPos, WalkAmount));
	if (DrawLift > 0.0f)
	{
		const FTransform Draw = McHand::GetDrawTransform(DrawLift, DrawSeconds);
		const FTransform Rest = McHand::GetHeldRestTransform(ShownKind, ShownDisplay);
		// Only the held thing moves, in the camera's space; the arm stays where it was.
		const float Eased = DrawLift * DrawLift * (3.0f - 2.0f * DrawLift);
		const FQuat Stand = FQuat::Slerp(Rest.GetRotation(), McHand::GetBowStandRotation(), Eased);
		HeldMesh->SetRelativeTransform(FTransform(Draw.GetRotation() * Stand, Rest.GetTranslation() + Draw.GetTranslation(), Rest.GetScale3D() * Draw.GetScale3D()) * ArmRest.Inverse());
		bDrawApplied = true;
	}
	else if (bDrawApplied)
	{
		HeldMesh->SetRelativeTransform(McHand::GetHeldRestTransform(ShownKind, ShownDisplay) * ArmRest.Inverse());
		bDrawApplied = false;
	}
	if (EatLift > 0.0f)
	{
		// Moved in the camera's space, but turned about its own middle.
		const FTransform Eat = McHand::GetEatTransform(EatLift, EatTime, EatSeconds);
		Pose.SetRotation(Eat.GetRotation() * Pose.GetRotation());
		Pose.AddToTranslation(Eat.GetTranslation());
	}
	if (bUseSwing && !Shown.IsNone() && Stroke > 0.0f)
	{
		// Using the item (a throw) carries it toward the middle and down out of the view and back, as the swing runs.
		Pose.AddToTranslation(McHand::UseSwingMove * Stroke);
	}
	SetRelativeTransform(Pose);
	// Only an empty hand shows its arm.
	SetMeshSectionVisible(0, Shown.IsNone());
	// Lit by the sky like the blocks around.
	for (const TPair<FString, TObjectPtr<UMaterialInstanceDynamic>>& Pair : Materials)
	{
		Pair.Value->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		Pair.Value->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	}
}
