#include "McRenderMode.h"
#include "McRender.h"
#include "McHdBlocks.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2DArray.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	bool GEnhanced = false;
	bool GHd = false;
	uint32 GGeneration = 1;

	// Vanilla material -> its lit twin (null when it has none). The twins are assets, kept loaded once found.
	TMap<TObjectKey<UMaterialInterface>, TWeakObjectPtr<UMaterialInterface>>& GetTwins(bool bHd = false)
	{
		static TMap<TObjectKey<UMaterialInterface>, TWeakObjectPtr<UMaterialInterface>> Twins;
		static TMap<TObjectKey<UMaterialInterface>, TWeakObjectPtr<UMaterialInterface>> HdTwins;
		return bHd ? HdTwins : Twins;
	}

	int32 GQuality = 2;
	bool GRayTracing = false;

	void SetConsoleFloat(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}

	void SetConsoleVariable(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}
}

static void ApplyQuality();

bool McRenderMode::IsEnhanced()
{
	return GEnhanced;
}

void McRenderMode::SetEnhanced(bool bEnhanced)
{
	if (bEnhanced == GEnhanced)
	{
		return;
	}
	GEnhanced = bEnhanced;
	++GGeneration;
	// The project turns virtual shadow maps off for the vanilla look, which casts no shadows at all.
	SetConsoleVariable(TEXT("r.Shadow.Virtual.Enable"), bEnhanced ? 1 : 0);
	ApplyQuality();
	UE_LOG(LogMcRender, Log, TEXT("Graphics: %s"), bEnhanced ? TEXT("enhanced") : TEXT("vanilla"));
}

