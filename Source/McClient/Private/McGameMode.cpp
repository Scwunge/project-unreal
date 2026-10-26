#include "McGameMode.h"
#include "McChunk.h"
#include "McClient.h"
#include "McFreeCameraPawn.h"
#include "McWorldActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"

#if MC_WITH_RENDER
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "McWorldRenderComponent.h"
#endif

AMcGameMode::AMcGameMode()
{
	DefaultPawnClass = AMcFreeCameraPawn::StaticClass();
}

void AMcGameMode::StartPlay()
{
	Super::StartPlay();

	// Spawned after Super::StartPlay so the world has begun play and BeginPlay runs straight away.
	WorldActor = GetWorld()->SpawnActor<AMcWorldActor>();
	if (!WorldActor || !WorldActor->GetBlockWorld())
	{
		UE_LOG(LogMcClient, Error, TEXT("No world to play in; see the McServer and McCore errors above."));
		return;
	}

#if MC_WITH_RENDER
	if (GetNetMode() != NM_DedicatedServer)
	{
		UMcWorldRenderComponent* Renderer = NewObject<UMcWorldRenderComponent>(WorldActor, TEXT("Renderer"));
		Renderer->SetupAttachment(WorldActor->GetRootComponent());
		Renderer->RegisterComponent();
		Renderer->SetWorld(WorldActor->GetBlockWorld());
		SpawnEnvironment();
	}
#endif

	// Players that joined before the world existed started at a guessed height; move them above the surface.
	const FTransform Spawn = GetSpawnTransform();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			Pawn->SetActorLocation(Spawn.GetLocation());
		}
	}

	StartDevCapture();
}

void AMcGameMode::LoadDevScene(const FString& Name, FString& OutCamera)
{
	// Scenes are boxes of block states, shared with tools/scene_to_mcfunction.py so the same scene can be
	// built in real 26.3 for side-by-side screenshots.
	const FString Path = FPaths::ProjectDir() / TEXT("tools/scenes") / Name + TEXT(".json");
	const TSharedPtr<FJsonObject> Scene = McCache::LoadJsonObject(Path);
	if (!Scene.IsValid())
	{
		return;
	}

	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	int32 Placed = 0;
	for (const TSharedPtr<FJsonValue>& BoxValue : Scene->GetArrayField(TEXT("blocks")))
	{
		const TSharedPtr<FJsonObject>& Box = BoxValue->AsObject();
		const FString StateText = Box->GetStringField(TEXT("state"));
		const TOptional<FMcStateId> State = Registry.ParseState(StateText);
		const TArray<TSharedPtr<FJsonValue>>& From = Box->GetArrayField(TEXT("from"));
		const TArray<TSharedPtr<FJsonValue>>& To = Box->GetArrayField(TEXT("to"));
		if (!State.IsSet() || From.Num() != 3 || To.Num() != 3)
		{
			UE_LOG(LogMcClient, Error, TEXT("%s: can't place %s"), *Path, *StateText);
			continue;
		}

		auto ToBlock = [](const TArray<TSharedPtr<FJsonValue>>& Values)
		{
			return FIntVector(FMath::RoundToInt32(Values[0]->AsNumber()), FMath::RoundToInt32(Values[1]->AsNumber()), FMath::RoundToInt32(Values[2]->AsNumber()));
		};
		const FIntVector A = ToBlock(From);
		const FIntVector B = ToBlock(To);
		for (int32 Y = FMath::Min(A.Y, B.Y); Y <= FMath::Max(A.Y, B.Y); ++Y)
		{
			for (int32 Z = FMath::Min(A.Z, B.Z); Z <= FMath::Max(A.Z, B.Z); ++Z)
			{
				for (int32 X = FMath::Min(A.X, B.X); X <= FMath::Max(A.X, B.X); ++X)
				{
					WorldActor->SetBlock(FIntVector(X, Y, Z), State.GetValue());
					++Placed;
				}
			}
		}
	}

	TArray<FString> CameraParts;
	for (const TSharedPtr<FJsonValue>& Value : Scene->GetArrayField(TEXT("camera")))
	{
		CameraParts.Add(FString::SanitizeFloat(Value->AsNumber()));
	}
	OutCamera = FString::Join(CameraParts, TEXT(","));
	UE_LOG(LogMcClient, Display, TEXT("Scene %s: placed %d blocks"), *Name, Placed);
}

