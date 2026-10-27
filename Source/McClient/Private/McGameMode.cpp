#include "McGameMode.h"
#include "McChunk.h"
#include "McEntityActor.h"
#include "McClient.h"
#include "McBlockShapes.h"
#include "McEntityHost.h"
#include "McPlayerController.h"
#include "McPlayerData.h"
#include "McPlayerSave.h"
#include "McTravel.h"
#include "McPlayerPawn.h"
#include "McWorldActor.h"
#include "McWorldSave.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformMisc.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"

#if MC_WITH_RENDER
#include "McHUD.h"
#endif

AMcGameMode::AMcGameMode()
{
	DefaultPawnClass = AMcPlayerPawn::StaticClass();
	PlayerControllerClass = AMcPlayerController::StaticClass();
#if MC_WITH_RENDER
	HUDClass = AMcHUD::StaticClass();
#endif
}

void AMcGameMode::StartPlay()
{
	Super::StartPlay();

	// Spawned after Super::StartPlay so the world has begun play and BeginPlay runs straight away.
	WorldActor = GetWorld()->SpawnActor<AMcWorldActor>();
	if (WorldActor)
	{
		WorldActor->SetEntityProxyClass(AMcEntityActor::StaticClass());
	}
	if (!WorldActor || !WorldActor->GetBlockWorld())
	{
		UE_LOG(LogMcClient, Error, TEXT("No world to play in; see the McServer and McCore errors above."));
#if !UE_BUILD_SHIPPING
		// A development run (a -McShot screenshot, a test) that can't start has nothing to wait for.
		FString ShotPath;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McShot="), ShotPath))
		{
			FPlatformMisc::RequestExit(true, TEXT("McNoWorld"));
		}
#endif
		return;
	}

	FMcBlockShapes::Get().EnsureLoaded();

	SetUpWorld(*WorldActor);
	if (WorldActor->HasStorage())
	{
		SavingHandle = WorldActor->OnSaving.AddUObject(this, &AMcGameMode::SavePlayers);
	}

	// Players that joined before the world existed started at a guessed height; move them above the surface (or to
	// where the saved world left them).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			bool bFlying = false;
			Pawn->SetActorLocation(GetStartTransform(It->Get(), bFlying).GetLocation());
			if (AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(Pawn))
			{
				McPawn->SetFlying(bFlying);
				RestoreSurvival(It->Get(), *McPawn);
			}
		}
	}

	StartDevCapture();
}

void AMcGameMode::SetUpWorld(AMcWorldActor& World)
{
	Worlds.AddUnique(&World);
	World.SetEntityProxyClass(AMcEntityActor::StaticClass());
	if (FMcEntityHost* Host = World.GetEntityHost())
	{
		Host->OnPlayerHit = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Position, float Damage)
		{
			if (const AMcWorldActor* Here = Weak.Get())
			{
				HurtPlayerNear(*Here, Position, Damage);
			}
		};
	}
	// With more than one world each looks after the players who are in it.
	if (Worlds.Num() > 1)
	{
		for (AMcWorldActor* Each : Worlds)
		{
			Each->PlayerFilter = [Weak = TWeakObjectPtr<AMcWorldActor>(Each)](const AController& Controller)
			{
				const AMcPlayerController* Player = Cast<AMcPlayerController>(&Controller);
				const AMcWorldActor* Home = Player ? Player->GetWorldActor() : nullptr;
				const AMcWorldActor* Self = Weak.Get();
				return !Player || !Self || Home == Self || (!Home && !Self->bSecondary);
			};
		}
	}
}

AMcWorldActor* AMcGameMode::FindWorld(const FString& Dimension) const
{
	for (AMcWorldActor* Each : Worlds)
	{
		if (Each && Each->Dimension == Dimension)
		{
			return Each;
		}
	}
	return nullptr;
}

