#include "McSkyComponent.h"
#include "McCache.h"
#include "McChunk.h"
#include "McDayCycle.h"
#include "McDimensionType.h"
#include "McRender.h"
#include "McRenderMode.h"
#include "McRenderTextures.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	float GSkyLightFactor = 1.0f;

	// The sky is a sphere around the camera, far enough that the fog's cutoff leaves it alone and near enough that it
	// is inside the far clip. The sun, moon and stars sit just inside it.
	constexpr float DomeRadius = 50000.0f;
	constexpr float CelestialRadius = 45000.0f;
	constexpr int32 DomeRings = 16;
	constexpr int32 DomeSegments = 32;
	constexpr int32 NumStars = 1500;
	// Half the width of the sun's and the moon's squares, as a fraction of their distance.
	constexpr float SunHalfSize = 0.2f;
	constexpr float MoonHalfSize = 0.12f;

	// The Nether's fog is the nether wastes' fog colour (data/minecraft/worldgen/biome/nether_wastes.json, 3344392), which
	// is also all its sky; the End's is its fog's, near the end sky's own average. Display colours.
	const FLinearColor NetherFogColor(0x33 / 255.0f, 0x08 / 255.0f, 0x08 / 255.0f, 1.0f);
	const FLinearColor EndFogColor(0x18 / 255.0f, 0x13 / 255.0f, 0x18 / 255.0f, 1.0f);
	// The Nether's fog starts 10 blocks out and is thick at 96; the End's is the view distance's.
	// Unreal Mode's exposure and grade leave a plain display colour dimmer than the vanilla look draws it, so the Nether's and
	// the End's fog and the End's sky picture are drawn this much brighter there.
	constexpr float EnhancedFogBoost = 2.4f;
	constexpr float NetherFogStartBlocks = 10.0f;
	constexpr float NetherFogEndBlocks = 96.0f;
	// The enhanced look's sun and moon, in lux. Lit surfaces show their colour times the light over pi, before the
	// tonemapper; the moon is a tenth of a lux or so, enough to see by.
	constexpr float SunLux = 7.0f;
	// Dusk and dawn's blue fill, in lux.
	constexpr float TwilightLux = 2.5f;
	constexpr float MoonLux = 0.6f;
		// The Nether's and the End's fill lights, in lux: just enough that shapes read where no block light reaches.
		constexpr float NetherFillLux = 4.0f;
		constexpr float EndFillLux = 1.2f;
	// How much of the fog's thickness the volumetric fog takes: a haze with light shafts, not a wall.
	constexpr float VolumetricFogShare = 0.04f;
	// Light shafts: the sun scattering in a thicker, forward-leaning volumetric fog where trees and roofs shadow it.
	// -McShafts= scales them (0 is the old thin haze).
	float GetShafts()
	{
		static const float Shafts = [] { float V = 1.8f; 
#if !UE_BUILD_SHIPPING
			FParse::Value(FCommandLine::Get(), TEXT("-McShafts="), V);
#endif
			return V; }();
		return Shafts;
	}

	// The End has no day, so its blocks get a fixed share of their sky light, as at night; the Nether has none.
	constexpr float EndSkyLightFactor = 0.24f;
	// The game draws the end sky picture dark, at 0x282828 (40/255) of its colour on screen. Tint scales linear colour,
	// hence the 2.2.
	const float EndSkyBrightness = FMath::Pow(40.0f / 255.0f, 2.2f);
	// And repeats it 16 times across each face of the box.
	constexpr float EndSkyRepeats = 16.0f;

	// Minecraft axes (x east, y up, z south) to Unreal's.
	FVector ToUnreal(const FVector& Dir)
	{
		return FVector(Dir.X, Dir.Z, Dir.Y);
	}

	FColor ToDisplayColor(const FLinearColor& Display)
	{
		return FColor(
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Display.R * 255.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Display.G * 255.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Display.B * 255.0f), 0, 255)), 255);
	}

	// A display (sRGB) colour as the linear one the fog wants; the blocks raise their colours the same way.
	FLinearColor DisplayToLinear(const FLinearColor& Display)
	{
		return FLinearColor(FMath::Pow(Display.R, 2.2f), FMath::Pow(Display.G, 2.2f), FMath::Pow(Display.B, 2.2f), 1.0f);
	}

	// A square facing the origin, centred on (0, 0, Distance) in Unreal's axes, as a section of the mesh.
	void AddSquare(UProceduralMeshComponent* Mesh, float Distance, float HalfSize, const FColor& Colour)
	{
		const float Half = Distance * HalfSize;
		const TArray<FVector> Positions = {
			FVector(-Half, -Half, Distance), FVector(Half, -Half, Distance), FVector(Half, Half, Distance), FVector(-Half, Half, Distance) };
		const TArray<int32> Triangles = { 0, 1, 2, 0, 2, 3 };
		const TArray<FVector> Normals = { FVector(0, 0, -1), FVector(0, 0, -1), FVector(0, 0, -1), FVector(0, 0, -1) };
		const TArray<FVector2D> UV0 = { FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) };
		TArray<FVector2D> Zero;
		Zero.Init(FVector2D::ZeroVector, 4);
		TArray<FColor> Colours;
		Colours.Init(Colour, 4);
		const TArray<FProcMeshTangent> NoTangents;
		Mesh->CreateMeshSection(0, Positions, Triangles, Normals, UV0, Zero, Zero, Zero, Colours, NoTangents, false);
	}
}

static float GCameraOpen = 1.0f;

float McSky::GetCameraOpen()
{
	return GCameraOpen;
}

void McSky::SetCameraOpen(float Open)
{
	GCameraOpen = Open;
}

static float GCameraUnderwater = 0.0f;

float McSky::GetCameraUnderwater()
{
	return GCameraUnderwater;
}

void McSky::SetCameraUnderwater(float Under)
{
	GCameraUnderwater = Under;
}

static TWeakObjectPtr<UTexture2D> GCloudMask;
static float GCloudDrift = 0.0f;
static float GCloudHeight = 192.0f;

void McSky::SetCloudLayer(UTexture2D* Mask, float DriftBlocks, float HeightBlocks)
{
	GCloudMask = Mask;
	GCloudDrift = DriftBlocks;
	GCloudHeight = HeightBlocks;
}

float McSky::GetSkyLightFactor()
{
	return GSkyLightFactor;
}

void McSky::SetSkyLightFactor(float Factor)
{
	GSkyLightFactor = Factor;
}

