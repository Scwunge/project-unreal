#include "McWeatherComponent.h"
#include "McBiomeRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McCache.h"
#include "McChunk.h"
#include "McRender.h"
#include "McSkyComponent.h"
#include "McRenderMode.h"
#include "McRenderTextures.h"
#include "McSound.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The pictures are 64 by 256 texels, a block across, so one picture is four blocks tall.
	constexpr float BlocksPerPicture = 4.0f;
	// How fast the pictures fall, in blocks a second: rain is a blur, snow drifts down.
	constexpr float RainSpeed = 9.0f;
	constexpr float SnowSpeed = 1.0f;
	// Seconds between looking up the columns' tops again.
	constexpr float TopsInterval = 0.5f;

	// A small hash of a column, for the offsets that keep neighbouring sheets from falling in step.
	float ColumnNoise(int32 X, int32 Z, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(X) * 73856093u ^ static_cast<uint32>(Z) * 19349663u ^ Salt * 83492791u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return static_cast<float>(H & 0xffffu) / 65535.0f;
	}

	struct FSheets
	{
		TArray<FVector> Positions;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FColor> Colours;
	};
}

FInt32Interval McWeather::GetColumnSpan(int32 CameraY, int32 Top)
{
	const int32 Ground = Top + 1;
	return FInt32Interval(FMath::Max(CameraY - Radius, Ground), FMath::Max(CameraY + Radius, Ground));
}

float McWeather::GetFade(float DistanceSquared)
{
	const float Edge = static_cast<float>(Radius * Radius);
	if (DistanceSquared > Edge)
	{
		return 0.0f;
	}
	return (1.0f - DistanceSquared / Edge) * 0.5f + 0.5f;
}

UMcWeatherComponent::UMcWeatherComponent(const FObjectInitializer& ObjectInitializer)
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

	// Made by tools/make_materials.py: translucent, both sides, the vertex alpha fading it.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Finder(TEXT("/Game/Mc/Materials/M_McWeather.M_McWeather"));
	WeatherMaterial = Finder.Object;
}

void UMcWeatherComponent::BeginPlay()
{
	Super::BeginPlay();
	SetWorldLocation(FVector::ZeroVector);
	if (!WeatherMaterial)
	{
		UE_LOG(LogMcRender, Error, TEXT("M_McWeather is missing; run tools/make_materials.py"));
		return;
	}
	int32 Width = 0, Height = 0;
	RainTexture = McRenderTextures::LoadPngAsArray(this, McCache::Asset(TEXT("textures/environment/rain.png")), Width, Height);
	SnowTexture = McRenderTextures::LoadPngAsArray(this, McCache::Asset(TEXT("textures/environment/snow.png")), Width, Height);
	for (UTexture2DArray* Texture : { RainTexture.Get(), SnowTexture.Get() })
	{
		if (Texture)
		{
			// The sheets scroll the picture, so it repeats rather than clamping.
			Texture->AddressX = TA_Wrap;
			Texture->AddressY = TA_Wrap;
			Texture->UpdateResource();
		}
	}
	RainMaterial = UMaterialInstanceDynamic::Create(WeatherMaterial, this);
	RainMaterial->SetTextureParameterValue(TEXT("Blocks"), RainTexture);
	SnowMaterial = UMaterialInstanceDynamic::Create(WeatherMaterial, this);
	SnowMaterial->SetTextureParameterValue(TEXT("Blocks"), SnowTexture);
	FMcBiomeRegistry::Get().EnsureLoaded();
}

int32 UMcWeatherComponent::FindTop(int32 X, int32 Z) const
{
	const FMcChunk* Chunk = BlockWorld->FindChunk(FIntPoint(X >> 4, Z >> 4));
	if (!Chunk)
	{
		return TNumericLimits<int32>::Max();
	}
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const int32 LocalX = X & 15;
	const int32 LocalZ = Z & 15;
	for (int32 Section = Chunk->NumSections() - 1; Section >= 0; --Section)
	{
		if (Chunk->GetSection(Section).IsEmpty())
		{
			continue;
		}
		const int32 Bottom = Chunk->SectionMinY(Section);
		for (int32 Y = Bottom + 15; Y >= Bottom; --Y)
		{
			const FMcStateId State = Chunk->GetBlock(LocalX, Y, LocalZ);
			if (Shapes.GetCollision(State).Num() > 0 || Shapes.GetFluid(State) != EMcFluid::None)
			{
				return Y;
			}
		}
	}
	return Chunk->GetMinY() - 1;
}

void UMcWeatherComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const float Rain = GetRain ? GetRain() : 0.0f;
	// In Unreal Mode the world is lit by the scene, so unlit streaks would glow at night: dim them with the sky's light
	// (the vanilla material already scales by it, but its picture is brighter than the dark scene around it).
	const uint8 Tone = McRenderMode::IsEnhanced() ? static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Sqrt(FMath::Clamp(McSky::GetSkyLightFactor(), 0.0f, 1.0f)) * 255.0f), 0, 255)) : 255;
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (Rain <= 0.0f || !BlockWorld || !RainMaterial || !Controller || !Controller->PlayerCameraManager)
	{
		if (bShown)
		{
			ClearAllMeshSections();
			bShown = false;
		}
		Age = 0.0f;
		return;
	}
	Age += DeltaTime;

	const FVector Eye = McCoords::UnrealToBlock(Controller->PlayerCameraManager->GetCameraLocation());
	const FIntVector Camera(FMath::FloorToInt32(Eye.X), FMath::FloorToInt32(Eye.Y), FMath::FloorToInt32(Eye.Z));
	TopsAge += DeltaTime;
	if (TopsAge >= TopsInterval)
	{
		TopsAge = 0.0f;
		Tops.Reset();
	}

	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	FSheets Sheets[2];  // rain, snow
	for (int32 DZ = -McWeather::Radius; DZ <= McWeather::Radius; ++DZ)
	{
		for (int32 DX = -McWeather::Radius; DX <= McWeather::Radius; ++DX)
		{
			const int32 X = Camera.X + DX;
			const int32 Z = Camera.Z + DZ;
			const FVector2D Centre(X + 0.5, Z + 0.5);
			const FVector2D Away = Centre - FVector2D(Eye.X, Eye.Z);
			const float Fade = McWeather::GetFade(static_cast<float>(Away.SizeSquared()));
			if (Fade <= 0.0f)
			{
				continue;
			}
			int32* Found = Tops.Find(FIntPoint(X, Z));
			const int32 Top = Found ? *Found : Tops.Add(FIntPoint(X, Z), FindTop(X, Z));
			if (Top == TNumericLimits<int32>::Max())
			{
				continue;
			}
			const FInt32Interval Span = McWeather::GetColumnSpan(Camera.Y, Top);
			if (Span.Min >= Span.Max)
			{
				continue;
			}
			const FMcChunk* Chunk = BlockWorld->FindChunk(FIntPoint(X >> 4, Z >> 4));
			const int32 BiomeY = FMath::Clamp(FMath::Max(Camera.Y, Top + 1), Chunk->GetMinY(), Chunk->GetMaxY());
			const FMcBiomeRegistry::EPrecipitation Kind = Biomes.GetPrecipitation(Chunk->GetBiome(X & 15, BiomeY, Z & 15), BiomeY);
			if (Kind == FMcBiomeRegistry::EPrecipitation::None)
			{
				continue;
			}
			const bool bSnow = Kind == FMcBiomeRegistry::EPrecipitation::Snow;
			FSheets& Out = Sheets[bSnow ? 1 : 0];

			// A sheet a block wide through the column's middle, square to the line from the camera.
			FVector2D Across(-Away.Y, Away.X);
			if (!Across.Normalize())
			{
				Across = FVector2D(1.0, 0.0);
			}
			Across *= 0.5;
			const float Offset = ColumnNoise(X, Z, 1);
			const float Speed = (bSnow ? SnowSpeed : RainSpeed) * (0.85f + 0.3f * ColumnNoise(X, Z, 2));
			const float Fall = (Age * Speed) / BlocksPerPicture + Offset;
			// Snow sways sideways a little.
			const float Sway = bSnow ? FMath::Sin(Age * (0.5f + ColumnNoise(X, Z, 3)) + Offset * 6.28f) * 0.15f : 0.0f;
			const float VTop = Span.Max / BlocksPerPicture + Fall;
			const float VBottom = Span.Min / BlocksPerPicture + Fall;
			// V runs down the picture, so the top corner has the smaller V; as Fall grows the picture slides down.
			const int32 First = Out.Positions.Num();
			const FVector2D Left = Centre - Across;
			const FVector2D Right = Centre + Across;
			Out.Positions.Add(McCoords::BlockToUnreal(FVector(Left.X, Span.Max, Left.Y)));
			Out.Positions.Add(McCoords::BlockToUnreal(FVector(Right.X, Span.Max, Right.Y)));
			Out.Positions.Add(McCoords::BlockToUnreal(FVector(Right.X, Span.Min, Right.Y)));
			Out.Positions.Add(McCoords::BlockToUnreal(FVector(Left.X, Span.Min, Left.Y)));
			Out.UV0.Add(FVector2D(Sway, -VTop));
			Out.UV0.Add(FVector2D(1.0f + Sway, -VTop));
			Out.UV0.Add(FVector2D(1.0f + Sway, -VBottom));
			Out.UV0.Add(FVector2D(Sway, -VBottom));
			const FVector Normal = McCoords::BlockToUnreal(FVector(Away.X, 0.0, Away.Y)).GetSafeNormal();
			const FColor Colour(Tone, Tone, Tone, static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Fade * Rain * 255.0f), 0, 255)));
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Out.Normals.Add(-Normal);
				Out.Colours.Add(Colour);
			}
			Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
	}

	ClearAllMeshSections();
	for (int32 Section = 0; Section < 2; ++Section)
	{
		const FSheets& Out = Sheets[Section];
		if (Out.Positions.IsEmpty())
		{
			continue;
		}
		// UV1.x is the texture array's layer (0); UV2 is full sky light and no block light, so the night dims it.
		TArray<FVector2D> UV1, UV2;
		UV1.Init(FVector2D::ZeroVector, Out.Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Out.Positions.Num());
		CreateMeshSection(Section, Out.Positions, Out.Triangles, Out.Normals, Out.UV0, UV1, UV2, TArray<FVector2D>(), Out.Colours, TArray<FProcMeshTangent>(), false);
		SetMaterial(Section, Section == 0 ? RainMaterial.Get() : SnowMaterial.Get());
	}
	bShown = true;

	// The rain's sound, a drop now and then, more often the harder it rains.
	SoundRemainder += DeltaTime;
	for (; SoundRemainder >= 0.05f; SoundRemainder -= 0.05f)
	{
		PlayRainSound(Camera, Rain);
	}
}