// Quality presets are listed Low to Ultra. Vanilla (Unreal Mode off) goes back to the plain full-scale picture.
static void ApplyQuality()
{
	struct FPreset { float Scale; int32 Probe; int32 Reflections; float ShadowBias; int32 Fog; int32 Post; };
	static const FPreset Presets[4] = {
		{ 70.0f, 32, 0, 2.0f, 0, 0 },
		{ 85.0f, 16, 1, 1.0f, 1, 1 },
		{ 100.0f, 16, 1, 0.0f, 1, 2 },
		{ 100.0f, 8, 1, -0.5f, 1, 3 },
	};
	const FPreset& P = Presets[FMath::Clamp(GQuality, 0, 3)];
	// Hardware ray tracing: Lumen traces the real scene (the procedural meshes included), and translucent surfaces get rays.
	const bool bRays = McRenderMode::IsRayTracing();
	SetConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"), bRays ? 1 : 0);
	SetConsoleVariable(TEXT("r.RayTracing.Shadows"), bRays ? 1 : 0);
	SetConsoleVariable(TEXT("r.RayTracing.Translucency"), bRays ? 1 : 0);
	SetConsoleVariable(TEXT("r.RayTracing.Reflections"), bRays ? 1 : 0);
	SetConsoleVariable(TEXT("r.RayTracing.Geometry.ProceduralMeshes"), bRays ? 1 : 0);
	if (!GEnhanced)
	{
		SetConsoleFloat(TEXT("r.ScreenPercentage"), 100.0f);
		SetConsoleVariable(TEXT("r.AntiAliasingMethod"), 0);
		return;
	}
	// TSR smooths the edges and fills out the lower render scales; vanilla keeps the crisp unfiltered pixels (none).
	SetConsoleVariable(TEXT("r.AntiAliasingMethod"), 4);
	SetConsoleFloat(TEXT("r.ScreenPercentage"), P.Scale);
	SetConsoleVariable(TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"), P.Probe);
	SetConsoleVariable(TEXT("r.Lumen.Reflections.Allow"), P.Reflections);
	SetConsoleFloat(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"), P.ShadowBias);
	SetConsoleVariable(TEXT("r.VolumetricFog"), P.Fog);
	SetConsoleVariable(TEXT("r.BloomQuality"), P.Post == 0 ? 3 : 5);
	SetConsoleVariable(TEXT("r.DepthOfFieldQuality"), P.Post >= 3 ? 2 : 0);
	SetConsoleVariable(TEXT("r.MotionBlurQuality"), P.Post >= 2 ? 3 : 0);
}

bool McRenderMode::IsRayTracingSupported()
{
	return GRHISupportsRayTracing && IsRayTracingEnabled();
}

bool McRenderMode::GetRayTracingSwitch()
{
	return GRayTracing;
}

bool McRenderMode::IsRayTracing()
{
	return GEnhanced && GRayTracing && IsRayTracingSupported();
}

void McRenderMode::SetRayTracing(bool bRayTracing)
{
	if (bRayTracing == GRayTracing)
	{
		return;
	}
	GRayTracing = bRayTracing;
	++GGeneration;
	ApplyQuality();
	UE_LOG(LogMcRender, Log, TEXT("Ray tracing: %s%s"), bRayTracing ? TEXT("on") : TEXT("off"), IsRayTracingSupported() ? TEXT("") : TEXT(" (not supported here, so no effect)"));
}

int32 McRenderMode::GetQuality()
{
	return GQuality;
}

void McRenderMode::SetQuality(int32 Quality)
{
	GQuality = FMath::Clamp(Quality, 0, 3);
	ApplyQuality();
	UE_LOG(LogMcRender, Log, TEXT("Unreal Mode quality: %d"), GQuality);
}

float McRenderMode::GetEntityAmbient()
{
	return IsEnhanced() ? 2.0f : 1.0f;
}

bool McRenderMode::IsHd()
{
	return GEnhanced && GHd;
}

bool McRenderMode::GetHdSwitch()
{
	return GHd;
}

void McRenderMode::SetHd(bool bHd)
{
	if (bHd == GHd)
	{
		return;
	}
	GHd = bHd;
	++GGeneration;
	UE_LOG(LogMcRender, Log, TEXT("HD Textures: %s%s"), bHd ? TEXT("on") : TEXT("off"), GEnhanced ? TEXT("") : TEXT(" (Unreal Mode is off, so no effect yet)"));
}

uint32 McRenderMode::GetGeneration()
{
	return GGeneration;
}

UMaterialInterface* McRenderMode::Pick(UMaterialInterface* Vanilla)
{
	if (!GEnhanced || !Vanilla)
	{
		return Vanilla;
	}
	const bool bHd = GHd;
	TWeakObjectPtr<UMaterialInterface>* Found = GetTwins(bHd).Find(Vanilla);
	if (!Found)
	{
		const FString Path = FString::Printf(TEXT("%s%s.%s%s"), *Vanilla->GetOutermost()->GetName(), bHd ? TEXT("Hd") : TEXT("Lit"), *Vanilla->GetName(), bHd ? TEXT("Hd") : TEXT("Lit"));
		UMaterialInterface* Twin = Vanilla->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
			? LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)
			: nullptr;
		if (Twin)
		{
			Twin->AddToRoot();
		}
		else
		{
			UE_LOG(LogMcRender, Verbose, TEXT("%s has no lit twin; enhanced mode draws it as it is"), *Vanilla->GetPathName());
		}
		Found = &GetTwins(bHd).Add(Vanilla, TWeakObjectPtr<UMaterialInterface>(Twin));
	}
	if (Found->IsValid())
	{
		return Found->Get();
	}
	// No HD twin (not made yet): the Lit one.
	if (bHd)
	{
		GHd = false;
		UMaterialInterface* Lit = Pick(Vanilla);
		GHd = true;
		return Lit;
	}
	return Vanilla;
}

void McRenderMode::ApplyHdBlocks(UMaterialInstanceDynamic* Material)
{
	if (!Material || !IsHd() || !FMcHdBlocks::Get().EnsureBuilt())
	{
		return;
	}
	FMcHdBlocks& Hd = FMcHdBlocks::Get();
	Material->SetTextureParameterValue(TEXT("BlocksHd"), Hd.GetAlbedo());
	Material->SetTextureParameterValue(TEXT("BlocksHdN"), Hd.GetNormal());
	Material->SetTextureParameterValue(TEXT("BlocksHdS"), Hd.GetSpecular());
}

void McRenderMode::ApplyHdPicture(UMaterialInstanceDynamic* Material, const FString& PngPath, bool bWhite)
{
	if (!Material || !IsHd())
	{
		return;
	}
	// "<cache>/client/assets/minecraft/textures/entity/zombie/zombie.png" -> "minecraft:entity/zombie/zombie"
	FString Id = PngPath.Replace(TEXT("\\"), TEXT("/"));
	int32 At = Id.Find(TEXT("/textures/"));
	Id = (At == INDEX_NONE ? Id : Id.Mid(At + 10));
	Id.RemoveFromEnd(TEXT(".png"));
	Id = TEXT("minecraft:") + Id;
	FMcHdBlocks& Hd = FMcHdBlocks::Get();
	const FMcHdPicture Picture = Hd.GetPicture(Id, PngPath);
	if (!Picture.IsValid())
	{
		return;
	}
	Material->SetTextureParameterValue(TEXT("BlocksHd"), bWhite ? Hd.GetWhitePicture(Id, PngPath) : Picture.Albedo.Get());
	Material->SetTextureParameterValue(TEXT("BlocksHdN"), Picture.Normal);
	Material->SetTextureParameterValue(TEXT("BlocksHdS"), Picture.Specular);
}