FLinearColor McSky::ApplyWeather(const FLinearColor& Colour, float Rain, float Thunder)
{
	const float Brightness = Colour.R * 0.3f + Colour.G * 0.59f + Colour.B * 0.11f;
	FLinearColor Out = Colour;
	if (Rain > 0.0f)
	{
		const float Amount = FMath::Clamp(Rain, 0.0f, 1.0f) * 0.75f;
		const float Grey = Brightness * 0.6f;
		Out = FLinearColor(FMath::Lerp(Out.R, Grey, Amount), FMath::Lerp(Out.G, Grey, Amount), FMath::Lerp(Out.B, Grey, Amount), Colour.A);
	}
	if (Thunder > 0.0f)
	{
		const float Amount = FMath::Clamp(Thunder, 0.0f, 1.0f) * 0.75f;
		const float Grey = Brightness * 0.2f;
		Out = FLinearColor(FMath::Lerp(Out.R, Grey, Amount), FMath::Lerp(Out.G, Grey, Amount), FMath::Lerp(Out.B, Grey, Amount), Colour.A);
	}
	return Out;
}

float McSky::ApplyWeatherToLight(float Factor, float Rain, float Thunder)
{
	constexpr float Night = 0.24f;
	const float Above = FMath::Max(0.0f, Factor - Night);
	return Night + Above * (1.0f - FMath::Clamp(Rain, 0.0f, 1.0f) * 5.0f / 16.0f) * (1.0f - FMath::Clamp(Thunder, 0.0f, 1.0f) * 5.0f / 16.0f)
		+ FMath::Min(Factor, Night) - Night;
}

void UMcSkyComponent::Flash()
{
	FlashTime = 0.2f;
#if !UE_BUILD_SHIPPING
	// Development: -McFlashHold keeps the flash lit, for a screenshot.
	if (FParse::Param(FCommandLine::Get(), TEXT("McFlashHold")))
	{
		FlashTime = 1.0e6f;
	}
#endif
}

UMcSkyComponent::UMcSkyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the camera has moved, so the sky never trails the view by a frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);

	// Made by tools/make_materials.py.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DomeFinder(TEXT("/Game/Mc/Materials/M_McSkyDome.M_McSkyDome"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> AdditiveFinder(TEXT("/Game/Mc/Materials/M_McSkyAdditive.M_McSkyAdditive"));
	// The engine's own cloud material (Unreal Mode's volumetric clouds); a hard reference so it is cooked with the game.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CloudFinder(TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
	CloudMaterial = CloudFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> McCloudFinder(TEXT("/Game/Mc/Materials/M_McVolumeClouds.M_McVolumeClouds"));
	McCloudMaterial = McCloudFinder.Object;
	DomeMaterial = DomeFinder.Object;
	AdditiveMaterial = AdditiveFinder.Object;
}

void UMcSkyComponent::SetDimension(const FMcDimensionType& Type)
{
	Mode = Type.Skybox == TEXT("end") ? EMode::End : Type.Skybox == TEXT("overworld") ? EMode::Overworld : EMode::Nether;
}

UProceduralMeshComponent* UMcSkyComponent::MakeMesh(const TCHAR* Name)
{
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(GetOwner(), Name);
	Mesh->SetupAttachment(this);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->bUseAsyncCooking = false;
	Mesh->RegisterComponent();
	return Mesh;
}

void UMcSkyComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!FMcDayCycle::Get().EnsureLoaded())
	{
		UE_LOG(LogMcRender, Error, TEXT("The day cycle failed to load; the sky stays at noon"));
	}
	if (!DomeMaterial || !AdditiveMaterial)
	{
		UE_LOG(LogMcRender, Error, TEXT("A sky material is missing; run tools/make_materials.py"));
		return;
	}

	BuildDome();
	BuildStars();

	// The sun and moon: one square each, turned about the east-west axis with the time.
	Sun = MakeMesh(TEXT("Sun"));
	Sun->SetUsingAbsoluteRotation(true);
	AddSquare(Sun, CelestialRadius, SunHalfSize, FColor::White);
	Moon = MakeMesh(TEXT("Moon"));
	Moon->SetUsingAbsoluteRotation(true);
	AddSquare(Moon, CelestialRadius, MoonHalfSize, FColor::White);

	int32 Width = 0, Height = 0;
	SunTexture = McRenderTextures::LoadPngAsArray(this, McCache::Asset(TEXT("textures/environment/celestial/sun.png")), Width, Height);
	for (int32 Phase = 0; Phase < FMcDayCycle::MoonPhases; ++Phase)
	{
		const FString Path = McCache::Asset(FString::Printf(TEXT("textures/environment/celestial/moon/%s.png"), *FMcDayCycle::GetMoonPhaseName(Phase).ToString()));
		MoonTextures.Add(McRenderTextures::LoadPngAsArray(this, Path, Width, Height));
	}
	SunMaterial = UMaterialInstanceDynamic::Create(AdditiveMaterial, this);
	SunMaterial->SetTextureParameterValue(TEXT("Blocks"), SunTexture);
	Sun->SetMaterial(0, SunMaterial);
	MoonMaterial = UMaterialInstanceDynamic::Create(AdditiveMaterial, this);
	Moon->SetMaterial(0, MoonMaterial);

	Fog = NewObject<UExponentialHeightFogComponent>(GetOwner(), TEXT("Fog"));
	Fog->SetupAttachment(this);
	// The fog stays at the camera's height, so it is the same all the way up the world.
	Fog->SetFogHeightFalloff(0.001f);
	Fog->SetFogMaxOpacity(1.0f);
	Fog->SetVolumetricFog(false);
	Fog->DirectionalInscatteringLuminance = FLinearColor::Black;
	Fog->RegisterComponent();

	BuildEnhanced();
	bReady = true;
}

