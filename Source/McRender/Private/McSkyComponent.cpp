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
#include "Engine/GameViewportClient.h"
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
	constexpr float NetherFogStartBlocks = 10.0f;
	constexpr float NetherFogEndBlocks = 96.0f;
	// The enhanced look's sun and moon, in lux. Lit surfaces show their colour times the light over pi, before the
	// tonemapper; the moon is a tenth of a lux or so, enough to see by.
	constexpr float SunLux = 7.0f;
	constexpr float MoonLux = 0.6f;
	// How much of the fog's thickness the volumetric fog takes: a haze with light shafts, not a wall.
	constexpr float VolumetricFogShare = 0.04f;

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
	Sun->SetVisibility(bOverworld && CelestialAlpha > 0.001f);
	Moon->SetVisibility(bOverworld && CelestialAlpha > 0.001f);
	Stars->SetVisibility(bOverworld && StarBrightness > 0.001f);
	if (EndSky)
	{
		EndSky->SetVisibility(Mode == EMode::End);
	}
	// The end sky is the whole sky, drawn on black rather than added to the dome's colour.
	Dome->SetVisibility(Mode != EMode::End);

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
	SunLight->SetVisibility(false);
	SunLight->RegisterComponent();

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
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = 0.35f;
	Settings.bOverride_AmbientOcclusionIntensity = true;
	Settings.AmbientOcclusionIntensity = 0.5f;
	Settings.bOverride_AmbientOcclusionRadius = true;
	Settings.AmbientOcclusionRadius = 120.0f;
#if !UE_BUILD_SHIPPING
	// For tuning: -McEnhancedExposure= (stops), -McSunLux=.
	FParse::Value(FCommandLine::Get(), TEXT("-McEnhancedExposure="), Settings.AutoExposureBias);
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
		Fog->SetVolumetricFogExtinctionScale(VolumetricFogShare);
		Fog->SetVolumetricFogDistance(FogEndBlocks * McCoords::UnitsPerBlock * 0.5f);
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
	if (Mode != EMode::Overworld)
	{
		// No sun in the Nether or the End: only the blocks' own light.
		SunLight->SetIntensity(0.0f);
		return;
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
	const float Day = FMath::SmoothStep(-0.04f, 0.12f, static_cast<float>(ToSun.Z));
	const float Clear = 1.0f - 0.8f * Rain;
	const bool bSun = Day > 0.0f;
	const FVector Towards = bSun ? ToSun : ToMoon;
	SunLight->SetWorldRotation((-Towards).Rotation());
	if (bSun)
	{
		// Low, the sun is orange; high, nearly white.
		const float High = FMath::Clamp(static_cast<float>(ToSun.Z) * 3.0f, 0.0f, 1.0f);
		SunLight->SetLightColor(FMath::Lerp(FLinearColor(1.0f, 0.55f, 0.3f), FLinearColor(1.0f, 0.96f, 0.9f), High));
		SunLight->SetIntensity(SunIntensity * Day * Clear);
	}
	else
	{
		SunLight->SetLightColor(FLinearColor(0.6f, 0.7f, 1.0f));
		SunLight->SetIntensity(MoonIntensity * FMath::SmoothStep(0.0f, 0.15f, static_cast<float>(ToMoon.Z)) * Clear);
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
	}
	TArray<FVector2D> Zero;
	Zero.Init(FVector2D::ZeroVector, Positions.Num());
	const TArray<FProcMeshTangent> NoTangents;
	Dome->UpdateMeshSection(0, Positions, Normals, UV0, Zero, Zero, Zero, Colours, NoTangents);
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
	const float End = (bNether ? FMath::Min(FogEndBlocks, NetherFogEndBlocks) : FogEndBlocks * (1.0f - 0.3f * Rain)) * McCoords::UnitsPerBlock;
	const float Start = bNether ? NetherFogStartBlocks * McCoords::UnitsPerBlock : End * 0.6f;
	Fog->SetStartDistance(Start);
	Fog->SetFogCutoffDistance(DomeRadius * 0.8f);
	Fog->SetFogDensity(3.3f / FMath::Max(End - Start, 1.0f) * 1000.0f);
	Fog->FogInscatteringLuminance = DisplayToLinear(FogColor);
	Fog->MarkRenderStateDirty();
}