AMcWorldActor* AMcGameMode::GetOrCreateWorld(const FString& Dimension)
{
	if (AMcWorldActor* Existing = FindWorld(Dimension))
	{
		return Existing;
	}
	if (!WorldActor || !WorldActor->GetBlockWorld())
	{
		return nullptr;
	}
	AMcWorldActor* Made = GetWorld()->SpawnActorDeferred<AMcWorldActor>(AMcWorldActor::StaticClass(), FTransform::Identity);
	if (!Made)
	{
		return nullptr;
	}
	Made->Dimension = Dimension;
	Made->NoiseSettings = Dimension == TEXT("minecraft:the_end") ? TEXT("minecraft:end") : Dimension == TEXT("minecraft:the_nether") ? TEXT("minecraft:nether") : TEXT("minecraft:overworld");
	Made->InheritFrom(*WorldActor);
	Made->FinishSpawning(FTransform::Identity);
	if (!Made->GetBlockWorld())
	{
		UE_LOG(LogMcClient, Error, TEXT("Can't make the world for %s; see the McServer errors above."), *Dimension);
		Made->Destroy();
		return nullptr;
	}
	SetUpWorld(*Made);
	if (Made->HasStorage())
	{
		Made->OnSaving.AddWeakLambda(this, [this]() { SavePlayers(); });
	}
	UE_LOG(LogMcClient, Display, TEXT("Made the world for %s"), *Dimension);
	return Made;
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
	// -McPillarAfter=<seconds>: builds a gold pillar at 3, 0 (in front of a new player) then, to check block changes reach clients.
	// -McEditY=<y> is the pillar's lowest block (default -60, the flat world's first block above the grass).
	float PillarDelay = 0.0f;
	int32 EditY = -60;
	FParse::Value(CommandLine, TEXT("-McEditY="), EditY);
	if (FParse::Value(CommandLine, TEXT("-McPillarAfter="), PillarDelay))
	{
		FTimerHandle PillarTimer;
		GetWorldTimerManager().SetTimer(PillarTimer, FTimerDelegate::CreateWeakLambda(this, [this, EditY]()
		{
			const FMcStateId Gold = FMcBlockRegistry::Get().FindDefaultState(TEXT("minecraft:gold_block")).Get(0);
			for (int32 Y = EditY; Y <= EditY + 6; ++Y)
			{
				WorldActor->SetBlock(FIntVector(3, Y, 0), Gold);
			}
			UE_LOG(LogMcClient, Display, TEXT("Built the gold pillar"));
		}), PillarDelay, false);
	}

	// -McEdit=x,y,z,<state> (or x1,y1,z1,x2,y2,z2,<state> for a box) places blocks two seconds in, to check that edits
	// survive saving a world (-McWorld): place them on a first run, and look for them on the second.
	FString EditSpec;
	if (FParse::Value(CommandLine, TEXT("-McEdit="), EditSpec, /*bShouldStopOnSeparator=*/ false))
	{
		FTimerHandle EditTimer;
		GetWorldTimerManager().SetTimer(EditTimer, FTimerDelegate::CreateWeakLambda(this, [this, EditSpec]()
		{
			TArray<FString> Parts;
			EditSpec.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() != 4 && Parts.Num() != 7)
			{
				UE_LOG(LogMcClient, Error, TEXT("-McEdit wants x,y,z,state or x1,y1,z1,x2,y2,z2,state; got %s"), *EditSpec);
				return;
			}
			const TOptional<FMcStateId> State = FMcBlockRegistry::Get().ParseState(Parts.Last());
			if (!State)
			{
				UE_LOG(LogMcClient, Error, TEXT("-McEdit: unknown block %s"), *Parts.Last());
				return;
			}
			const FIntVector A(FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2]));
			const FIntVector B = Parts.Num() == 7 ? FIntVector(FCString::Atoi(*Parts[3]), FCString::Atoi(*Parts[4]), FCString::Atoi(*Parts[5])) : A;
			int32 Placed = 0;
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
			UE_LOG(LogMcClient, Display, TEXT("McEdit: placed %d blocks of %s"), Placed, *Parts.Last());
		}), 2.0f, false);
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
				if (AMcPlayerPawn* Pawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr)
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

#endif
}

void AMcGameMode::RestartPlayer(AController* NewPlayer)
{
	// There are no PlayerStart actors: players start above the middle of block 0, 0.
	if (NewPlayer && !NewPlayer->IsPendingKillPending())
	{
		bool bFlying = false;
		RestartPlayerAtTransform(NewPlayer, GetStartTransform(NewPlayer, bFlying));
		if (AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(NewPlayer->GetPawn()))
		{
			McPawn->SetFlying(bFlying);
			RestoreSurvival(NewPlayer, *McPawn);
		}
		// A player last saved in another dimension goes back there, once it is ready.
		FMcPlayerData Saved;
		AMcPlayerController* Player = Cast<AMcPlayerController>(NewPlayer);
		if (Player && WorldActor && LoadSavedPlayer(NewPlayer, Saved) && !Saved.Dimension.IsEmpty() && Saved.Dimension != WorldActor->Dimension)
		{
			if (UMcTravelSubsystem* Travel = GetWorld()->GetSubsystem<UMcTravelSubsystem>())
			{
				Travel->StartExactTrip(*Player, Saved.Dimension, Saved.Position, Saved.Yaw);
			}
		}
	}
}

void AMcGameMode::RespawnPlayer(AController* Controller)
{
	if (!Controller)
	{
		return;
	}
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Weak = TWeakObjectPtr<AController>(Controller)]()
	{
		AController* Who = Weak.Get();
		if (!Who)
		{
			return;
		}
		if (APawn* Old = Who->GetPawn())
		{
			Who->UnPossess();
			Old->Destroy();
		}
		// The spawn is in the first world: a player who died in the Nether comes back there.
		AMcPlayerController* Player = Cast<AMcPlayerController>(Who);
		if (Player && WorldActor && Player->GetWorldActor() != WorldActor)
		{
			Player->EnterWorld(*WorldActor);
		}
		RestartPlayerAtTransform(Who, GetSpawnTransform());
	}));
}