void UMcSkyComponent::BuildDome()
{
	Dome = MakeMesh(TEXT("Dome"));
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<int32> Triangles;
	for (int32 Ring = 0; Ring <= DomeRings; ++Ring)
	{
		const float Elevation = FMath::DegreesToRadians(-90.0f + 180.0f * Ring / DomeRings);
		for (int32 Segment = 0; Segment <= DomeSegments; ++Segment)
		{
			const float Azimuth = FMath::DegreesToRadians(360.0f * Segment / DomeSegments);
			const FVector Dir(FMath::Cos(Elevation) * FMath::Cos(Azimuth), FMath::Sin(Elevation), FMath::Cos(Elevation) * FMath::Sin(Azimuth));
			DomeDirections.Add(Dir);
			Positions.Add(ToUnreal(Dir) * DomeRadius);
			Normals.Add(-ToUnreal(Dir));
			UV0.Add(FVector2D::ZeroVector);
		}
	}
	const int32 Stride = DomeSegments + 1;
	for (int32 Ring = 0; Ring < DomeRings; ++Ring)
	{
		for (int32 Segment = 0; Segment < DomeSegments; ++Segment)
		{
			const int32 A = Ring * Stride + Segment;
			const int32 B = A + 1;
			const int32 C = A + Stride + 1;
			const int32 D = A + Stride;
			Triangles.Append({ A, B, C, A, C, D });
		}
	}
	TArray<FColor> Colours;
	Colours.Init(FColor::Black, Positions.Num());
	TArray<FVector2D> Zero;
	Zero.Init(FVector2D::ZeroVector, Positions.Num());
	const TArray<FProcMeshTangent> NoTangents;
	Dome->CreateMeshSection(0, Positions, Triangles, Normals, UV0, Zero, Zero, Zero, Colours, NoTangents, false);
	Dome->SetMaterial(0, DomeMaterial);

	// The dome's lower half, up to the horizon, for the physical sky: past the loaded terrain the atmosphere's own ground
	// shows as a dark band between the last grass and the horizon, so the fog colour is drawn there as it is in vanilla.
	Skirt = MakeMesh(TEXT("Skirt"));
	const int32 SkirtVertices = (DomeRings / 2 + 1) * Stride;
	TArray<int32> SkirtTriangles;
	for (int32 Ring = 0; Ring < DomeRings / 2; ++Ring)
	{
		for (int32 Segment = 0; Segment < DomeSegments; ++Segment)
		{
			const int32 A = Ring * Stride + Segment;
			SkirtTriangles.Append({ A, A + 1, A + Stride + 1, A, A + Stride + 1, A + Stride });
		}
	}
	Skirt->CreateMeshSection(0, TArray<FVector>(Positions.GetData(), SkirtVertices), SkirtTriangles, TArray<FVector>(Normals.GetData(), SkirtVertices),
		TArray<FVector2D>(UV0.GetData(), SkirtVertices), TArray<FVector2D>(Zero.GetData(), SkirtVertices), TArray<FVector2D>(Zero.GetData(), SkirtVertices),
		TArray<FVector2D>(Zero.GetData(), SkirtVertices), TArray<FColor>(Colours.GetData(), SkirtVertices), NoTangents, false);
	Skirt->SetMaterial(0, DomeMaterial);
	Skirt->SetVisibility(false);
}

void UMcSkyComponent::BuildStars()
{
	Stars = MakeMesh(TEXT("Stars"));
	Stars->SetUsingAbsoluteRotation(true);
	FRandomStream Random(7013);
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<int32> Triangles;
	TArray<FColor> Colours;
	for (int32 Star = 0; Star < NumStars; ++Star)
	{
		const FVector Dir = Random.VRand();
		const FVector Centre = ToUnreal(Dir) * (CelestialRadius * 0.98f);
		// Squares facing the origin, a few pixels wide.
		const FVector Forward = -Centre.GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Forward, Right);
		const float Half = CelestialRadius * (0.0015f + Random.FRand() * 0.0015f);
		const float Weight = 0.4f + 0.6f * Random.FRand();
		StarWeights.Add(Weight);
		const int32 First = Positions.Num();
		Positions.Add(Centre - Right * Half - Up * Half);
		Positions.Add(Centre + Right * Half - Up * Half);
		Positions.Add(Centre + Right * Half + Up * Half);
		Positions.Add(Centre - Right * Half + Up * Half);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Normals.Add(Forward);
			const uint8 Level = static_cast<uint8>(Weight * 255.0f);
			Colours.Add(FColor(Level, Level, Level, 255));
		}
		UV0.Append({ FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) });
		Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
	}
	NumStarVertices = Positions.Num();
	TArray<FVector2D> Zero;
	Zero.Init(FVector2D::ZeroVector, Positions.Num());
	const TArray<FProcMeshTangent> NoTangents;
	Stars->CreateMeshSection(0, Positions, Triangles, Normals, UV0, Zero, Zero, Zero, Colours, NoTangents, false);
	// Stars are plain squares: Solid swaps the texture for white.
	UMaterialInstanceDynamic* StarMaterial = UMaterialInstanceDynamic::Create(AdditiveMaterial, this);
	StarMaterial->SetScalarParameterValue(TEXT("Solid"), 1.0f);
	Stars->SetMaterial(0, StarMaterial);
}

void UMcSkyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bReady)
	{
		return;
	}

	// Follow the camera, so the sky is always the same distance away.
	FVector Camera = GetComponentLocation();
	if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		if (Controller->PlayerCameraManager)
		{
			Camera = Controller->PlayerCameraManager->GetCameraLocation();
		}
	}
	SetWorldLocation(Camera);
	if (Fog)
	{
		Fog->SetWorldLocation(Camera);
	}

	const double Ticks = GetTime ? GetTime() : 1000.0;
	FMcDayCycle& Cycle = FMcDayCycle::Get();
	McSky::SetSkyLightFactor(Cycle.GetSkyLightFactor(Ticks));
	SkyColor = Cycle.GetSkyColor(Ticks);
	FogColor = Cycle.GetFogColor(Ticks);
	StarBrightness = Cycle.GetStarBrightness(Ticks);

	// Rain greys the sky and the fog, dims the daylight and hides the sun, moon and stars.
	const FVector2D WeatherNow = GetWeather ? GetWeather() : FVector2D::ZeroVector;
	Rain = static_cast<float>(WeatherNow.X);
	Thunder = static_cast<float>(WeatherNow.Y);
	if (Rain > 0.0f)
	{
		SkyColor = McSky::ApplyWeather(SkyColor, Rain, Thunder);
		FogColor = McSky::ApplyWeather(FogColor, Rain, Thunder);
		StarBrightness *= 1.0f - Rain;
		McSky::SetSkyLightFactor(McSky::ApplyWeatherToLight(McSky::GetSkyLightFactor(), Rain, Thunder));
	}
	const float CelestialAlpha = 1.0f - Rain;
	SunMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(CelestialAlpha, CelestialAlpha, CelestialAlpha, 1.0f));
	MoonMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(CelestialAlpha, CelestialAlpha, CelestialAlpha, 1.0f));

	// A lightning flash: the sky light at full, a little over, and the sky lit a pale blue.
	if (FlashTime > 0.0f)
	{
		FlashTime -= DeltaTime;
		McSky::SetSkyLightFactor(1.1f);
		const FLinearColor Lit(0.8f, 0.8f, 1.0f, 1.0f);
		SkyColor = FMath::Lerp(SkyColor, Lit, 0.45f);
		FogColor = FMath::Lerp(FogColor, Lit, 0.45f);
	}

	if (Mode != EMode::Overworld)
	{
		// No day and night: one colour for the sky and the fog, nothing in the sky but (in the End) its own picture.
		SkyColor = FogColor = Mode == EMode::Nether ? NetherFogColor : EndFogColor;
		StarBrightness = 0.0f;
		McSky::SetSkyLightFactor(Mode == EMode::Nether ? 0.0f : EndSkyLightFactor);
		if (Mode == EMode::End && !EndSky)
		{
			BuildEndSky();
		}
	}
	const bool bOverworld = Mode == EMode::Overworld;
	// With Unreal Mode on, the Overworld's sky is the atmosphere's: no dome, no sun square.
	const bool bPhysicalSky = bOverworld && McRenderMode::IsEnhanced() && Atmosphere;
	Sun->SetVisibility(bOverworld && CelestialAlpha > 0.001f && !bPhysicalSky);
	Moon->SetVisibility(bOverworld && CelestialAlpha > 0.001f);
	Stars->SetVisibility(bOverworld && StarBrightness > 0.001f);
	if (EndSky)
	{
		EndSky->SetVisibility(Mode == EMode::End);
	}
	// The end sky is the whole sky, drawn on black rather than added to the dome's colour.
	Dome->SetVisibility(Mode != EMode::End && !bPhysicalSky);
	Skirt->SetVisibility(bPhysicalSky);

	UpdateDome(Ticks);
	if (bOverworld)
	{
		UpdateStars(Ticks);
		UpdateCelestial(Ticks);
	}
	UpdateFog();
	if (LookGeneration != McRenderMode::GetGeneration())
	{
		ApplyLook();
	}
	if (McRenderMode::IsEnhanced())
	{
		UpdateSunLight(Ticks);
	}
}