void AMcGameMode::StartDevCapture()
{
#if !UE_BUILD_SHIPPING && MC_WITH_RENDER
	const TCHAR* CommandLine = FCommandLine::Get();

	FString Camera;
	FString SceneName;
	if (FParse::Value(CommandLine, TEXT("-McScene="), SceneName))
	{
		LoadDevScene(SceneName, Camera);
	}
	FParse::Value(CommandLine, TEXT("-McCam="), Camera, /*bShouldStopOnSeparator=*/ false);
	if (!Camera.IsEmpty())
	{
		TArray<FString> Parts;
		Camera.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 5)
		{
			const FVector Block(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (AMcFreeCameraPawn* Pawn = It->Get() ? Cast<AMcFreeCameraPawn>(It->Get()->GetPawn()) : nullptr)
				{
					Pawn->SetView(Block, FCString::Atof(*Parts[3]), FCString::Atof(*Parts[4]));
				}
			}
		}
		else
		{
			UE_LOG(LogMcClient, Error, TEXT("-McCam wants x,y,z,yaw,pitch; got %s"), *Camera);
		}
	}

	FString ShotPath;
	if (!FParse::Value(CommandLine, TEXT("-McShot="), ShotPath))
	{
		return;
	}
	float Delay = 5.0f;
	FParse::Value(CommandLine, TEXT("-McShotDelay="), Delay);

	FTimerHandle ShotTimer;
	GetWorldTimerManager().SetTimer(ShotTimer, FTimerDelegate::CreateWeakLambda(this, [this, ShotPath]()
	{
		UE_LOG(LogMcClient, Display, TEXT("McShot: saving %s"), *ShotPath);
		FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
		FTimerHandle ExitTimer;
		GetWorldTimerManager().SetTimer(ExitTimer, FTimerDelegate::CreateWeakLambda(this, []()
		{
			FPlatformMisc::RequestExit(false, TEXT("McShot"));
		}), 1.0f, false);
	}), Delay, false);
#endif
}

void AMcGameMode::RestartPlayer(AController* NewPlayer)
{
	// There are no PlayerStart actors: players start above the middle of block 0, 0.
	if (NewPlayer && !NewPlayer->IsPendingKillPending())
	{
		RestartPlayerAtTransform(NewPlayer, GetSpawnTransform());
	}
}

FTransform AMcGameMode::GetSpawnTransform() const
{
	// Before the world exists, guess the classic flat surface (y = -61).
	const int32 SurfaceY = WorldActor && WorldActor->GetBlockWorld() ? WorldActor->GetSurfaceY() : -61;
	const FVector Block(0.5, SurfaceY + 1 + SpawnHeightAboveSurface, 0.5);
	return FTransform(FRotator::ZeroRotator, McCoords::BlockToUnreal(Block));
}

void AMcGameMode::SpawnEnvironment()
{
#if MC_WITH_RENDER
	// A plain daylight sky to see the greybox by. M8 replaces it with Minecraft's sky, sun, moon and fog.
	UWorld* World = GetWorld();

	// Blocks carry Minecraft's final colours; the filmic tonemapper would brighten and desaturate them.
	if (UGameViewportClient* Viewport = World->GetGameViewport())
	{
		Viewport->EngineShowFlags.SetTonemapper(false);
	}

	// Fixed exposure, so an emissive 1.0 shows as the texture's own colour. -McExposureBias= is for tuning.
	if (APostProcessVolume* Post = World->SpawnActor<APostProcessVolume>())
	{
		Post->bUnbound = true;
		FPostProcessSettings& Settings = Post->Settings;
		Settings.bOverride_AutoExposureMethod = true;
		Settings.AutoExposureMethod = AEM_Manual;
		Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Settings.AutoExposureApplyPhysicalCameraExposure = false;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 0.0f;
		FParse::Value(FCommandLine::Get(), TEXT("-McExposureBias="), Settings.AutoExposureBias);
	}

	const FTransform SunTransform(FRotator(-50.0f, 30.0f, 0.0f));

	if (ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), SunTransform))
	{
		if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunLight->SetMobility(EComponentMobility::Movable);
			SunLight->SetAtmosphereSunLight(true);
		}
		Sun->FinishSpawning(SunTransform);
	}

	World->SpawnActor<ASkyAtmosphere>();

	if (ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity))
	{
		if (USkyLightComponent* SkyLight = Sky->GetLightComponent())
		{
			SkyLight->SetMobility(EComponentMobility::Movable);
			SkyLight->bRealTimeCapture = true;
		}
		Sky->FinishSpawning(FTransform::Identity);
	}
#endif
}
