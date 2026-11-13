#include "McBlockLightComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McRenderMode.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "McPerf.h"

namespace
{
	constexpr int32 SlicesPerPass = 8;
	// Candelas at a block that gives light 15; a block is a metre, so lux a block away.
	constexpr float CandelasAtFull = 5.0f;
}

int32 UMcBlockLightComponent::Budget()
{
	static const int32 Override = []()
	{
#if !UE_BUILD_SHIPPING
		int32 Value = -1;
		FParse::Value(FCommandLine::Get(), TEXT("-McBlockLights="), Value);
		return Value;
#else
		return -1;
#endif
	}();
	if (Override >= 0)
	{
		return FMath::Min(Override, MaxLights);
	}
	static constexpr int32 ByQuality[] = { 8, 16, 24, 32 };
	return ByQuality[FMath::Clamp(McRenderMode::GetQuality(), 0, 3)];
}

int32 UMcBlockLightComponent::ShadowBudget()
{
	static constexpr int32 ByQuality[] = { 0, 3, 6, 8 };
	return ByQuality[FMath::Clamp(McRenderMode::GetQuality(), 0, 3)];
}

UMcBlockLightComponent::UMcBlockLightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UMcBlockLightComponent::SetWorld(const FMcBlockWorld* InWorld)
{
	World = InWorld;
	Found.Reset();
	Scanning.Reset();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcBlockLight& BlockLight = FMcBlockLight::Get();
	if (!World || !BlockLight.EnsureLoaded())
	{
		return;
	}
	Emission.Init(0, Registry.NumStates());
	Colours.Init(FLinearColor::White, Registry.NumStates());
	Flickers.Init(0, Registry.NumStates());
	for (int32 Id = 0; Id < Registry.NumStates(); ++Id)
	{
		const FMcStateId State = static_cast<FMcStateId>(Id);
		if (!Registry.IsValidState(State))
		{
			continue;
		}
		const uint8 Level = BlockLight.GetEmission(State);
		// Dim glows (magma, sculk, brewing stands) light little; leave them to the chunk's light.
		if (Level < 7)
		{
			continue;
		}
		const FString Name = Registry.GetBlockOf(State).Name.ToString();
		Emission[Id] = Level;
		FLinearColor& Colour = Colours[Id];
		if (Name.Contains(TEXT("soul")))
		{
			Colour = FLinearColor(0.35f, 0.75f, 1.0f);
		}
		else if (Name == TEXT("minecraft:lava") || Name.Contains(TEXT("magma")))
		{
			Colour = FLinearColor(1.0f, 0.38f, 0.08f);
		}
		else if (Name.Contains(TEXT("sea_lantern")) || Name.Contains(TEXT("beacon")) || Name.Contains(TEXT("end_rod")) || Name.Contains(TEXT("froglight")))
		{
			Colour = FLinearColor(0.85f, 0.95f, 1.0f);
		}
		else if (Name.Contains(TEXT("redstone")))
		{
			Colour = FLinearColor(1.0f, 0.2f, 0.1f);
		}
		else if (Name.Contains(TEXT("amethyst")))
		{
			Colour = FLinearColor(0.8f, 0.5f, 1.0f);
		}
		else
		{
			Colour = FLinearColor(1.0f, 0.68f, 0.38f);
		}
		Flickers[Id] = Name.Contains(TEXT("torch")) || Name.Contains(TEXT("campfire")) || Name.Contains(TEXT("fire")) || Name.Contains(TEXT("candle"))
			|| (Name.Contains(TEXT("lantern")) && !Name.Contains(TEXT("sea_lantern")));
	}
}