void UMcSkyComponent::BuildEnhanced()
{
	// One light for the sun by day and the moon by night, casting the world's shadows.
	SunLight = NewObject<UDirectionalLightComponent>(GetOwner(), TEXT("SunLight"));
	SunLight->SetupAttachment(this);
	SunLight->SetUsingAbsoluteRotation(true);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetCastShadows(true);
	SunLight->SetDynamicShadowDistanceMovableLight(FogEndBlocks * McCoords::UnitsPerBlock);
	SunLight->SetDynamicShadowCascades(4);
	SunLight->SetVolumetricScatteringIntensity(1.0f);
	// Soft-edged shadows, fine contact shadows in the gaps between blocks, and shafts of light round what stands in front
	// of a low sun.
	SunLight->SetLightSourceAngle(0.9f);
	SunLight->ContactShadowLength = 0.04f;
	SunLight->SetEnableLightShaftBloom(true);
	SunLight->SetBloomScale(0.12f);
	SunLight->SetBloomThreshold(4.0f);
	// The sun is the atmosphere's: the sky's colours, the sun's own reddening at a low angle and the sun disc come from it.
	SunLight->SetAtmosphereSunLight(true);
	SunLight->SetAtmosphereSunLightIndex(0);
	SunLight->SetVisibility(false);
	SunLight->RegisterComponent();

	TwilightFill = NewObject<UDirectionalLightComponent>(GetOwner(), TEXT("TwilightFill"));
	TwilightFill->SetupAttachment(this);
	TwilightFill->SetUsingAbsoluteRotation(true);
	TwilightFill->SetMobility(EComponentMobility::Movable);
	TwilightFill->SetCastShadows(false);
	TwilightFill->SetLightColor(FLinearColor(0.45f, 0.55f, 1.0f));
	TwilightFill->SetIntensity(0.0f);
	TwilightFill->SetVisibility(false);
	TwilightFill->RegisterComponent();

	// Physical sky: Rayleigh and Mie scattering round a planet whose top is the world's y = 0. Hidden outside the Overworld
	// and with Unreal Mode off, where the dome stands in.
	Atmosphere = NewObject<USkyAtmosphereComponent>(GetOwner(), TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(this);
	Atmosphere->SetUsingAbsoluteLocation(true);
	Atmosphere->SetVisibility(false);
	Atmosphere->RegisterComponent();
	// The sky captured live, for what reflects it (water, glass, wet stone). Its diffuse light is off: Lumen can't tell
	// a cave's ceiling from open sky here, and the chunks' own sky light does the ambient.
	SkyLight = NewObject<USkyLightComponent>(GetOwner(), TEXT("SkyLight"));
	SkyLight->SetupAttachment(this);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetRealTimeCaptureEnabled(true);
	SkyLight->SetIndirectLightingIntensity(0.0f);
	SkyLight->SetVisibility(false);
	SkyLight->RegisterComponent();
	// Clouds above the world, lit by the sun and the sky and shadowing the ground. A few hundred metres up and a kilometre
	// thick, much taller than the game's flat 4-block layer: a thin slab would look like cardboard at this scale.
	if (CloudMaterial)
	{
		Clouds = NewObject<UVolumetricCloudComponent>(GetOwner(), TEXT("Clouds"));
		Clouds->SetupAttachment(this);
		Clouds->SetUsingAbsoluteLocation(true);
		Clouds->SetMaterial(CloudMaterial);
		Clouds->SetLayerBottomAltitude(0.6f);
		Clouds->SetLayerHeight(1.4f);
		Clouds->SetTracingStartMaxDistance(60.0f);
		Clouds->SetVisibility(false);
		Clouds->RegisterComponent();
	}

	// Over the controller's fixed-exposure volume (which stays for the vanilla look) while enhanced.
	EnhancedPost = NewObject<UPostProcessComponent>(GetOwner(), TEXT("EnhancedPost"));
	EnhancedPost->SetupAttachment(this);
	EnhancedPost->bUnbound = true;
	EnhancedPost->Priority = 10.0f;
	EnhancedPost->bEnabled = false;
	FPostProcessSettings& Settings = EnhancedPost->Settings;
	Settings.bOverride_DynamicGlobalIlluminationMethod = true;
	Settings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	Settings.bOverride_ReflectionMethod = true;
	Settings.ReflectionMethod = EReflectionMethod::Lumen;
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = 0.0f;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.25f;
	Settings.bOverride_MotionBlurMax = true;
	Settings.MotionBlurMax = 3.0f;
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = 0.35f;
	Settings.bOverride_AmbientOcclusionIntensity = true;
	Settings.AmbientOcclusionIntensity = 0.5f;
	Settings.bOverride_AmbientOcclusionRadius = true;
	Settings.AmbientOcclusionRadius = 120.0f;
	// Unreal Mode's grade: a little richer and punchier than Unreal's default, glowing things bloom, a soft vignette
	// and a touch of lens fringe and flare; Lumen at higher quality, reflecting in water and glass too.
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = 0.6f;
	Settings.bOverride_BloomThreshold = true;
	Settings.BloomThreshold = 1.0f;
	Settings.bOverride_ColorSaturation = true;
	Settings.ColorSaturation = FVector4(1.08, 1.08, 1.08, 1.0);
	Settings.bOverride_ColorContrast = true;
	Settings.ColorContrast = FVector4(1.05, 1.05, 1.05, 1.0);
	Settings.bOverride_ColorGain = true;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = 0.35f;
	Settings.bOverride_SceneFringeIntensity = true;
	Settings.SceneFringeIntensity = 0.2f;
	Settings.bOverride_LensFlareIntensity = true;
	Settings.LensFlareIntensity = 0.15f;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.0f;
	Settings.bOverride_LumenSceneLightingQuality = true;
	Settings.LumenSceneLightingQuality = 1.5f;
	Settings.bOverride_LumenFinalGatherQuality = true;
	Settings.LumenFinalGatherQuality = 1.5f;
	Settings.bOverride_LumenReflectionQuality = true;
	Settings.LumenReflectionQuality = 1.5f;
	Settings.bOverride_LumenFrontLayerTranslucencyReflections = true;
	Settings.LumenFrontLayerTranslucencyReflections = true;
#if !UE_BUILD_SHIPPING
	// For tuning: -McEnhancedExposure= (stops), -McSunLux=.
	FParse::Value(FCommandLine::Get(), TEXT("-McEnhancedExposure="), Settings.AutoExposureBias);
	BaseExposure = Settings.AutoExposureBias;
#endif
	EnhancedPost->RegisterComponent();
}

void UMcSkyComponent::ApplyLook()
{
	LookGeneration = McRenderMode::GetGeneration();
	const bool bEnhanced = McRenderMode::IsEnhanced();
	if (SunLight)
	{
		SunLight->SetVisibility(bEnhanced);
	}
	if (EnhancedPost)
	{
		EnhancedPost->bEnabled = bEnhanced;
	}
	if (Fog)
	{
		Fog->SetVolumetricFog(bEnhanced);
		Fog->SetVolumetricFogExtinctionScale(VolumetricFogShare + 0.03f * GetShafts());
		Fog->SetVolumetricFogScatteringDistribution(FMath::Lerp(0.2f, 0.8f, FMath::Clamp(GetShafts(), 0.0f, 1.0f)));
		Fog->SetVolumetricFogDistance(FogEndBlocks * McCoords::UnitsPerBlock * 0.5f);
		if (!bEnhanced)
		{
			Fog->SetDirectionalInscatteringColor(FLinearColor::Black);
		}
	}
	// Blocks carry Minecraft's final colours in the vanilla look; the filmic tonemapper would brighten and desaturate
	// them. Lit by Unreal, they need it.
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->EngineShowFlags.SetTonemapper(bEnhanced);
	}
}

