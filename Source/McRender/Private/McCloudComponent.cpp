#include "McCloudComponent.h"
#include "McCache.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McRender.h"
#include "McRenderMode.h"
#include "McRenderTextures.h"
#include "McSkyComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

double McClouds::GetDrift(double Ticks, int32 Width)
{
	const double Period = Width * CellSize;
	return Period > 0.0 ? FMath::Fmod(Ticks * DriftPerTick, Period) : 0.0;
}

FIntPoint McClouds::WrapCell(FIntPoint Cell, int32 Width, int32 Height)
{
	return FIntPoint(((Cell.X % Width) + Width) % Width, ((Cell.Y % Height) + Height) % Height);
}

UMcCloudComponent::UMcCloudComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bUseAsyncCooking = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Finder(TEXT("/Game/Mc/Materials/M_McClouds.M_McClouds"));
	CloudMaterial = Finder.Object;
}

void UMcCloudComponent::SetDimension(const FMcDimensionType& Type)
{
	Height = Type.CloudHeight;
	BaseColor = Type.CloudColor;
	BuiltCentre.Reset();
}

void UMcCloudComponent::BeginPlay()
{
	Super::BeginPlay();
	TArray<FColor> Pixels;
	if (!CloudMaterial || !McRenderTextures::LoadPngPixels(McCache::Asset(TEXT("textures/environment/clouds.png")), Pixels, PatternWidth, PatternHeight))
	{
		UE_LOG(LogMcRender, Error, TEXT("No clouds: clouds.png or M_McClouds is missing"));
		PatternWidth = PatternHeight = 0;
		return;
	}
	Pattern.SetNum(Pixels.Num());
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		Pattern[Index] = Pixels[Index].A >= 128;
	}
	// The clouds are plain: a white texel, coloured by the vertices.
	White = McRenderTextures::MakeArray(this, { FColor::White }, 1, 1);
	Material = UMaterialInstanceDynamic::Create(CloudMaterial, this);
	Material->SetTextureParameterValue(TEXT("Blocks"), White);
	// The same pattern for Unreal Mode's volumetric clouds, smoothed by the bilinear filter, wrapping like the picture.
	Mask = UTexture2D::CreateTransient(PatternWidth, PatternHeight, PF_B8G8R8A8);
	Mask->SRGB = false;
	Mask->Filter = TF_Bilinear;
	Mask->AddressX = Mask->AddressY = TA_Wrap;
	Mask->NeverStream = true;
	{
		FTexture2DMipMap& Mip = Mask->GetPlatformData()->Mips[0];
		uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Index = 0; Index < Pattern.Num(); ++Index)
		{
			const uint8 Value = Pattern[Index] ? 255 : 0;
			Data[Index * 4 + 0] = Data[Index * 4 + 1] = Data[Index * 4 + 2] = Data[Index * 4 + 3] = Value;
		}
		Mip.BulkData.Unlock();
	}
	Mask->UpdateResource();
}

bool UMcCloudComponent::IsCloud(FIntPoint Cell) const
{
	const FIntPoint Pixel = McClouds::WrapCell(Cell, PatternWidth, PatternHeight);
	return Pattern[Pixel.Y * PatternWidth + Pixel.X];
}

void UMcCloudComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Unreal Mode has its own volumetric clouds in the sky (UMcSkyComponent); the vanilla slabs are for the vanilla look.
	SetVisibility(!McRenderMode::IsEnhanced());
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (!Height || PatternWidth == 0 || !Material || !Controller || !Controller->PlayerCameraManager)
	{
		McSky::SetCloudLayer(nullptr, 0.0f, 0.0f);
		if (BuiltCentre)
		{
			ClearAllMeshSections();
			BuiltCentre.Reset();
		}
		return;
	}
	// The pattern's cell (X along x, Y along z) under the camera, with the drift: the pattern moves west, so the cell
	// under a fixed point counts up.
	const double Drift = McClouds::GetDrift(GetTime ? GetTime() : 0.0, PatternWidth);
	const FVector Eye = McCoords::UnrealToBlock(Controller->PlayerCameraManager->GetCameraLocation());
	const FIntPoint Centre(FMath::FloorToInt32((Eye.X + Drift) / McClouds::CellSize), FMath::FloorToInt32(Eye.Z / McClouds::CellSize));
	McSky::SetCloudLayer(Mask, static_cast<float>(Drift), *Height);

	// Rain greys them as it does the sky.
	const FVector2D Weather = GetWeather ? GetWeather() : FVector2D::ZeroVector;
	FLinearColor Colour = McSky::ApplyWeather(BaseColor, static_cast<float>(Weather.X), static_cast<float>(Weather.Y));
	Colour.A = BaseColor.A;
	// Quantised, so a slow change of weather rebuilds now and then rather than every frame.
	const FLinearColor Rounded(FMath::RoundToFloat(Colour.R * 32.0f) / 32.0f, FMath::RoundToFloat(Colour.G * 32.0f) / 32.0f, FMath::RoundToFloat(Colour.B * 32.0f) / 32.0f, Colour.A);
	if (!BuiltCentre || *BuiltCentre != Centre || !BuiltColour.Equals(Rounded))
	{
		Rebuild(Centre, Rounded);
	}
	// The mesh is built with the centre cell at the origin; this puts it where the drift has carried it.
	SetWorldLocation(McCoords::BlockToUnreal(FVector(Centre.X * McClouds::CellSize - Drift, *Height, Centre.Y * McClouds::CellSize)));
}