void AMcGameMode::HurtPlayerNear(const AMcWorldActor& World, const FVector& Position, float Damage)
{
	AMcPlayerPawn* Nearest = nullptr;
	double NearestDistance = 2.5;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		// Only the players in the dimension the mob is in.
		const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
		if (Player && Player->GetWorldActor() && Player->GetWorldActor() != &World)
		{
			continue;
		}
		AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		const double Distance = McPawn ? FVector::Dist(McPawn->GetFeetPosition(), Position) : NearestDistance;
		if (McPawn && Distance < NearestDistance)
		{
			Nearest = McPawn;
			NearestDistance = Distance;
		}
	}
	if (Nearest)
	{
		// The mob that bit is at Position: the player is knocked away from it.
		Nearest->ServerHurt(Damage, McSurvival::EDamage::Mob, &Position);
	}
}

void AMcGameMode::RestoreSurvival(const AController* Controller, AMcPlayerPawn& Pawn) const
{
	FMcPlayerData Saved;
	if (Pawn.Data && WorldActor && WorldActor->HasStorage() && McWorldSave::LoadPlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), Saved)
		&& McPlayerSave::Apply(Saved, *Pawn.Data))
	{
		Pawn.SelectedSlot = Pawn.Data->SelectedSlot;
	}
}

FTransform AMcGameMode::GetStartTransform(const AController* Controller, bool& bOutFlying) const
{
	bOutFlying = false;
	FMcPlayerData Saved;
	const FMcBlockWorld* Blocks = WorldActor ? WorldActor->GetBlockWorld() : nullptr;
	if (Blocks && WorldActor->HasStorage() && McWorldSave::LoadPlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), Saved))
	{
		const FMcDimensionType& Dimension = Blocks->GetDimension();
		// A player saved in another dimension, or at a height this one doesn't have (the file was edited, or the world
		// changed), starts at the spawn rather than outside the world.
		if (Saved.Dimension == WorldActor->Dimension && Saved.Position.Y >= Dimension.MinY && Saved.Position.Y <= Dimension.MinY + Dimension.Height + 64)
		{
			bOutFlying = Saved.bFlying;
			// The pawn takes its look from the transform; see AMcPlayerPawn::SetView for the offsets.
			return FTransform(FRotator(-Saved.Pitch, Saved.Yaw + 90.0f, 0.0f), McCoords::BlockToUnreal(Saved.Position));
		}
	}
	return GetSpawnTransform();
}

bool AMcGameMode::LoadSavedPlayer(const AController* Controller, FMcPlayerData& OutSaved) const
{
	return WorldActor && WorldActor->HasStorage() && McWorldSave::LoadPlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), OutSaved);
}

FString AMcGameMode::GetSaveId(const AController* Controller) const
{
	if (const APlayerState* State = Controller ? Controller->PlayerState.Get() : nullptr)
	{
		// By name for now; a platform account id replaces it when sign-in exists.
		if (!State->GetPlayerName().IsEmpty())
		{
			return State->GetPlayerName();
		}
	}
	return TEXT("player");
}

void AMcGameMode::SavePlayer(const AController* Controller) const
{
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn || !WorldActor || !WorldActor->HasStorage())
	{
		return;
	}
	FMcPlayerData Data;
	Data.Position = McCoords::UnrealToBlock(Pawn->GetActorLocation());
	const AMcPlayerController* McController = Cast<AMcPlayerController>(Controller);
	Data.Dimension = McController && McController->GetWorldActor() ? McController->GetWorldActor()->Dimension : WorldActor->Dimension;
	if (const AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(Pawn))
	{
		Data.Yaw = McPawn->GetLookYaw();
		Data.Pitch = McPawn->GetLookPitch();
		Data.bFlying = McPawn->IsFlying();
		if (McPawn->Data)
		{
			McPlayerSave::Capture(*McPawn->Data, Data);
		}
	}
	McWorldSave::SavePlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), Data);
}

void AMcGameMode::SavePlayers() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		SavePlayer(It->Get());
	}
}

void AMcGameMode::Logout(AController* Exiting)
{
	SavePlayer(Exiting);
	Super::Logout(Exiting);
}

void AMcGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The world actor saves the players too when it ends, but which of the two ends first isn't defined, and by the time
	// the second does the pawns may be gone.
	if (IsValid(WorldActor))
	{
		SavePlayers();
		WorldActor->OnSaving.Remove(SavingHandle);
	}
	SavingHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

FTransform AMcGameMode::GetSpawnTransform() const
{
	// Before the world exists, guess the classic flat surface (y = -61).
	const int32 SurfaceY = WorldActor && WorldActor->GetBlockWorld() ? WorldActor->GetSurfaceY() : -61;
	const FVector Block(0.5, SurfaceY + 1 + SpawnHeightAboveSurface, 0.5);
	return FTransform(FRotator::ZeroRotator, McCoords::BlockToUnreal(Block));
}