void UMcSkyComponent::UpdateSunLight(double Ticks)
{
	if (!SunLight)
	{
		return;
	}
	// The grade follows the dimension: warm and heavy in the Nether, cool and violet in the End.
	{
		const bool bNether = Mode == EMode::Nether;
		const bool bEnd = Mode == EMode::End;
		FPostProcessSettings& Grade = EnhancedPost->Settings;
		Grade.ColorGain = bNether ? FVector4(1.14, 0.94, 0.84, 1.0) : bEnd ? FVector4(0.94, 0.9, 1.14, 1.0) : FVector4(1.0, 1.0, 1.0, 1.0);
		Grade.BloomIntensity = Mode == EMode::Overworld ? 0.6f : 0.95f;
		// Eyes adjust: under a roof in the Overworld the exposure rises over a second or two (up to 1.5 stops), and falls
		// back outdoors.
		const float Target = Mode == EMode::Overworld ? (1.0f - McSky::GetCameraOpen()) * 1.5f : 0.0f;
		CaveExposure = FMath::FInterpTo(CaveExposure, Target, GetWorld()->GetDeltaSeconds(), 1.0f);
		// Under water the picture goes blue-green and a little darker, the red light being lost first.
		const float Sea = McRenderMode::IsEnhanced() ? McSky::GetCameraUnderwater() : 0.0f;
		Grade.ColorGain = FMath::Lerp(Grade.ColorGain, FVector4(0.3, 0.72, 0.95, 1.0), Sea);
		Grade.AutoExposureBias = BaseExposure + CaveExposure - 1.8f * Sea;
		Grade.VignetteIntensity = Mode == EMode::Overworld ? 0.35f : 0.5f;
		SunLight->SetCastShadows(Mode == EMode::Overworld);
		SunLight->SetCastRaytracedShadow(McRenderMode::IsRayTracing());
		// An unshadowed fill scattering in the volumetric fog draws the edges of its grid.
		SunLight->SetVolumetricScatteringIntensity(Mode == EMode::Overworld ? 1.0f + 2.0f * GetShafts() : 0.0f);
	}
	if (Atmosphere)
	{
		const bool bSky = Mode == EMode::Overworld;
		Atmosphere->SetVisibility(bSky);
		if (SkyLight)
		{
			SkyLight->SetVisibility(bSky);
		}
		// Rain is haze: a thicker, less forward-scattering Mie layer turns the sky grey and the sun dim.
		Atmosphere->SetMieScatteringScale(FMath::Lerp(0.004f, 0.06f, Rain));
		Atmosphere->SetMieAnisotropy(FMath::Lerp(0.8f, 0.5f, Rain));
		if (Clouds)
		{
			Clouds->SetVisibility(bSky);
			// The vanilla layout once the cloud component has the pattern: the layer sits at the vanilla cloud height, a
			// few tens of blocks thick (a 4 block slab would look like cardboard at this scale), the pattern carried west.
			// Parked: the vanilla-layout cloud material renders the sky black on the 1070 (PLAN.md, volumetric clouds), so it
			// is only used with -McVolumeClouds until that is found; the engine's own clouds are the default.
			static const bool bVolume = FParse::Param(FCommandLine::Get(), TEXT("McVolumeClouds"));
			if (UTexture2D* Mask = GCloudMask.Get(); Mask && McCloudMaterial && bSky && bVolume)
			{
				if (!McCloudInstance)
				{
					McCloudInstance = UMaterialInstanceDynamic::Create(McCloudMaterial, this);
				}
				const float BottomZ = GCloudHeight * McCoords::UnitsPerBlock;
				const float ThickZ = 28.0f * McCoords::UnitsPerBlock;
				const float GroundZ = Atmosphere->GetComponentLocation().Z;
				McCloudInstance->SetTextureParameterValue(TEXT("CloudMask"), Mask);
				McCloudInstance->SetScalarParameterValue(TEXT("DriftX"), GCloudDrift);
				static float DensityScale = []()
				{
					float Value = 1.0f;
					FParse::Value(FCommandLine::Get(), TEXT("-McCloudDensity="), Value);  // development: times the material's density
					return Value;
				}();
				McCloudInstance->SetScalarParameterValue(TEXT("Density"), 6.0f * DensityScale);
				McCloudInstance->SetScalarParameterValue(TEXT("Storm"), FMath::Clamp(Rain + Thunder, 0.0f, 1.0f));
				McCloudInstance->SetScalarParameterValue(TEXT("BottomZ"), BottomZ);
				McCloudInstance->SetScalarParameterValue(TEXT("ThickZ"), ThickZ);
				// The component's layer is in km above the planet's ground (at the sky's own height); a little room round
				// the band so the layer's edges never cut it. Set only when it changes: each set rebuilds the cloud's
				// render state.
				const float LayerBottom = FMath::Max((BottomZ - GroundZ) / 100000.0f - 0.01f, 0.01f);
				if (Clouds->GetMaterial() != McCloudInstance)
				{
					Clouds->SetMaterial(McCloudInstance);
				}
				if (!FMath::IsNearlyEqual(Clouds->LayerBottomAltitude, LayerBottom))
				{
					Clouds->SetLayerBottomAltitude(LayerBottom);
					Clouds->SetLayerHeight((ThickZ + 2.0f * 1000.0f) / 100000.0f);
				}
			}
		}
		SunLight->SetAtmosphereSunLight(bSky);
#if !UE_BUILD_SHIPPING
		// Development: -McHide=fog,clouds,stars,moon,moonsky (what to leave out, to find what draws a line).
		FString Hide;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McHide="), Hide))
		{
			if (Hide.Contains(TEXT("fog"))) { Fog->SetVisibility(false); }
			if (Hide.Contains(TEXT("clouds")) && Clouds) { Clouds->SetVisibility(false); }
			if (Hide.Contains(TEXT("stars"))) { Stars->SetVisibility(false); }
			if (Hide.Contains(TEXT("moon"))) { Moon->SetVisibility(false); }
			if (Hide.Contains(TEXT("post")) && EnhancedPost) { EnhancedPost->bEnabled = false; }
		}
#endif
	}
	if (Mode != EMode::Overworld)
	{
		// No sun in the Nether or the End. Their mood is a dim fill light from above with no shadows (so it reaches
		// everywhere, as the Nether's glow does), the fog lit from inside by the lava's colour, and the blocks' own light.
		const bool bNether = Mode == EMode::Nether;
		SunLight->SetWorldRotation(FRotator(-65.0f, 40.0f, 0.0f));
		// The fills' colours are the dimension types' own: the Nether's ambient_light_color (#302821, scaled up to full
		// brightness and warmed towards the lava) and the End's sky_light_color (#ac60cd).
		float Fill = bNether ? NetherFillLux : EndFillLux;
		float Glow = 0.0f;
#if !UE_BUILD_SHIPPING
		// For tuning: -McNetherFill=, -McEndFill= (lux) and -McFogGlow= (times the fog's emissive).
		FParse::Value(FCommandLine::Get(), bNether ? TEXT("-McNetherFill=") : TEXT("-McEndFill="), Fill);
		FParse::Value(FCommandLine::Get(), TEXT("-McFogGlow="), Glow);
#endif
		const FLinearColor NetherAmbient = FLinearColor(FColor(0x30, 0x28, 0x21)) * 5.0f;
		SunLight->SetLightColor(bNether ? FLinearColor::LerpUsingHSV(NetherAmbient.GetClamped(), FLinearColor(1.0f, 0.3f, 0.1f), 0.5f) : FMath::Lerp(FLinearColor::White, FLinearColor(FColor(0xac, 0x60, 0xcd)), 0.5f));
		SunLight->SetIntensity(Fill);
		if (EndSky)
		{
			if (UMaterialInstanceDynamic* EndMaterial = Cast<UMaterialInstanceDynamic>(EndSky->GetMaterial(0)))
			{
				const float Shine = EndSkyBrightness * (McRenderMode::IsEnhanced() ? EnhancedFogBoost : 1.0f);
				EndMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Shine, Shine, Shine, 1.0f));
			}
		}
		if (Fog)
		{
			// The Nether's and the End's fog is their own colour at a distance; the volumetric fog (made for the sun's
			// shafts) only darkens it to black there.
			if (Fog->bEnableVolumetricFog)
			{
				Fog->SetVolumetricFog(false);
			}
			Fog->SetDirectionalInscatteringColor(FLinearColor::Black);
			Fog->SetVolumetricFogEmissive(bNether ? FLinearColor(0.12f, 0.025f, 0.004f) * Glow : FLinearColor(0.02f, 0.01f, 0.05f) * Glow);
			Fog->SetVolumetricFogAlbedo(bNether ? FColor(255, 110, 60) : FColor(200, 160, 255));
		}
		return;
	}
	if (Fog)
	{
		if (Fog->bEnableVolumetricFog != McRenderMode::IsEnhanced())
		{
			Fog->SetVolumetricFog(McRenderMode::IsEnhanced());
		}
		Fog->SetVolumetricFogEmissive(FLinearColor::Black);
		Fog->SetVolumetricFogAlbedo(FColor::White);
	}
	// Under water the sun's light dances as caustics (a light function, only while it matters).
	{
		static UMaterialInterface* Caustics = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mc/Materials/M_McCaustics.M_McCaustics"));
		const bool bCaustic = Caustics && McRenderMode::IsEnhanced() && McSky::GetCameraUnderwater() > 0.5f;
		SunLight->SetLightFunctionMaterial(bCaustic ? Caustics : nullptr);
	}
	float SunIntensity = SunLux;
	const float MoonIntensity = MoonLux;
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("-McSunLux="), SunIntensity);
#endif
	FMcDayCycle& Cycle = FMcDayCycle::Get();
	const FVector ToSun = ToUnreal(FMcDayCycle::AngleToDirection(Cycle.GetSunAngle(Ticks)));
	const FVector ToMoon = ToUnreal(FMcDayCycle::AngleToDirection(Cycle.GetMoonAngle(Ticks)));
	// The sun fades out just after it sets and the moon takes over; rain hides most of both.
	const float Day = FMath::SmoothStep(McRenderMode::IsEnhanced() ? -0.2f : -0.04f, 0.12f, static_cast<float>(ToSun.Z));
	const float Clear = 1.0f - 0.5f * Rain;
	const bool bSun = Day > 0.0f;
	const FVector Towards = bSun ? ToSun : ToMoon;
	SunLight->SetWorldRotation((-Towards).Rotation());
	if (bSun)
	{
		// Low, the sun is orange; high, nearly white.
		const float High = FMath::Clamp(static_cast<float>(ToSun.Z) * 3.0f, 0.0f, 1.0f);
		// The atmosphere's own transmittance reddens the low sun; the light itself is white.
			SunLight->SetLightColor(FLinearColor::White);
		SunLight->SetIntensity(SunIntensity * Day * Clear * (1.0f - 0.35f * (McRenderMode::IsEnhanced() ? McSky::GetCameraUnderwater() : 0.0f)));
		// Looking towards the sun the haze glows its colour, most of all as it sets.
		if (Fog)
		{
			Fog->SetDirectionalInscatteringExponent(6.0f);
			Fog->SetDirectionalInscatteringStartDistance(1000.0f);
			Fog->SetDirectionalInscatteringColor(SunLight->GetLightColor() * (0.25f + 0.5f * (1.0f - High)) * Day * Clear);
		}
	}
	else
	{
		SunLight->SetLightColor(FLinearColor(0.6f, 0.7f, 1.0f));
		SunLight->SetIntensity(MoonIntensity * FMath::SmoothStep(0.0f, 0.15f, static_cast<float>(ToMoon.Z)) * Clear);
		if (Fog)
		{
			Fog->SetDirectionalInscatteringColor(FLinearColor::Black);
		}
	}
	// Dusk and dawn in Unreal Mode: the sun just under the horizon, the sky still glowing; a blue fill lights the ground.
	if (TwilightFill)
	{
		const float Z = static_cast<float>(ToSun.Z);
		const float Twilight = McRenderMode::IsEnhanced() ? FMath::SmoothStep(-0.3f, -0.05f, Z) * (1.0f - FMath::SmoothStep(0.0f, 0.25f, Z)) : 0.0f;
		TwilightFill->SetVisibility(Twilight > 0.001f);
		TwilightFill->SetIntensity(TwilightLux * Twilight * Clear);
		const FVector From = FVector(-ToSun.X, -ToSun.Y, 0.7).GetSafeNormal();
		TwilightFill->SetWorldRotation((-From).Rotation());
	}
	// A lightning flash lights the scene: a pale blue light from high up, with shadows, over the sun or the moon.
	if (Mode == EMode::Overworld && FlashTime > 0.0f)
	{
		SunLight->SetWorldRotation(FRotator(-75.0f, 30.0f, 0.0f));
		SunLight->SetLightColor(FLinearColor(0.75f, 0.85f, 1.0f));
		SunLight->SetIntensity(SunIntensity * 1.0f * FMath::Clamp(FlashTime / 0.2f, 0.0f, 1.0f));
	}
}

