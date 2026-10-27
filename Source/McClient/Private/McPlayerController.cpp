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
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "McSkyComponent.h"
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
			ChunkStream->OnAttackRequest = [this](const FVector& Look)
			{
				// The direction comes from the client: ignore one that isn't a direction.
				if (Look.ContainsNaN() || Look.IsNearlyZero())
				{
					return;
				}
				const APawn* MyPawn = GetPawn();
				if (WorldActor && MyPawn)
				{
					WorldActor->AttackMobs(McCoords::UnrealToBlock(MyPawn->GetActorLocation()) + FVector(0.0, 1.62, 0.0), Look.GetSafeNormal(), 3.0, 1.0f);
				}
			};
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
		MyPawn->AttackHandler = [WeakStream = TWeakObjectPtr<UMcChunkStreamComponent>(ChunkStream)](const FVector& Look)
		{
			if (UMcChunkStreamComponent* Stream = WeakStream.Get())
			{
				Stream->RequestAttack(Look);
			}
		};
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
	// -McEditY=<y> is the pillar's lowest block, as for the host; -McPlaceAfter=<seconds> puts a diamond block under
	// the pillar; -McCam=x,y,z,yaw,pitch moves a client's own view, as it does on the host.
	float BreakDelay = 0.0f;
	float PlaceDelay = 0.0f;
	int32 EditY = -60;
	FParse::Value(FCommandLine::Get(), TEXT("-McEditY="), EditY);
	if (FParse::Value(FCommandLine::Get(), TEXT("-McBreakAfter="), BreakDelay))
	{
		FTimerHandle BreakTimer;
		GetWorldTimerManager().SetTimer(BreakTimer, FTimerDelegate::CreateWeakLambda(this, [this, EditY]()
		{
			ChunkStream->RequestSetBlock(FIntVector(3, EditY + 1, 0), FMcBlockRegistry::Get().FindDefaultState(TEXT("minecraft:air")).Get(0));
			UE_LOG(LogMcClient, Display, TEXT("Broke the pillar's eye-level block"));
		}), BreakDelay, false);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("-McPlaceAfter="), PlaceDelay))
	{
		FTimerHandle PlaceTimer;
		GetWorldTimerManager().SetTimer(PlaceTimer, FTimerDelegate::CreateWeakLambda(this, [this, EditY]()
		{
			ChunkStream->RequestSetBlock(FIntVector(3, EditY - 1, 0), FMcBlockRegistry::Get().FindDefaultState(TEXT("minecraft:diamond_block")).Get(0));
			UE_LOG(LogMcClient, Display, TEXT("Placed a diamond block under the pillar"));
		}), PlaceDelay, false);
	}
	FString Camera;
	TArray<FString> CameraParts;
	if (!HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("-McCam="), Camera, /*bShouldStopOnSeparator=*/ false))
	{
		Camera.ParseIntoArray(CameraParts, TEXT(","));
	}
	if (CameraParts.Num() == 5)
	{
		// The pawn arrives a moment after the controller.
		const TSharedRef<FTimerHandle> CameraTimer = MakeShared<FTimerHandle>();
		GetWorldTimerManager().SetTimer(*CameraTimer, FTimerDelegate::CreateWeakLambda(this, [this, CameraParts, CameraTimer]()
		{
			if (AMcPlayerPawn* MyPawn = Cast<AMcPlayerPawn>(GetPawn()))
			{
				MyPawn->SetView(FVector(FCString::Atod(*CameraParts[0]), FCString::Atod(*CameraParts[1]), FCString::Atod(*CameraParts[2])), FCString::Atof(*CameraParts[3]), FCString::Atof(*CameraParts[4]));
				GetWorldTimerManager().ClearTimer(*CameraTimer);
			}
		}), 0.5f, true);
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

	// The sky, sun, moon, stars and fog follow the world's time of day, which the server sets and replicates.
	UMcSkyComponent* Sky = NewObject<UMcSkyComponent>(WorldActor, TEXT("Sky"));
	Sky->SetupAttachment(WorldActor->GetRootComponent());
	Sky->GetTime = [Actor = TWeakObjectPtr<AMcWorldActor>(WorldActor)]()
	{
		return Actor.IsValid() ? Actor->GetDayTime() : 1000.0;
	};
	Sky->RegisterComponent();
#endif
}