void UMcCloudComponent::Rebuild(FIntPoint Centre, const FLinearColor& Colour)
{
	BuiltCentre = Centre;
	BuiltColour = Colour;
	TArray<FVector> Positions;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FColor> Colours;
	const uint8 Alpha = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Colour.A * 255.0f), 0, 255));
	// Each face: its corners (as fractions of a cell, y in cloud thicknesses), its outward direction, its shade.
	auto AddFace = [&](const FVector& Origin, const FVector (&Corners)[4], const FVector& Out, float Shade)
	{
		const int32 First = Positions.Num();
		const FColor Face = FLinearColor(Colour.R * Shade, Colour.G * Shade, Colour.B * Shade, 1.0f).ToFColor(false);
		for (const FVector& Corner : Corners)
		{
			Positions.Add(McCoords::BlockToUnreal(Origin + FVector(Corner.X * McClouds::CellSize, Corner.Y * McClouds::Thickness, Corner.Z * McClouds::CellSize)));
			Normals.Add(McCoords::BlockToUnreal(Out).GetSafeNormal());
			UV0.Add(FVector2D(0.5, 0.5));
			Colours.Add(FColor(Face.R, Face.G, Face.B, Alpha));
		}
		Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
	};
	static const FVector Top[4] = { FVector(0, 1, 0), FVector(1, 1, 0), FVector(1, 1, 1), FVector(0, 1, 1) };
	static const FVector Bottom[4] = { FVector(0, 0, 0), FVector(0, 0, 1), FVector(1, 0, 1), FVector(1, 0, 0) };
	static const FVector West[4] = { FVector(0, 0, 0), FVector(0, 1, 0), FVector(0, 1, 1), FVector(0, 0, 1) };
	static const FVector East[4] = { FVector(1, 0, 0), FVector(1, 0, 1), FVector(1, 1, 1), FVector(1, 1, 0) };
	static const FVector North[4] = { FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0) };
	static const FVector South[4] = { FVector(0, 0, 1), FVector(0, 1, 1), FVector(1, 1, 1), FVector(1, 0, 1) };
	for (int32 DZ = -McClouds::Radius; DZ <= McClouds::Radius; ++DZ)
	{
		for (int32 DX = -McClouds::Radius; DX <= McClouds::Radius; ++DX)
		{
			const FIntPoint Cell = Centre + FIntPoint(DX, DZ);
			if (!IsCloud(Cell))
			{
				continue;
			}
			const FVector Origin(DX * McClouds::CellSize, 0.0, DZ * McClouds::CellSize);
			// The top in full light, the bottom in shadow, the sides between; sides shared with another cloud are inside.
			AddFace(Origin, Top, FVector(0, 1, 0), 1.0f);
			AddFace(Origin, Bottom, FVector(0, -1, 0), 0.7f);
			if (!IsCloud(Cell + FIntPoint(-1, 0)))
			{
				AddFace(Origin, West, FVector(-1, 0, 0), 0.9f);
			}
			if (!IsCloud(Cell + FIntPoint(1, 0)))
			{
				AddFace(Origin, East, FVector(1, 0, 0), 0.9f);
			}
			if (!IsCloud(Cell + FIntPoint(0, -1)))
			{
				AddFace(Origin, North, FVector(0, 0, -1), 0.8f);
			}
			if (!IsCloud(Cell + FIntPoint(0, 1)))
			{
				AddFace(Origin, South, FVector(0, 0, 1), 0.8f);
			}
		}
	}
	ClearAllMeshSections();
	if (Positions.IsEmpty())
	{
		return;
	}
	// UV1.x is the texture array's layer; UV2 is full sky light, so the clouds dim with the night as the sky does.
	TArray<FVector2D> UV1, UV2;
	UV1.Init(FVector2D::ZeroVector, Positions.Num());
	UV2.Init(FVector2D(1.0, 0.0), Positions.Num());
	CreateMeshSection(0, Positions, Triangles, Normals, UV0, UV1, UV2, TArray<FVector2D>(), Colours, TArray<FProcMeshTangent>(), false);
	SetMaterial(0, Material);
}