void UMcSkyComponent::BuildEndSky()
{
	// The picture on each of the six faces of a box round the camera, as in the game.
	EndSky = MakeMesh(TEXT("EndSky"));
	int32 Width = 0, Height = 0;
	EndSkyTexture = McRenderTextures::LoadPngAsArray(this, McCache::Asset(TEXT("textures/environment/end_sky.png")), Width, Height);
	if (EndSkyTexture)
	{
		// Repeated across each face, so it wraps rather than clamping like the other pictures.
		EndSkyTexture->AddressX = TA_Wrap;
		EndSkyTexture->AddressY = TA_Wrap;
		EndSkyTexture->UpdateResource();
	}
	// Inside the dome even at its corners (the dome would hide what is outside it).
	const float Half = DomeRadius * 0.55f;
	// Per face: the corners in Unreal's axes, seen from inside.
	const FVector Corners[6][4] = {
		{ FVector(-Half, -Half, -Half), FVector(Half, -Half, -Half), FVector(Half, Half, -Half), FVector(-Half, Half, -Half) },
		{ FVector(-Half, Half, Half), FVector(Half, Half, Half), FVector(Half, -Half, Half), FVector(-Half, -Half, Half) },
		{ FVector(-Half, -Half, -Half), FVector(-Half, Half, -Half), FVector(-Half, Half, Half), FVector(-Half, -Half, Half) },
		{ FVector(Half, Half, -Half), FVector(Half, -Half, -Half), FVector(Half, -Half, Half), FVector(Half, Half, Half) },
		{ FVector(Half, -Half, -Half), FVector(-Half, -Half, -Half), FVector(-Half, -Half, Half), FVector(Half, -Half, Half) },
		{ FVector(-Half, Half, -Half), FVector(Half, Half, -Half), FVector(Half, Half, Half), FVector(-Half, Half, Half) },
	};
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<int32> Triangles;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		const int32 First = Positions.Num();
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Positions.Add(Corners[Face][Corner]);
			Normals.Add(-Corners[Face][Corner].GetSafeNormal());
		}
		UV0.Append({ FVector2D(0, 0), FVector2D(EndSkyRepeats, 0), FVector2D(EndSkyRepeats, EndSkyRepeats), FVector2D(0, EndSkyRepeats) });
		Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
	}
	TArray<FVector2D> Zero;
	Zero.Init(FVector2D::ZeroVector, Positions.Num());
	TArray<FColor> Colours;
	Colours.Init(FColor::White, Positions.Num());
	const TArray<FProcMeshTangent> NoTangents;
	EndSky->CreateMeshSection(0, Positions, Triangles, Normals, UV0, Zero, Zero, Zero, Colours, NoTangents, false);
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(AdditiveMaterial, this);
	Material->SetTextureParameterValue(TEXT("Blocks"), EndSkyTexture);
	Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(EndSkyBrightness, EndSkyBrightness, EndSkyBrightness, 1.0f));
	EndSky->SetMaterial(0, Material);
}

