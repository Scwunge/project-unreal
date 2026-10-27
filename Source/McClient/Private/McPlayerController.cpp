#include "McPlayerController.h"
#include "McBlockUse.h"
#include "McEntityActor.h"
#include "McEntityHost.h"
#include "McGameMode.h"
#include "McPlayerData.h"
#include "McPortal.h"
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
	ResolveWantedDimension();
	if (IsLocalController() && WorldActor)
	{
		ViewSyncTimer -= DeltaSeconds;
		if (ViewSyncTimer <= 0.0f)
		{
			ViewSyncTimer = 0.2f;
			SyncDimensionView();
		}
	}

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
		// The first dimension's; the others come and go as players travel.
		for (TActorIterator<AMcWorldActor> It(GetWorld()); It; ++It)
		{
			if (!It->bSecondary)
			{
				WorldActor = *It;
				break;
			}
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
		ChunkStream->OnWorldSwitched = [this](const FString& Dimension)
		{
			WantedDimension = Dimension;
		};
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
				if (AMcPlayerPawn* MyPawn = Cast<AMcPlayerPawn>(GetPawn()))
				{
					MyPawn->ServerSwing(Look.GetSafeNormal());
				}
			};
		}
		bStreamConnected = true;
	}

	if (HasAuthority())
	{
		AMcPlayerPawn* ServerPawn = Cast<AMcPlayerPawn>(GetPawn());
		if (ServerPawn && !ServerPawn->bServerHooked)
		{
			HookServerPawn(*ServerPawn);
		}
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

void AMcPlayerController::HookServerPawn(AMcPlayerPawn& Hooked)
{
	if (!WorldActor || !WorldActor->GetBlockWorld())
	{
		return;
	}
	Hooked.bServerHooked = true;
	if (!Hooked.HasBlockWorld())
	{
		Hooked.SetBlockWorld(WorldActor->GetBlockWorld());
	}
	const TWeakObjectPtr<AMcWorldActor> World = WorldActor;
	Hooked.ServerSetBlock = [World](const FIntVector& Pos, FMcStateId State)
	{
		if (World.IsValid())
		{
			World->SetBlock(Pos, State);
		}
	};
	Hooked.GetEntities = [World]() -> FMcEntityManager*
	{
		FMcEntityHost* Host = World.IsValid() ? World->GetEntityHost() : nullptr;
		return Host ? &Host->GetManager() : nullptr;
	};
	Hooked.OnUseBlock = [World](AMcPlayerPawn& User, const FIntVector& Pos)
	{
		UMcBlockUseSubsystem* Use = User.GetWorld() ? User.GetWorld()->GetSubsystem<UMcBlockUseSubsystem>() : nullptr;
		return Use && World.IsValid() && Use->Use(User, *World, Pos);
	};
	Hooked.MobAttack = [World](const FVector& Eye, const FVector& Look, double Reach, float Damage)
	{
		return World.IsValid() && World->AttackMobs(Eye, Look, Reach, Damage);
	};
	Hooked.OnBeforeBreak = [World](const FIntVector& Pos)
	{
		UMcBlockUseSubsystem* Use = World.IsValid() ? World->GetWorld()->GetSubsystem<UMcBlockUseSubsystem>() : nullptr;
		if (Use)
		{
			Use->ReleaseBlock(*World, Pos);
		}
	};
	Hooked.OnUseItemOn = [World](AMcPlayerPawn& User, const FIntVector& Pos, const FIntVector& Beside)
	{
		FMcBlockWorld* Blocks = World.IsValid() ? World->GetBlockWorld() : nullptr;
		if (!Blocks || !User.Data)
		{
			return false;
		}
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FName Held = User.Data->Inventory->Get(User.Data->SelectedSlot).Item;
		// The builders of portals change blocks in pieces, so the world is not to look for broken frames in between.
		const McPortal::FSetBlock Place = [World](const FIntVector& At, FMcStateId State) { World->SetBlock(At, State, false); };
		if (Held == TEXT("minecraft:flint_and_steel"))
		{
			if (!Registry.IsAir(Blocks->GetBlock(Beside)))
			{
				return false;
			}
			World->SetBlock(Beside, Registry.FindDefaultState(TEXT("minecraft:fire")).Get(0));
			McPortal::TryLight(*Blocks, Beside, Place);
			return true;
		}
		const FMcStateId Clicked = Blocks->GetBlock(Pos);
		if (Held == TEXT("minecraft:ender_eye") && McPortal::IsEndPortalFrame(Clicked) && !McPortal::HasEye(Clicked))
		{
			World->SetBlock(Pos, McPortal::WithEye(Clicked));
			McPortal::TryOpenEndPortal(*Blocks, Pos, Place);
			return true;
		}
		return false;
	};
	// Dying shows the death screen; the player comes back when they press Respawn.
	Hooked.OnRespawnRequest = [WeakController = TWeakObjectPtr<AMcPlayerController>(this)](AMcPlayerPawn&)
	{
		if (AMcPlayerController* Dead = WeakController.Get())
		{
			if (AMcGameMode* Mode = Dead->GetWorld()->GetAuthGameMode<AMcGameMode>())
			{
				Mode->RespawnPlayer(Dead);
			}
		}
	};
	if (UMcPlayerDataComponent* Data = Hooked.Data)
	{
		const TWeakObjectPtr<AMcPlayerPawn> WeakPawn = &Hooked;
		Data->ServerDrop = [World, WeakPawn](const FMcItemStack& Stack)
		{
			FMcEntityHost* Host = World.IsValid() ? World->GetEntityHost() : nullptr;
			if (Host && WeakPawn.IsValid() && !Stack.IsEmpty())
			{
				// Thrown from the eyes, with a pause before it can be taken straight back.
				Host->GetManager().DropItem(WeakPawn->GetFeetPosition() + FVector(0.0, 1.3, 0.0), Stack, 40);
			}
		};
		Data->ServerWindowValid = [World, WeakPawn](const FMcWindow& Window)
		{
			return World.IsValid() && WeakPawn.IsValid() && UMcBlockUseSubsystem::IsWindowValid(*WeakPawn, *World, Window);
		};
	}
}

void AMcPlayerController::HandleSetBlockRequest(const FIntVector& Pos, uint16 State)
{
	// Survival players change blocks only by digging and placing (see AMcPlayerPawn::ServerDig); this is creative's.
	if (const AMcPlayerPawn* Current = Cast<AMcPlayerPawn>(GetPawn()))
	{
		if (!Current->Data || !Current->Data->bCreative)
		{
			return;
		}
	}
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
	AttachView();
	StartDevScreenshot();
}

void AMcPlayerController::AttachView()
{
#if MC_WITH_RENDER
	UMcWorldRenderComponent* Drawer = NewObject<UMcWorldRenderComponent>(WorldActor);
	Drawer->SetupAttachment(WorldActor->GetRootComponent());
	Drawer->RegisterComponent();
	Drawer->SetWorld(WorldActor->GetBlockWorld());
	Renderer = Drawer;
	SpawnEnvironment();
#endif
}

void AMcPlayerController::DetachView()
{
	if (UActorComponent* Drawer = Renderer.Get())
	{
		Drawer->DestroyComponent();
	}
	if (UActorComponent* Heavens = Sky.Get())
	{
		Heavens->DestroyComponent();
	}
	Renderer.Reset();
	Sky.Reset();
}

void AMcPlayerController::SwitchLocalView(AMcWorldActor* Old)
{
	DetachView();
	if (Old && Old != WorldActor)
	{
		Old->SetViewed(false);
		// A client forgets the dimension it left: the server sends it again if the player comes back.
		if (!HasAuthority() && Old->GetBlockWorld())
		{
			TArray<FIntPoint> Loaded;
			Old->GetBlockWorld()->GetChunks().GetKeys(Loaded);
			for (const FIntPoint& Pos : Loaded)
			{
				Old->GetBlockWorld()->RemoveChunk(Pos);
			}
		}
	}
	if (WorldActor)
	{
		WorldActor->SetViewed(true);
		AttachView();
	}
}

void AMcPlayerController::EnterWorld(AMcWorldActor& NewWorld)
{
	if (!HasAuthority() || !NewWorld.GetBlockWorld())
	{
		return;
	}
	AMcWorldActor* Old = WorldActor;
	WorldActor = &NewWorld;
	ChunkStream->SwitchWorld(NewWorld.GetBlockWorld(), NewWorld.Dimension);
	if (AMcPlayerPawn* MyPawn = Cast<AMcPlayerPawn>(GetPawn()))
	{
		MyPawn->SetBlockWorld(NewWorld.GetBlockWorld());
		HookServerPawn(*MyPawn);
	}
	if (IsLocalController() && Old != WorldActor)
	{
		SwitchLocalView(Old);
	}
}

void AMcPlayerController::ResolveWantedDimension()
{
	if (WantedDimension.IsEmpty() || HasAuthority())
	{
		return;
	}
	for (TActorIterator<AMcWorldActor> It(GetWorld()); It; ++It)
	{
		if (It->Dimension == WantedDimension && It->GetBlockWorld())
		{
			AMcWorldActor* Old = WorldActor;
			WorldActor = *It;
			WantedDimension.Reset();
			ChunkStream->SetBlockWorld(WorldActor->GetBlockWorld());
			if (AMcPlayerPawn* MyPawn = Cast<AMcPlayerPawn>(GetPawn()))
			{
				MyPawn->SetBlockWorld(WorldActor->GetBlockWorld());
			}
			SwitchLocalView(Old);
			return;
		}
	}
}

void AMcPlayerController::SyncDimensionView()
{
	for (TActorIterator<AMcWorldActor> It(GetWorld()); It; ++It)
	{
		It->SetViewed(*It == WorldActor);
	}
	// What the server made for another dimension is not drawn here (it has that dimension's world actor as its owner).
	for (TActorIterator<AMcEntityActor> It(GetWorld()); It; ++It)
	{
		const AMcWorldActor* Home = Cast<AMcWorldActor>(It->GetOwner());
		const bool bHide = Home && Home != WorldActor;
		if (It->IsHidden() != bHide)
		{
			It->SetActorHiddenInGame(bHide);
		}
	}
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
	APostProcessVolume* Post = bEnvironmentSpawned ? nullptr : World->SpawnActor<APostProcessVolume>();
	bEnvironmentSpawned = true;
	if (Post)
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
	UMcSkyComponent* Heavens = NewObject<UMcSkyComponent>(WorldActor);
	Heavens->SetupAttachment(WorldActor->GetRootComponent());
	Heavens->GetTime = [Actor = TWeakObjectPtr<AMcWorldActor>(WorldActor)]()
	{
		return Actor.IsValid() ? Actor->GetDayTime() : 1000.0;
	};
	if (const FMcBlockWorld* Blocks = WorldActor->GetBlockWorld())
	{
		Heavens->SetDimension(Blocks->GetDimension());
	}
	Heavens->RegisterComponent();
	Sky = Heavens;
#endif
}