void UMcWeatherComponent::PlayRainSound(const FIntVector& Camera, float Rain)
{
	// About one tick in three at full rain.
	if (Random.FRand() >= Rain * Rain * 0.35f)
	{
		return;
	}
	const int32 X = Camera.X + Random.RandRange(-McWeather::Radius, McWeather::Radius);
	const int32 Z = Camera.Z + Random.RandRange(-McWeather::Radius, McWeather::Radius);
	const int32* Top = Tops.Find(FIntPoint(X, Z));
	const FMcChunk* Chunk = BlockWorld->FindChunk(FIntPoint(X >> 4, Z >> 4));
	if (!Top || !Chunk || *Top < Chunk->GetMinY() || *Top > Chunk->GetMaxY())
	{
		return;
	}
	const int32 Above = FMath::Min(*Top + 1, Chunk->GetMaxY());
	if (FMcBiomeRegistry::Get().GetPrecipitation(Chunk->GetBiome(X & 15, Above, Z & 15), Above) != FMcBiomeRegistry::EPrecipitation::Rain)
	{
		return;
	}
	// On a roof over the player's head it is quieter and lower.
	const bool bOverhead = *Top > Camera.Y + 1;
	FMcSoundEvent Event;
	Event.Name = bOverhead ? FName("weather.rain.above") : FName("weather.rain");
	Event.Position = FVector(X + Random.FRand(), *Top + 1.0, Z + Random.FRand());
	Event.Volume = bOverhead ? 0.1f : 0.2f;
	Event.Pitch = bOverhead ? 0.5f : 1.0f;
	FMcSoundBus::Get().OnSound.Broadcast(Event);
}
