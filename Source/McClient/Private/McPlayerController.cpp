#include "McPlayerController.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "Camera/PlayerCameraManager.h"
#include "McChunk.h"
#include "McChunkStreamComponent.h"
#include "McPlayerPawn.h"
#include "McWorldActor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "McBlockShapes.h"
#include "McClient.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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

AMcPlayerController::AMcPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	ChunkStream = CreateDefaultSubobject<UMcChunkStreamComponent>(TEXT("ChunkStream"));
}

void AMcPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ConnectToWorld();

#if !UE_BUILD_SHIPPING
	// Development: -McNetLog logs where this player is and how many chunks it has, every two seconds.
	static const bool bNetLog = FParse::Param(FCommandLine::Get(), TEXT("McNetLog"));
	NetLogTimer += DeltaSeconds;
	if (bNetLog && IsLocalController() && NetLogTimer > 2.0f)
	{
		NetLogTimer = 0.0f;
		const FMcBlockWorld* BlockWorld = WorldActor ? WorldActor->GetBlockWorld() : nullptr;
		const FVector Feet = GetPawn() ? McCoords::UnrealToBlock(GetPawn()->GetActorLocation()) : FVector::ZeroVector;
		UE_LOG(LogMcClient, Display, TEXT("McNetLog: %s pawn %s at %.2f %.2f %.2f, %d chunks in the world, camera at %s"),
			HasAuthority() ? TEXT("host") : TEXT("client"), GetPawn() ? *GetPawn()->GetName() : TEXT("none"), Feet.X, Feet.Y, Feet.Z,
			BlockWorld ? BlockWorld->NumChunks() : -1,
			PlayerCameraManager ? *McCoords::UnrealToBlock(PlayerCameraManager->GetCameraLocation()).ToString() : TEXT("none"));
	}
#endif
}

void AMcPlayerController::ConnectToWorld()
{
	// The world actor is spawned by the game mode on the server and replicated, so on a client it may come
	// a little after this controller.
	if (!WorldActor)
	{
		for (TActorIterator<AMcWorldActor> It(GetWorld()); It; ++It)
		{
			WorldActor = *It;
			break;
		}
	}
	FMcBlockWorld* BlockWorld = WorldActor ? WorldActor->GetBlockWorld() : nullptr;
	if (!BlockWorld)
	{
		return;
	}

	if (!bStreamConnected && (HasAuthority() || IsLocalController()))
	{
		ChunkStream->SetBlockWorld(BlockWorld);
		if (HasAuthority())
		{
			ChunkStream->OnSetBlockRequest.BindUObject(this, &AMcPlayerController::HandleSetBlockRequest);
		}
		bStreamConnected = true;
	}

	if (!IsLocalController())
	{
		return;
	}
	if (!bViewSetUp)
	{
		FMcBlockShapes::Get().EnsureLoaded();
		SetUpLocalView();
		bViewSetUp = true;
	}
	AMcPlayerPawn* MyPawn = Cast<AMcPlayerPawn>(GetPawn());
	if (MyPawn && !MyPawn->HasBlockWorld())
	{
		MyPawn->SetBlockWorld(BlockWorld);
		MyPawn->SetBlockHandler = [WeakStream = TWeakObjectPtr<UMcChunkStreamComponent>(ChunkStream)](const FIntVector& Pos, FMcStateId State)
		{
			if (UMcChunkStreamComponent* Stream = WeakStream.Get())
			{
				Stream->RequestSetBlock(Pos, State);
			}
		};
	}
}

void AMcPlayerController::HandleSetBlockRequest(const FIntVector& Pos, uint16 State)
{
	// Only valid states, and only within reach of the player's eyes.
	if (!WorldActor || !FMcBlockRegistry::Get().IsValidState(State))
	{
		return;
	}
	if (const APawn* MyPawn = GetPawn())
	{
		const FVector Eye = McCoords::UnrealToBlock(MyPawn->GetActorLocation()) + FVector(0.0, 1.62, 0.0);
		const double Distance = FVector::Dist(Eye, FVector(Pos) + FVector(0.5));
		if (Distance > MaxEditDistance)
		{
			UE_LOG(LogMcClient, Warning, TEXT("%s can't change %s: %.1f blocks from their eyes at %s"),
				*GetName(), *Pos.ToString(), Distance, *Eye.ToString());
			return;
		}
	}
	WorldActor->SetBlock(Pos, State);
}

void AMcPlayerController::SetUpLocalView()
{
#if MC_WITH_RENDER
	UMcWorldRenderComponent* Renderer = NewObject<UMcWorldRenderComponent>(WorldActor, TEXT("Renderer"));
	Renderer->SetupAttachment(WorldActor->GetRootComponent());
	Renderer->RegisterComponent();
	Renderer->SetWorld(WorldActor->GetBlockWorld());
	SpawnEnvironment();
#endif
	StartDevScreenshot();
}

void AMcPlayerController::StartDevScreenshot()
{
#if !UE_BUILD_SHIPPING && MC_WITH_RENDER
	// Development: -McShot=<png> saves a screenshot after -McShotDelay seconds (default 5) and quits. Used for
	// side-by-side checks, on the host or on a client.
	// -McBreakAfter=<seconds>: this player breaks the block at 3, -59, 0 (eye level in -McPillarAfter's
	// pillar), to check that edits reach the server and the other players.
	float BreakDelay = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McBreakAfter="), BreakDelay))
	{
		FTimerHandle BreakTimer;
		GetWorldTimerManager().SetTimer(BreakTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			ChunkStream->RequestSetBlock(FIntVector(3, -59, 0), FMcBlockRegistry::Get().FindDefaultState(TEXT("minecraft:air")).Get(0));
			UE_LOG(LogMcClient, Display, TEXT("Broke the pillar's eye-level block"));
		}), BreakDelay, false);
	}

	FString ShotPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-McShot="), ShotPath))
	{
		return;
	}
	float Delay = 5.0f;
	FParse::Value(FCommandLine::Get(), TEXT("-McShotDelay="), Delay);

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

void AMcPlayerController::SpawnEnvironment()
{
#if MC_WITH_RENDER
	// A plain daylight sky to see by. M8 replaces it with Minecraft's sky, sun, moon and fog.
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