void UMcBlockLightComponent::ScanSlice()
{
	// A slab of the cube round the camera, a few layers deep.
	const int32 Layers = (Radius * 2 + 1 + SlicesPerPass - 1) / SlicesPerPass;
	const int32 FromY = ScanCentre.Y - Radius + ScanSliceIndex * Layers;
	const int32 ToY = FMath::Min(FromY + Layers - 1, ScanCentre.Y + Radius);
	for (int32 Y = FromY; Y <= ToY; ++Y)
	{
		for (int32 Z = ScanCentre.Z - Radius; Z <= ScanCentre.Z + Radius; ++Z)
		{
			for (int32 X = ScanCentre.X - Radius; X <= ScanCentre.X + Radius; ++X)
			{
				const FIntVector Pos(X, Y, Z);
				const FMcStateId State = World->GetBlock(Pos);
				if (State != 0 && Emission.IsValidIndex(State) && Emission[State] > 0)
				{
					// Lava with lava over it gives no light anyone sees.
					if (World->GetBlock(Pos + FIntVector(0, 1, 0)) == State && !Flickers[State])
					{
						continue;
					}
					Scanning.Add({ Pos, State, FVector::DistSquared(FVector(Pos), FVector(ScanCentre)) });
				}
			}
		}
	}
	if (++ScanSliceIndex >= SlicesPerPass)
	{
		ScanSliceIndex = 0;
		Scanning.Sort([](const FFound& A, const FFound& B) { return A.DistanceSquared < B.DistanceSquared; });
		// Lava lakes would take every light: a lit block's neighbours within two blocks give way to it.
		Found.Reset();
		for (const FFound& Candidate : Scanning)
		{
			if (Found.Num() >= Budget())
			{
				break;
			}
			bool bNear = false;
			for (const FFound& Kept : Found)
			{
				bNear |= (Kept.Pos - Candidate.Pos).GetAbsMax() <= 2;
			}
			if (!bNear)
			{
				Found.Add(Candidate);
			}
		}
		Scanning.Reset();
	}
}

void UMcBlockLightComponent::Place()
{
	while (Lights.Num() < MaxLights)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(GetOwner());
		Light->SetupAttachment(this);
		Light->SetUsingAbsoluteLocation(true);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetSourceRadius(8.0f);
		Light->SetSoftSourceRadius(20.0f);
		Light->bUseInverseSquaredFalloff = true;
		Light->SetVolumetricScatteringIntensity(0.6f);
		Light->SetVisibility(false);
		Light->RegisterComponent();
		Lights.Add(Light);
	}
	NumShown = 0;
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		UPointLightComponent* Light = Lights[Index];
		if (!Found.IsValidIndex(Index) || Index >= Budget())
		{
			Light->SetVisibility(false);
			continue;
		}
		const FFound& Lamp = Found[Index];
		const float Level = Emission[Lamp.State] / 15.0f;
		float Flicker = 1.0f;
		if (Flickers[Lamp.State])
		{
			const double Seed = Lamp.Pos.X * 12.9898 + Lamp.Pos.Y * 78.233 + Lamp.Pos.Z * 37.719;
			Flicker = 0.9f + 0.06f * FMath::Sin(Time * 9.0 + Seed) + 0.04f * FMath::Sin(Time * 23.0 + Seed * 1.7);
		}
		Light->SetWorldLocation(McCoords::BlockToUnreal(FVector(Lamp.Pos) + FVector(0.5, 0.6, 0.5)));
		Light->SetLightColor(Colours[Lamp.State]);
		Light->SetIntensity(CandelasAtFull * Level * Level * Flicker);
		Light->SetAttenuationRadius(Emission[Lamp.State] * McCoords::UnitsPerBlock);
		Light->SetCastShadows(Index < ShadowBudget());
		Light->SetVisibility(true);
		++NumShown;
	}
}

void UMcBlockLightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	MC_PERF_SCOPE(BlockLight);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Time += DeltaTime;
	if (!World || !McRenderMode::IsEnhanced())
	{
		if (NumShown > 0)
		{
			for (UPointLightComponent* Light : Lights)
			{
				Light->SetVisibility(false);
			}
			NumShown = 0;
		}
		return;
	}
	if (ScanSliceIndex == 0)
	{
		if (const APlayerController* Player = GetWorld()->GetFirstPlayerController())
		{
			if (Player->PlayerCameraManager)
			{
				const FVector Camera = McCoords::UnrealToBlock(Player->PlayerCameraManager->GetCameraLocation());
				ScanCentre = FIntVector(FMath::FloorToInt(Camera.X), FMath::FloorToInt(Camera.Y), FMath::FloorToInt(Camera.Z));
			}
		}
	}
	ScanSlice();
	Place();
}