void UMcSkyComponent::UpdateDome(double Ticks)
{
	const FMcDayCycle& Cycle = FMcDayCycle::Get();
	const FLinearColor Glow = Cycle.GetSunriseColor(Ticks);
	const FVector SunDir = FMcDayCycle::AngleToDirection(Cycle.GetSunAngle(Ticks));
	FVector2D SunAzimuth(SunDir.X, SunDir.Z);
	const bool bHasGlow = Glow.A > 0.001f && SunAzimuth.Normalize();

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FColor> Colours;
	TArray<FColor> SkirtColours;
	Positions.Reserve(DomeDirections.Num());
	Normals.Reserve(DomeDirections.Num());
	UV0.Reserve(DomeDirections.Num());
	Colours.Reserve(DomeDirections.Num());
	for (const FVector& Dir : DomeDirections)
	{
		Positions.Add(ToUnreal(Dir) * DomeRadius);
		Normals.Add(-ToUnreal(Dir));
		UV0.Add(FVector2D::ZeroVector);

		// The fog colour at the horizon and below it, the sky colour overhead, blending over the first third.
		const float Up = FMath::Clamp(Dir.Y / 0.35f, 0.0f, 1.0f);
		const float Blend = Up * Up * (3.0f - 2.0f * Up);
		FLinearColor Colour = FMath::Lerp(FogColor, SkyColor, Blend);

		// The dawn and dusk glow: a fan at the horizon towards the sun, fading with the angle from it and the height.
		if (bHasGlow)
		{
			FVector2D Horizontal(Dir.X, Dir.Z);
			if (Horizontal.Normalize())
			{
				const float Around = FMath::Max(0.0f, static_cast<float>(FVector2D::DotProduct(Horizontal, SunAzimuth)));
				const float Height = FMath::Clamp(1.0f - FMath::Abs(Dir.Y) / 0.45f, 0.0f, 1.0f);
				const float Strength = Glow.A * Height * Around * Around;
				Colour = FMath::Lerp(Colour, FLinearColor(Glow.R, Glow.G, Glow.B, 1.0f), Strength);
			}
		}
		Colours.Add(ToDisplayColor(Colour));
		// The skirt under the physical sky takes the dusk glow all the way round, as the atmosphere's horizon does.
		FLinearColor Under = Colour;
		if (bHasGlow)
		{
			Under = FMath::Lerp(Colour, FLinearColor(Glow.R, Glow.G, Glow.B, 1.0f), Glow.A * 0.7f * FMath::Clamp(1.0f - FMath::Abs(Dir.Y) / 0.45f, 0.0f, 1.0f));
		}
		SkirtColours.Add(ToDisplayColor(Under));
	}
	TArray<FVector2D> Zero;
	Zero.Init(FVector2D::ZeroVector, Positions.Num());
	const TArray<FProcMeshTangent> NoTangents;
	Dome->UpdateMeshSection(0, Positions, Normals, UV0, Zero, Zero, Zero, Colours, NoTangents);
	if (Skirt)
	{
		const int32 SkirtVertices = (DomeRings / 2 + 1) * (DomeSegments + 1);
		Skirt->UpdateMeshSection(0, TArray<FVector>(Positions.GetData(), SkirtVertices), TArray<FVector>(Normals.GetData(), SkirtVertices),
			TArray<FVector2D>(UV0.GetData(), SkirtVertices), TArray<FVector2D>(Zero.GetData(), SkirtVertices), TArray<FVector2D>(Zero.GetData(), SkirtVertices),
			TArray<FVector2D>(Zero.GetData(), SkirtVertices), TArray<FColor>(SkirtColours.GetData(), SkirtVertices), NoTangents);
	}
}

