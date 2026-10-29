#include "McPlayerController.h"
#include "McLaunch.h"
#include "McBlockUse.h"
#include "McBuckets.h"
#include "McEntityActor.h"
#include "McEntityHost.h"
#include "McGameMode.h"
#include "McPlayerData.h"
#include "McPortal.h"
#include "McRails.h"
#include "McVehicle.h"
#include "McBiomeRegistry.h"
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
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if MC_WITH_RENDER
#include "Engine/GameViewportClient.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/GameInstance.h"
#include "McAudioSubsystem.h"
#include "McSkyComponent.h"
#include "McWeatherComponent.h"
#include "McCloudComponent.h"
#include "McLightningComponent.h"
#include "McWorldRenderComponent.h"
#include "McBlockLightComponent.h"
#include "McParticleComponent.h"
#include "McPhysics.h"
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
	if (!bMenuShotStarted && IsLocalController() && McLaunch::IsMenu(GetWorld()))
	{
		// The title screen has no world to set the view up in; a -McShot of it starts here instead.
		bMenuShotStarted = true;
		StartDevScreenshot();
	}
	if (IsLocalController() && WorldActor)
	{
		ViewSyncTimer -= DeltaSeconds;
		if (ViewSyncTimer <= 0.0f)
		{
			ViewSyncTimer = 0.2f;
			SyncDimensionView();
		}
		TickMusic(DeltaSeconds);
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

void AMcPlayerController::PawnLeavingGame()
{
	if (HasAuthority())
	{
		if (const AMcGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMcGameMode>() : nullptr)
		{
			GameMode->SavePlayer(this);
		}
	}
	Super::PawnLeavingGame();
}

void AMcPlayerController::TickMusic(float DeltaSeconds)
{
#if MC_WITH_RENDER
	UMcAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMcAudioSubsystem>() : nullptr;
	const FMcBlockWorld* BlockWorld = WorldActor->GetBlockWorld();
	if (!Audio || !BlockWorld || !PlayerCameraManager)
	{
		return;
	}
	// The picker counts game ticks.
	MusicTickTime += DeltaSeconds;
	if (MusicTickTime < 0.05f)
	{
		return;
	}
	MusicTickTime = FMath::Min(MusicTickTime - 0.05f, 0.05f);

	// The biome's own music where the camera is, else the dimension's.
	const AMcPlayerPawn* Listener = Cast<AMcPlayerPawn>(GetPawn());
	const bool bCreative = Listener && Listener->Data && Listener->Data->bCreative;
	const TMap<FName, FMcDimensionType::FMusic>* Lists = &BlockWorld->GetDimension().Music;
	const FVector Eye = McCoords::UnrealToBlock(PlayerCameraManager->GetCameraLocation());
	const FIntVector Block(FMath::FloorToInt32(Eye.X), FMath::FloorToInt32(Eye.Y), FMath::FloorToInt32(Eye.Z));
	if (const FMcChunk* Chunk = BlockWorld->FindChunk(FIntPoint(Block.X >> 4, Block.Z >> 4)))
	{
		const FMcBiomeId Biome = Chunk->GetBiome(Block.X & 15, FMath::Clamp(Block.Y, Chunk->GetMinY(), Chunk->GetMaxY()), Block.Z & 15);
		const TMap<FName, FMcDimensionType::FMusic>& BiomeLists = FMcBiomeRegistry::Get().GetMusicLists(Biome);
		if (!BiomeLists.IsEmpty())
		{
			Lists = &BiomeLists;
		}
	}
	const FMcDimensionType::FMusic* Wanted = FMcMusicPicker::Choose(*Lists, bCreative);
	switch (Music.Tick(Wanted, Audio->GetMusic(), MusicRandom.FRand()))
	{
	case FMcMusicPicker::EAction::Start: Audio->PlayMusic(Wanted->Sound); break;
	case FMcMusicPicker::EAction::Stop: Audio->StopMusic(); break;
	default: break;
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
	Hooked.FindVehicle = [World](const FVector& Eye, const FVector& Look, double Reach)
	{
		return World.IsValid() ? World->FindVehicle(Eye, Look, static_cast<float>(Reach)) : 0;
	};
	Hooked.MobAttack = [World](const FVector& Eye, const FVector& Look, double Reach, float Damage, bool bCreative)
	{
		return World.IsValid() && World->AttackMobs(Eye, Look, Reach, Damage, bCreative);
	};
	Hooked.OnBeforeBreak = [World](const FIntVector& Pos)
	{
		UMcBlockUseSubsystem* Use = World.IsValid() ? World->GetWorld()->GetSubsystem<UMcBlockUseSubsystem>() : nullptr;
		if (Use)
		{
			Use->ReleaseBlock(*World, Pos);
		}
	};
	Hooked.OnBreakDrops = [World](const FIntVector& Pos, TArray<FMcItemStack>& Drops)
	{
		UMcBlockUseSubsystem* Use = World.IsValid() ? World->GetWorld()->GetSubsystem<UMcBlockUseSubsystem>() : nullptr;
		if (Use)
		{
			Use->PackBoxDrops(*World, Pos, Drops);
		}
	};
	Hooked.OnPlacedStack = [World](const FIntVector& Pos, const FMcItemStack& Placed)
	{
		UMcBlockUseSubsystem* Use = World.IsValid() ? World->GetWorld()->GetSubsystem<UMcBlockUseSubsystem>() : nullptr;
		if (Use)
		{
			Use->FillBox(*World, Pos, Placed);
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
		const FString HeldName = Held.ToString();
		const bool bBoat = (HeldName.EndsWith(TEXT("_boat")) || HeldName.EndsWith(TEXT("_raft"))) && !HeldName.Contains(TEXT("chest"));
		if (Held == TEXT("minecraft:minecart") || bBoat)
		{
			FMcEntityHost* Host = World->GetEntityHost();
			if (!Host)
			{
				return false;
			}
			TUniquePtr<FMcVehicle> Vehicle;
			if (bBoat)
			{
				TUniquePtr<FMcBoatEntity> Boat = MakeUnique<FMcBoatEntity>();
				FString Wood = HeldName;
				Wood.RemoveFromStart(TEXT("minecraft:"));
				Wood.RemoveFromEnd(TEXT("_boat"));
				Wood.RemoveFromEnd(TEXT("_raft"));
				Boat->WoodName = FName(*(TEXT("minecraft:") + Wood));
				// On the water clicked (a little under its surface), or on the empty block beside the face.
				const FName ClickedName = Registry.GetBlockOf(Blocks->GetBlock(Pos)).Name;
				if (ClickedName == TEXT("minecraft:water"))
				{
					Boat->Body.Position = FVector(Pos.X + 0.5, Pos.Y + 0.8, Pos.Z + 0.5);
				}
				else if (Registry.IsAir(Blocks->GetBlock(Beside)))
				{
					Boat->Body.Position = FVector(Beside.X + 0.5, Beside.Y, Beside.Z + 0.5);
				}
				else
				{
					return false;
				}
				Vehicle = MoveTemp(Boat);
			}
			else
			{
				if (!McRails::IsRail(Blocks->GetBlock(Pos)))
				{
					return false;
				}
				TUniquePtr<FMcMinecartEntity> Cart = MakeUnique<FMcMinecartEntity>();
				Cart->Body.Position = FVector(Pos.X + 0.5, Pos.Y + FMcMinecartEntity::RailLift, Pos.Z + 0.5);
				Vehicle = MoveTemp(Cart);
			}
			Vehicle->Yaw = User.GetLookYaw();
			Host->GetManager().Add(MoveTemp(Vehicle));
			return true;
		}
		const FMcStateId Clicked = Blocks->GetBlock(Pos);
		if (McBuckets::Use(*World, User, Pos, Beside))
		{
			return true;
		}
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
		Data->ServerOnChat = [WeakController = TWeakObjectPtr<AMcPlayerController>(this)](const FString& Text)
		{
			AMcPlayerController* Speaker = WeakController.Get();
			if (AMcGameMode* Mode = Speaker ? Speaker->GetWorld()->GetAuthGameMode<AMcGameMode>() : nullptr)
			{
				Mode->HandleChat(Speaker, Text);
			}
		};
		Data->ServerDrop =[World, WeakPawn](const FMcItemStack& Stack)
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
	Drawer->GetRain = [Actor = TWeakObjectPtr<AMcWorldActor>(WorldActor)]()
	{
		return Actor.IsValid() ? Actor->GetRainLevel() : 0.0f;
	};
	Renderer = Drawer;

	UMcParticleComponent* Bits = NewObject<UMcParticleComponent>(WorldActor);
	Bits->SetupAttachment(WorldActor->GetRootComponent());
	Bits->RegisterComponent();
	Bits->SetWorld(WorldActor->GetBlockWorld(), Drawer);
	Bits->Collide = [Actor = TWeakObjectPtr<AMcWorldActor>(WorldActor)](const FBox& Box, const FVector& Motion)
	{
		const FMcBlockWorld* Blocks = Actor.IsValid() ? Actor->GetBlockWorld() : nullptr;
		return Blocks ? McPhysics::Collide(*Blocks, Box, Motion, 0.0f, false) : Motion;
	};
	Particles = Bits;

	UMcBlockLightComponent* Lamps = NewObject<UMcBlockLightComponent>(WorldActor);
	Lamps->SetupAttachment(WorldActor->GetRootComponent());
	Lamps->RegisterComponent();
	Lamps->SetWorld(WorldActor->GetBlockWorld());
	BlockLights = Lamps;
	WorldActor->OnBlockBroken = [WeakBits = TWeakObjectPtr<UMcParticleComponent>(Bits)](const FIntVector& Pos, FMcStateId State)
	{
		if (UMcParticleComponent* Live = WeakBits.Get())
		{
			Live->BlockBroken(Pos, State);
		}
	};
	SpawnEnvironment();
	if (UMcAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMcAudioSubsystem>() : nullptr)
	{
		Audio->SetListenerWorld(WorldActor ? WorldActor->GetBlockWorld() : nullptr);
	}
#endif
}

void AMcPlayerController::DetachView()
{
#if MC_WITH_RENDER
	if (UMcAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMcAudioSubsystem>() : nullptr)
	{
		Audio->SetListenerWorld(nullptr);
	}
#endif
	if (UActorComponent* Drawer = Renderer.Get())
	{
		Drawer->DestroyComponent();
	}
	if (UActorComponent* Heavens = Sky.Get())
	{
		Heavens->DestroyComponent();
	}
	if (UActorComponent* Bits = Particles.Get())
	{
		Bits->DestroyComponent();
	}
	if (UActorComponent* Lamps = BlockLights.Get())
	{
		Lamps->DestroyComponent();
	}
	if (UActorComponent* Precipitation = Weather.Get())
	{
		Precipitation->DestroyComponent();
	}
	if (UActorComponent* Cover = Clouds.Get())
	{
		Cover->DestroyComponent();
	}
	Renderer.Reset();
	Sky.Reset();
	Weather.Reset();
	Clouds.Reset();
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
	// -McUse=x,y,z: this player right-clicks that block after -McUseAfter seconds (default 4), as a hand would, to check
	// levers, buttons, doors and gates end to end.
	FString UseSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McUse="), UseSpec, /*bShouldStopOnSeparator=*/ false))
	{
		TArray<FString> UseParts;
		UseSpec.ParseIntoArray(UseParts, TEXT(","));
		float UseDelay = 4.0f;
		FParse::Value(FCommandLine::Get(), TEXT("-McUseAfter="), UseDelay);
		if (UseParts.Num() == 3)
		{
			const FIntVector UsePos(FCString::Atoi(*UseParts[0]), FCString::Atoi(*UseParts[1]), FCString::Atoi(*UseParts[2]));
			FTimerHandle UseTimer;
			GetWorldTimerManager().SetTimer(UseTimer, FTimerDelegate::CreateWeakLambda(this, [this, UsePos]()
			{
				if (AMcPlayerPawn* User = Cast<AMcPlayerPawn>(GetPawn()))
				{
					User->RequestUseBlock(UsePos);
					UE_LOG(LogMcClient, Display, TEXT("Used the block at %s"), *UsePos.ToString());
				}
			}), UseDelay, false);
		}
	}
	// -McPlaceHeld=x,y,z,seconds: puts the held block at that spot after that long, as a placing hand would.
	FString PlaceSpec;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McPlaceHeld="), PlaceSpec, /*bShouldStopOnSeparator=*/ false))
	{
		TArray<FString> PlaceParts;
		PlaceSpec.ParseIntoArray(PlaceParts, TEXT(","));
		if (PlaceParts.Num() == 4)
		{
			const FIntVector PlacePos(FCString::Atoi(*PlaceParts[0]), FCString::Atoi(*PlaceParts[1]), FCString::Atoi(*PlaceParts[2]));
			FTimerHandle PlaceTimer;
			GetWorldTimerManager().SetTimer(PlaceTimer, FTimerDelegate::CreateWeakLambda(this, [this, PlacePos]()
			{
				if (AMcPlayerPawn* Placer = Cast<AMcPlayerPawn>(GetPawn()))
				{
					Placer->PlaceHeldAt(PlacePos);
				}
			}), FCString::Atof(*PlaceParts[3]), false);
		}
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

	// -McRecord=<dir> -McPath=x,y,z,yaw,pitch/x,y,z,yaw,pitch/... -McRecordSeconds=<n> (-McRecordDelay=<s> of warm-up):
	// flies the camera along the keyframes (a smooth curve through them) and saves one frame per tick as
	// <dir>/f00000.png..., then quits. Run with -benchmark -fps=30 for an even 30 frames a second.
	FString RecordDir;
	FString PathText;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McRecord="), RecordDir) && FParse::Value(FCommandLine::Get(), TEXT("-McPath="), PathText, /*bShouldStopOnSeparator=*/ false))
	{
		struct FKey
		{
			FVector Pos;
			float Yaw;
			float Pitch;
		};
		TArray<FKey> Keys;
		TArray<FString> KeyTexts;
		PathText.ParseIntoArray(KeyTexts, TEXT("/"));
		for (const FString& Text : KeyTexts)
		{
			TArray<FString> P;
			Text.ParseIntoArray(P, TEXT(","));
			if (P.Num() == 5)
			{
				float Yaw = FCString::Atof(*P[3]);
				if (Keys.Num() > 0)
				{
					// The short way round.
					Yaw = Keys.Last().Yaw + FMath::UnwindDegrees(Yaw - Keys.Last().Yaw);
				}
				Keys.Add({FVector(FCString::Atod(*P[0]), FCString::Atod(*P[1]), FCString::Atod(*P[2])), Yaw, FCString::Atof(*P[4])});
			}
		}
		float Seconds = 20.0f;
		float Warmup = 10.0f;
		FParse::Value(FCommandLine::Get(), TEXT("-McRecordSeconds="), Seconds);
		FParse::Value(FCommandLine::Get(), TEXT("-McRecordDelay="), Warmup);
		if (Keys.Num() >= 2)
		{
			IFileManager::Get().MakeDirectory(*RecordDir, true);
			// At least two frames, the path's two ends (the curve divides by Total - 1).
			const int32 Total = FMath::Max(2, FMath::RoundToInt(Seconds * 30.0f));
			const double Start = FPlatformTime::Seconds();
			const TSharedRef<int32> Frame = MakeShared<int32>(0);
			TWeakObjectPtr<AMcPlayerController> Weak(this);
			FTSTicker::GetCoreTicker().AddTicker(TEXT("McRecord"), 0.0f, [Weak, Keys, RecordDir, Total, Start, Warmup, Frame](float)
			{
				AMcPlayerController* Self = Weak.Get();
				if (!Self)
				{
					return false;
				}
				AMcPlayerPawn* MyPawn = Cast<AMcPlayerPawn>(Self->GetPawn());
				if (!MyPawn || FPlatformTime::Seconds() - Start < Warmup)
				{
					return true;
				}
				if (*Frame >= Total)
				{
					FPlatformMisc::RequestExit(false, TEXT("McRecord"));
					return false;
				}
				// Catmull-Rom through the keyframes, evenly spaced in time.
				const float U = (float)*Frame / (float)(Total - 1) * (Keys.Num() - 1);
				const int32 I = FMath::Min((int32)U, Keys.Num() - 2);
				const float T = FMath::SmoothStep(0.0f, 1.0f, U - I) * 0.5f + (U - I) * 0.5f;
				const FKey& K0 = Keys[FMath::Max(I - 1, 0)];
				const FKey& K1 = Keys[I];
				const FKey& K2 = Keys[I + 1];
				const FKey& K3 = Keys[FMath::Min(I + 2, Keys.Num() - 1)];
				auto CR = [T](double A, double B, double C, double D)
				{
					return 0.5 * ((2.0 * B) + (-A + C) * T + (2.0 * A - 5.0 * B + 4.0 * C - D) * T * T + (-A + 3.0 * B - 3.0 * C + D) * T * T * T);
				};
				const FVector Pos(CR(K0.Pos.X, K1.Pos.X, K2.Pos.X, K3.Pos.X), CR(K0.Pos.Y, K1.Pos.Y, K2.Pos.Y, K3.Pos.Y), CR(K0.Pos.Z, K1.Pos.Z, K2.Pos.Z, K3.Pos.Z));
				MyPawn->SetView(Pos, FMath::Lerp(K1.Yaw, K2.Yaw, T), FMath::Lerp(K1.Pitch, K2.Pitch, T));
				FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("%s/f%05d.png"), *RecordDir, *Frame), false, false);
				++*Frame;
				return true;
			});
		}
		else
		{
			UE_LOG(LogMcClient, Error, TEXT("-McPath wants at least two x,y,z,yaw,pitch keyframes separated by /"));
		}
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

	// The tonemapper is the sky's to switch with the look (UMcSkyComponent, McRenderMode).
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
	Heavens->GetWeather = [Actor = TWeakObjectPtr<AMcWorldActor>(WorldActor)]()
	{
		return Actor.IsValid() ? FVector2D(Actor->GetRainLevel(), Actor->GetThunderLevel()) : FVector2D::ZeroVector;
	};
	if (const FMcBlockWorld* Blocks = WorldActor->GetBlockWorld())
	{
		Heavens->SetDimension(Blocks->GetDimension());
	}
	Heavens->RegisterComponent();
	Sky = Heavens;

	// Rain and snow round the camera, and the rain's sound.
	UMcWeatherComponent* Precipitation = NewObject<UMcWeatherComponent>(WorldActor);
	Precipitation->SetupAttachment(WorldActor->GetRootComponent());
	Precipitation->SetBlockWorld(WorldActor->GetBlockWorld());
	Precipitation->GetRain = [Actor = TWeakObjectPtr<AMcWorldActor>(WorldActor)]()
	{
		return Actor.IsValid() ? Actor->GetRainLevel() : 0.0f;
	};
	Precipitation->RegisterComponent();
	Weather = Precipitation;

	// The clouds, drifting with the world's time.
	UMcCloudComponent* Cover = NewObject<UMcCloudComponent>(WorldActor);
	Cover->SetupAttachment(WorldActor->GetRootComponent());
	Cover->GetTime = Heavens->GetTime;
	Cover->GetWeather = Heavens->GetWeather;
	if (const FMcBlockWorld* Blocks = WorldActor->GetBlockWorld())
	{
		Cover->SetDimension(Blocks->GetDimension());
	}
	Cover->RegisterComponent();
	Clouds = Cover;

	// A bolt: drawn where it struck, and the sky flashes.
	WorldActor->OnLightning = [Actor = TWeakObjectPtr<AMcWorldActor>(WorldActor), WeakSky = TWeakObjectPtr<UMcSkyComponent>(Heavens)](const FVector& Position, int32 Seed)
	{
		if (!Actor.IsValid())
		{
			return;
		}
		UMcLightningComponent* Bolt = NewObject<UMcLightningComponent>(Actor.Get());
		Bolt->SetupAttachment(Actor->GetRootComponent());
		Bolt->SetBolt(Position, Seed);
		Bolt->RegisterComponent();
		if (UMcSkyComponent* Lit = WeakSky.Get())
		{
			Lit->Flash();
		}
	};
#endif
}