void UMcSkyComponent::UpdateStars(double Ticks)
{
	// Turning the whole mesh is cheaper than moving its 6000 corners.
	Stars->SetWorldRotation(FRotator(FMcDayCycle::Get().GetStarAngle(Ticks), 0.0f, 0.0f));
	// The stars are white squares that fade in with the dark: the brightness is the amount of white added.
	const float Amount = FMath::Clamp(StarBrightness * 2.0f, 0.0f, 1.0f);
	Stars->SetVisibility(StarBrightness > 0.001f);
	if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Stars->GetMaterial(0)))
	{
		Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Amount, Amount, Amount, 1.0f));
	}
}

void UMcSkyComponent::UpdateCelestial(double Ticks)
{
	const FMcDayCycle& Cycle = FMcDayCycle::Get();
	Sun->SetWorldRotation(FRotator(Cycle.GetSunAngle(Ticks), 0.0f, 0.0f));
	Moon->SetWorldRotation(FRotator(Cycle.GetMoonAngle(Ticks), 0.0f, 0.0f));

	const int32 Phase = FMcDayCycle::GetMoonPhase(static_cast<int64>(FMath::FloorToDouble(Ticks)));
	if (Phase != ShownMoonPhase && MoonTextures.IsValidIndex(Phase))
	{
		ShownMoonPhase = Phase;
		MoonMaterial->SetTextureParameterValue(TEXT("Blocks"), MoonTextures[Phase]);
	}
}

void UMcSkyComponent::UpdateFog()
{
	// Thick at the view distance, starting at three fifths of it. 3.3 optical depths take the fog to about 90%.
	const bool bNether = Mode == EMode::Nether;
	// Rain brings the fog 30% closer.
	// Under water (Unreal Mode) the view closes in to about 16 blocks, in the sea's own colour, darker the less sky there is.
	const float Under = McRenderMode::IsEnhanced() ? McSky::GetCameraUnderwater() : 0.0f;
	const float AboveEnd = (bNether ? FMath::Min(FogEndBlocks, NetherFogEndBlocks) : FogEndBlocks * (1.0f - 0.3f * Rain)) * McCoords::UnitsPerBlock;
	const float End = FMath::Lerp(AboveEnd, 16.0f * McCoords::UnitsPerBlock, Under);
	const float Start = FMath::Lerp(bNether ? NetherFogStartBlocks * McCoords::UnitsPerBlock : AboveEnd * 0.6f, 0.0f, Under);
	Fog->SetStartDistance(Start);
	Fog->SetFogCutoffDistance(DomeRadius * 0.8f);
	// Under the physical sky the atmosphere hazes the distance itself, so the distance fog is half as thick.
	const float Thin = (Mode == EMode::Overworld && McRenderMode::IsEnhanced() && Atmosphere) ? 0.5f : 1.0f;
	Fog->SetFogDensity(3.3f / FMath::Max(End - Start, 1.0f) * 1000.0f * Thin);
	const FLinearColor SeaColour(0.04f, 0.2f, 0.34f, 1.0f);
	const FLinearColor Shown = FMath::Lerp(FogColor, SeaColour * (0.35f + 0.65f * McSky::GetSkyLightFactor()), Under);
	Fog->FogInscatteringLuminance = DisplayToLinear(Shown) * ((Mode != EMode::Overworld && McRenderMode::IsEnhanced()) ? EnhancedFogBoost : 1.0f);
	Fog->MarkRenderStateDirty();
}
