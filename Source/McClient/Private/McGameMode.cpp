#include "McGameMode.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "McBlockEntity.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEntityActor.h"
#include "McClient.h"
#include "McBlockShapes.h"
#include "McEntityHost.h"
#include "McVehicle.h"
#include "McPlayerController.h"
#include "McPlayerData.h"
#include "McPlayerSave.h"
#include "McTravel.h"
#include "McPlayerPawn.h"
#include "McWorldActor.h"
#include "McWorldSave.h"
#include "McLaunch.h"
#include "McCommands.h"
#include "McItemRegistry.h"
#include "McLang.h"
#include "McSurvival.h"
#include "McEffects.h"
#include "McSimulation.h"
#include "McConduitHunt.h"
#include "McEntityManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameSession.h"
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
#include "UObject/UObjectGlobals.h"
#include "Engine/Engine.h"
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
	if (McLaunch::IsMenu(GetWorld()))
	{
		// The title screen (AMcHUD) has no world behind it; it opens one by loading the map again with ?McWorld.
		return;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		const FMcServerProperties& Properties = FMcServerProperties::Get();
		if (GameSession)
		{
			// The player limit is max-players, checked in PreLogin so operators can be let past it.
			GameSession->MaxPlayers = 10000;
		}
		// Makes the allowlist and ops files if they aren't there yet, so there is something to edit.
		FMcServerAccess::Load();
		UE_LOG(LogMcClient, Log, TEXT("Dedicated server \"%s\" on port %d: up to %d players, allowlist %s."), *Properties.GetString(TEXT("motd")),
			GetWorld()->URL.Port, Properties.GetInt(TEXT("max-players"), 20), Properties.GetBool(TEXT("white-list")) ? TEXT("on") : TEXT("off"));
	}

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
		// Leaving for another map (Save and Quit to Title) takes the pawns away before anything here ends play, so the
		// players are saved as the engine starts loading it.
		PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddWeakLambda(this, [this](const FWorldContext& Context, const FString&)
		{
			if (Context.World() == GetWorld())
			{
				SavePlayers();
			}
		});
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
				McPawn->SetGameRules(GetGameRules());
				McPawn->SetFlying(bFlying);
				if (!RestoreSurvival(It->Get(), *McPawn))
				{
					ApplyDefaultGameMode(*McPawn);
				}
			}
		}
	}

	StartDevCapture();
}

void AMcGameMode::SetUpWorld(AMcWorldActor& World)
{
	Worlds.AddUnique(&World);
	ApplyGameRules();
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
	if (FMcSimulation* Sim = World.GetSimulation())
	{
		// Each hunting conduit's target, by where it is.
		TSharedRef<TMap<FIntVector, int32>> Targets = MakeShared<TMap<FIntVector, int32>>();
		Sim->OnConduitPulse = [this, Targets, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FIntVector& Pos, int32 Range, bool bHunting)
		{
			AMcWorldActor* Here = Weak.Get();
			if (!Here)
			{
				return;
			}
			GiveConduitPower(*Here, FVector(Pos) + FVector(0.5), Range);
			FMcEntityHost* Host = Here->GetEntityHost();
			const FMcSimulation* HereSim = Here->GetSimulation();
			if (!bHunting || !Host || !HereSim)
			{
				Targets->Remove(Pos);
				return;
			}
			const int32 Target = McConduitHunt::Pulse(Host->GetManager(), Pos, Targets->FindRef(Pos), [HereSim](const FMcEntity& Mob)
			{
				const FVector& Feet = Mob.Body.Position;
				return Mob.Body.bInWater || HereSim->IsRainingAt(FIntVector(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z)));
			});
			if (Target != 0)
			{
				Targets->Add(Pos, Target);
			}
			else
			{
				Targets->Remove(Pos);
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
	auto ToBlock = [](const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		return FIntVector(FMath::RoundToInt32(Values[0]->AsNumber()), FMath::RoundToInt32(Values[1]->AsNumber()), FMath::RoundToInt32(Values[2]->AsNumber()));
	};
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

	// Signs' words: { "at": [x, y, z], "front": [up to four lines], "color": "red", "glowing": true, and "back",
	// "back_color", "back_glowing" for the other side, "waxed" }. Written once the sign's chunk is there.
	struct FSceneSign
	{
		FIntVector Pos;
		FMcBlockEntity Entity;
	};
	TSharedRef<TArray<FSceneSign>> Signs = MakeShared<TArray<FSceneSign>>();
	const TArray<TSharedPtr<FJsonValue>>* SignValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("signs"), SignValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SignValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3)
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a sign needs \"at\": [x, y, z]"), *Path);
				continue;
			}
			FSceneSign& Sign = Signs->Emplace_GetRef();
			Sign.Pos = ToBlock(*At);
			Sign.Entity = FMcBlockEntity::MakeSign();
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				FMcSignSide& Side = Sign.Entity.SignSides[SideIndex];
				const FString Prefix = SideIndex == 0 ? FString() : FString(TEXT("back_"));
				const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
				if (Object->TryGetArrayField(SideIndex == 0 ? TEXT("front") : TEXT("back"), Lines))
				{
					for (int32 Line = 0; Line < FMath::Min(Lines->Num(), FMcSignSide::NumLines); ++Line)
					{
						Side.Lines[Line] = (*Lines)[Line]->AsString();
					}
				}
				FString Color;
				if (Object->TryGetStringField(Prefix + TEXT("color"), Color))
				{
					Side.Color = FName(*Color);
				}
				Object->TryGetBoolField(Prefix + TEXT("glowing"), Side.bGlowing);
			}
			Object->TryGetBoolField(TEXT("waxed"), Sign.Entity.bWaxed);
		}
	}
	// Lecterns' books: { "at": [x, y, z], "pages": ["text", ...], "page": 0, "title": ..., "author": ... }, a written
	// book. Written the same way as the signs; the lectern (placed with has_book=false) then gets has_book=true, so
	// that a comparator reading it reads the book.
	const TArray<TSharedPtr<FJsonValue>>* LecternValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("lecterns"), LecternValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *LecternValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3)
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a lectern needs \"at\": [x, y, z]"), *Path);
				continue;
			}
			const TSharedRef<FJsonObject> Content = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Pages;
			const TArray<TSharedPtr<FJsonValue>>* PageValues = nullptr;
			if (Object->TryGetArrayField(TEXT("pages"), PageValues))
			{
				for (const TSharedPtr<FJsonValue>& Page : *PageValues)
				{
					const TSharedRef<FJsonObject> Raw = MakeShared<FJsonObject>();
					Raw->SetStringField(TEXT("raw"), Page->AsString());
					Pages.Add(MakeShared<FJsonValueObject>(Raw));
				}
			}
			Content->SetArrayField(TEXT("pages"), Pages);
			FString TitleText, Author;
			Object->TryGetStringField(TEXT("title"), TitleText);
			Object->TryGetStringField(TEXT("author"), Author);
			const TSharedRef<FJsonObject> Title = MakeShared<FJsonObject>();
			Title->SetStringField(TEXT("raw"), TitleText);
			Content->SetObjectField(TEXT("title"), Title);
			Content->SetStringField(TEXT("author"), Author);
			FMcItemStack Book(TEXT("minecraft:written_book"));
			Book.Set(TEXT("minecraft:written_book_content"), MakeShared<FJsonValueObject>(Content));
			FSceneSign& Lectern = Signs->Emplace_GetRef();
			Lectern.Pos = ToBlock(*At);
			Lectern.Entity = FMcBlockEntity::MakeLectern(Book);
			Object->TryGetNumberField(TEXT("page"), Lectern.Entity.Page);
		}
	}
	// Decorated pots' sides: { "at": [x, y, z], "sides": [back, left, right, front] }, items (bricks or sherds).
	const TArray<TSharedPtr<FJsonValue>>* PotValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("pots"), PotValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *PotValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* SideValues = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3 || !Object->TryGetArrayField(TEXT("sides"), SideValues))
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a pot needs \"at\": [x, y, z] and \"sides\""), *Path);
				continue;
			}
			TArray<FName> Sides;
			for (const TSharedPtr<FJsonValue>& Side : *SideValues)
			{
				Sides.Add(FName(*Side->AsString()));
			}
			FSceneSign& Pot = Signs->Emplace_GetRef();
			Pot.Pos = ToBlock(*At);
			Pot.Entity = FMcBlockEntity::MakePot(Sides);
		}
	}
	if (Signs->Num() > 0)
	{
		TSharedRef<FTimerHandle> SignTimer = MakeShared<FTimerHandle>();
		GetWorldTimerManager().SetTimer(*SignTimer, FTimerDelegate::CreateWeakLambda(this, [this, Signs, SignTimer]()
		{
			FMcBlockWorld* Blocks = WorldActor ? WorldActor->GetBlockWorld() : nullptr;
			for (int32 Index = Signs->Num() - 1; Blocks && Index >= 0; --Index)
			{
				const FSceneSign& Sign = (*Signs)[Index];
				if (FMcChunk* Chunk = Blocks->FindChunk(FIntPoint(Sign.Pos.X >> 4, Sign.Pos.Z >> 4)))
				{
					Chunk->SetBlockEntity(Sign.Pos.X & 15, Sign.Pos.Y, Sign.Pos.Z & 15, Sign.Entity);
					WorldActor->MarkChunkDirty(FIntPoint(Sign.Pos.X >> 4, Sign.Pos.Z >> 4));
					Blocks->OnBlockEntityChanged.Broadcast(Sign.Pos);
					if (Sign.Entity.Kind == EMcBlockEntityKind::Lectern)
					{
						const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
						const FString Facing = Registry.GetPropertyValue(Blocks->GetBlock(Sign.Pos), TEXT("facing")).ToString();
						WorldActor->SetBlock(Sign.Pos, Registry.ParseState(TEXT("minecraft:lectern[facing=") + Facing + TEXT(",has_book=true,powered=false]")).Get(0));
					}
					Signs->RemoveAt(Index);
				}
			}
			if (Signs->Num() == 0)
			{
				GetWorldTimerManager().ClearTimer(*SignTimer);
			}
		}), 0.25f, true);
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
	// The title screen has no world to be in.
	if (McLaunch::IsMenu(GetWorld()))
	{
		return;
	}
	// There are no PlayerStart actors: players start above the middle of block 0, 0.
	if (NewPlayer && !NewPlayer->IsPendingKillPending())
	{
		bool bFlying = false;
		RestartPlayerAtTransform(NewPlayer, GetStartTransform(NewPlayer, bFlying));
		if (AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(NewPlayer->GetPawn()))
		{
			McPawn->SetFlying(bFlying);
			McPawn->SetGameRules(GetGameRules());
			if (!RestoreSurvival(NewPlayer, *McPawn))
			{
				ApplyDefaultGameMode(*McPawn);
			}
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
		// With keepInventory what they carried, and their experience, goes with them to the new body.
		TArray<FMcItemStack> Kept[3];
		int32 KeptLevel = 0;
		float KeptProgress = 0.0f;
		int32 KeptSlot = 0;
		bool bKeep = false;
		if (const AMcPlayerPawn* Dead = Cast<AMcPlayerPawn>(Who->GetPawn()))
		{
			bKeep = Dead->Data && Dead->GetGameRules().bKeepInventory;
			if (bKeep)
			{
				const TSharedPtr<FMcContainer> Containers[3] = { Dead->Data->Inventory, Dead->Data->Armor, Dead->Data->Offhand };
				for (int32 Index = 0; Index < 3; ++Index)
				{
					for (int32 Slot = 0; Slot < Containers[Index]->Num(); ++Slot)
					{
						Kept[Index].Add(Containers[Index]->Get(Slot));
					}
				}
				KeptLevel = Dead->Data->Stats.XpLevel;
				KeptProgress = Dead->Data->Stats.XpProgress;
				KeptSlot = Dead->Data->SelectedSlot;
			}
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
		if (AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(Who->GetPawn()))
		{
			McPawn->SetGameRules(GetGameRules());
			ApplyDefaultGameMode(*McPawn);
			if (bKeep && McPawn->Data)
			{
				const TSharedPtr<FMcContainer> Containers[3] = { McPawn->Data->Inventory, McPawn->Data->Armor, McPawn->Data->Offhand };
				for (int32 Index = 0; Index < 3; ++Index)
				{
					for (int32 Slot = 0; Slot < Containers[Index]->Num() && Slot < Kept[Index].Num(); ++Slot)
					{
						Containers[Index]->Set(Slot, Kept[Index][Slot]);
					}
				}
				McPawn->Data->Stats.XpLevel = KeptLevel;
				McPawn->Data->Stats.XpProgress = KeptProgress;
				McPawn->Data->SelectedSlot = KeptSlot;
				McPawn->SelectedSlot = KeptSlot;
			}
		}
	}));
}

FMcGameRules AMcGameMode::GetGameRules() const
{
	if (const AMcWorldActor* Primary = GetPrimaryWorld())
	{
		return Primary->GetGameRules();
	}
	return FMcGameRules();
}

void AMcGameMode::ApplyGameRules()
{
	const FMcGameRules Rules = GetGameRules();
	for (const TObjectPtr<AMcWorldActor>& World : Worlds)
	{
		if (World)
		{
			World->ApplyGameRules(Rules);
		}
	}
	if (UWorld* Game = GetWorld())
	{
		for (FConstPlayerControllerIterator It = Game->GetPlayerControllerIterator(); It; ++It)
		{
			if (AMcPlayerPawn* Pawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr)
			{
				Pawn->SetGameRules(Rules);
			}
		}
	}
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

void AMcGameMode::GiveConduitPower(const AMcWorldActor& World, const FVector& Middle, int32 Range)
{
	const FMcSimulation* Sim = World.GetSimulation();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
		if (Player && Player->GetWorldActor() && Player->GetWorldActor() != &World)
		{
			continue;
		}
		AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		if (!McPawn || !McPawn->Data || FVector::DistSquared(McPawn->GetFeetPosition(), Middle) > FMath::Square(double(Range)))
		{
			continue;
		}
		// Only players in water or out in the rain (minecraft.wiki).
		const FVector Feet = McPawn->GetFeetPosition();
		const FIntVector Block(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z));
		if (McPawn->GetBody().bInWater || (Sim && Sim->IsRainingAt(Block)))
		{
			McEffects::Add(McPawn->Data->Stats, TEXT("conduit_power"), 0, FMcSimulation::ConduitPowerTicks);
		}
	}
}

bool AMcGameMode::RestoreSurvival(const AController* Controller, AMcPlayerPawn& Pawn) const
{
	FMcPlayerData Saved;
	if (Pawn.Data && WorldActor && WorldActor->HasStorage() && McWorldSave::LoadPlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), Saved)
		&& McPlayerSave::Apply(Saved, *Pawn.Data))
	{
		Pawn.SelectedSlot = Pawn.Data->SelectedSlot;
		return true;
	}
	return false;
}

void AMcGameMode::ApplyDefaultGameMode(AMcPlayerPawn& Pawn) const
{
	if (!Pawn.Data || Pawn.Data->bCreative)
	{
		// Already creative (-McCreative).
		return;
	}
	// A dedicated server's gamemode setting is for everyone new, whatever the world was made with.
	const FString Mode = GetNetMode() == NM_DedicatedServer ? FMcServerProperties::Get().GetString(TEXT("gamemode"))
		: WorldActor ? WorldActor->GetLevel().GameMode : FString();
	if (McLaunch::IsCreativeMode(Mode))
	{
		Pawn.Data->bCreative = true;
		Pawn.Data->FillCreativeHotbar();
	}
}

void AMcGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty() || GetNetMode() != NM_DedicatedServer)
	{
		return;
	}
	// Read afresh, so an edited allowlist or ops file applies to the next player to join.
	const FString Name = UGameplayStatics::ParseOption(Options, TEXT("Name"));
	ErrorMessage = FMcServerAccess::CheckJoin(FMcServerProperties::Get(), FMcServerAccess::Load(), Name, GetNumPlayers());
	if (!ErrorMessage.IsEmpty())
	{
		UE_LOG(LogMcClient, Log, TEXT("%s (%s) can't join: %s"), *Name, *Address, *ErrorMessage);
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
	const FString Name = GetPlayerName(Exiting);
	Super::Logout(Exiting);
	if (!Name.IsEmpty() && GetNetMode() != NM_Standalone)
	{
		BroadcastChat(McLang::Format(TEXT("multiplayer.player.left"), { Name }), 0xFFFF55);
	}
}

void AMcGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	// A world played alone says nothing; anywhere others can join, everyone is told (in yellow, as in the game).
	if (GetNetMode() != NM_Standalone)
	{
		BroadcastChat(McLang::Format(TEXT("multiplayer.player.joined"), { GetPlayerName(NewPlayer) }), 0xFFFF55);
	}
}

// ---- Chat ----

namespace
{
	// What commands do, done to this game's players and worlds.
	class FGameCommandTarget : public IMcCommandTarget
	{
	public:
		explicit FGameCommandTarget(AMcGameMode& InMode) : Mode(InMode) {}

		AMcPlayerPawn* FindPawn(const FString& Player) const
		{
			const APlayerController* Controller = Mode.FindPlayer(Player);
			return Controller ? Cast<AMcPlayerPawn>(Controller->GetPawn()) : nullptr;
		}

		virtual TArray<FString> GetPlayerNames() const override
		{
			TArray<FString> Names;
			for (FConstPlayerControllerIterator It = Mode.GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const FString Name = AMcGameMode::GetPlayerName(It->Get());
				if (!Name.IsEmpty())
				{
					Names.Add(Name);
				}
			}
			return Names;
		}
		virtual int32 GetMaxPlayers() const override
		{
			return Mode.GetNetMode() == NM_DedicatedServer ? FMcServerProperties::Get().GetInt(TEXT("max-players"), 20) : 8;
		}
		virtual bool IsDedicated() const override { return Mode.GetNetMode() == NM_DedicatedServer; }
		virtual void SendTo(const FString& Player, const FMcCommandReply& Line) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (Pawn && Pawn->Data)
			{
				Pawn->Data->TellOwner(Line.Text, Line.bError ? 0xFF5555 : 0xFFFFFF);
			}
			else if (Player.IsEmpty())
			{
				UE_LOG(LogMcClient, Log, TEXT("%s"), *Line.Text);
			}
		}
		virtual void Broadcast(const FString& Line) override { Mode.BroadcastChat(Line); }

		virtual bool IsCreative(const FString& Player) const override
		{
			const AMcPlayerPawn* Pawn = FindPawn(Player);
			return Pawn && Pawn->Data && Pawn->Data->bCreative;
		}
		virtual void SetCreative(const FString& Player, bool bCreative) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (Pawn && Pawn->Data)
			{
				Pawn->Data->bCreative = bCreative;
			}
		}
		virtual bool GetPosition(const FString& Player, FVector& OutFeet) const override
		{
			const AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn)
			{
				return false;
			}
			OutFeet = Pawn->GetFeetPosition();
			return true;
		}
		virtual void Teleport(const FString& Player, const FVector& Feet) override
		{
			if (AMcPlayerPawn* Pawn = FindPawn(Player))
			{
				Pawn->ServerTeleport(Feet, Pawn->GetLookYaw());
			}
		}
		virtual void Kill(const FString& Player) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data || Pawn->IsServerDead())
			{
				return;
			}
			// /kill reaches creative players too, which nothing else hurts.
			const bool bCreative = Pawn->Data->bCreative;
			Pawn->Data->bCreative = false;
			Pawn->ServerHurt(1.0e9f, McSurvival::EDamage::Void);
			Pawn->Data->bCreative = bCreative;
		}
		virtual void Kick(const FString& Player, const FString& Reason) override
		{
			APlayerController* Controller = Mode.FindPlayer(Player);
			if (Controller && Mode.GameSession)
			{
				Mode.GameSession->KickPlayer(Controller, FText::FromString(Reason));
			}
		}
		virtual int32 GetMaxStackSize(FName Item) const override
		{
			FMcItemRegistry& Items = FMcItemRegistry::Get();
			return Items.EnsureLoaded() && Items.Exists(Item) ? Items.GetMaxStackSize(Item) : 0;
		}
		virtual FString GetItemName(FName Item) const override
		{
			const FString Path = McCache::PathOf(Item.ToString());
			const FString AsItem = McLang::Get(TEXT("item.minecraft.") + Path);
			return AsItem.StartsWith(TEXT("item.")) ? McLang::Get(TEXT("block.minecraft.") + Path) : AsItem;
		}
		virtual void Give(const FString& Player, FName Item, int32 Count) override
		{
			GiveWith(Player, Item, Count, {});
		}
		virtual void GiveWith(const FString& Player, FName Item, int32 Count, const TMap<FName, FString>& Components) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data)
			{
				return;
			}
			const int32 MaxStack = FMath::Max(1, GetMaxStackSize(Item));
			while (Count > 0)
			{
				const int32 Now = FMath::Min(Count, MaxStack);
				Count -= Now;
				// What doesn't fit falls at the player's feet.
				FMcItemStack Stack(Item, Now);
				for (const TPair<FName, FString>& Component : Components)
				{
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TEXT("{\"v\":") + Component.Value.Replace(TEXT("'"), TEXT("\"")) + TEXT("}"));
					TSharedPtr<FJsonObject> Wrapper;
					if (FJsonSerializer::Deserialize(Reader, Wrapper) && Wrapper.IsValid())
					{
						Stack.Set(Component.Key, Wrapper->TryGetField(TEXT("v")));
					}
				}
				const FMcItemStack Left = Pawn->Data->Inventory->AddItem(Stack);
				if (!Left.IsEmpty() && Pawn->Data->ServerDrop)
				{
					Pawn->Data->ServerDrop(Left);
				}
			}
		}

		virtual double GetDayTime() const override { return Mode.GetPrimaryWorld() ? Mode.GetPrimaryWorld()->GetDayTime() : 0.0; }
		virtual void SetDayTime(double Ticks) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetDayTime(Ticks);
			}
		}
		virtual double GetGameTime() const override { return Mode.GetPrimaryWorld() ? static_cast<double>(Mode.GetPrimaryWorld()->GetGameTime()) : 0.0; }
		virtual void SetWeather(EMcWeather Weather, int32 DurationTicks) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetWeather(Weather, DurationTicks);
			}
		}
		virtual int64 GetSeed() const override { return Mode.GetPrimaryWorld() ? Mode.GetPrimaryWorld()->GetLevel().Seed : 0; }
		virtual int32 GetDifficulty() const override { return Mode.GetPrimaryWorld() ? Mode.GetPrimaryWorld()->GetLevel().Difficulty : 2; }
		virtual void SetDifficulty(int32 Difficulty) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetDifficulty(Difficulty);
			}
			Mode.ApplyGameRules();
		}
		virtual FString GetGameRule(const FString& Rule) const override
		{
			const FString* Value = Mode.GetPrimaryWorld() ? Mode.GetPrimaryWorld()->GetLevel().GameRules.Find(Rule) : nullptr;
			return Value ? *Value : FString();
		}
		virtual void SetGameRule(const FString& Rule, const FString& Value) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetGameRule(Rule, Value);
			}
			Mode.ApplyGameRules();
		}

		virtual bool GetBlock(const FIntVector& Pos, FMcStateId& OutState) const override
		{
			const AMcWorldActor* World = Mode.GetPrimaryWorld();
			const FMcBlockWorld* Blocks = World ? World->GetBlockWorld() : nullptr;
			if (!Blocks || !Blocks->HasChunk(FIntPoint(FMath::FloorToInt(Pos.X / 16.0), FMath::FloorToInt(Pos.Z / 16.0)))
				|| Pos.Y < Blocks->GetDimension().MinY || Pos.Y >= Blocks->GetDimension().MinY + Blocks->GetDimension().Height)
			{
				return false;
			}
			OutState = Blocks->GetBlock(Pos);
			return true;
		}
		virtual void SetBlock(const FIntVector& Pos, FMcStateId State) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetBlock(Pos, State);
			}
		}
		virtual bool Summon(FName Kind, const FVector& Feet) override
		{
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
			if (Host && FMcVehicle::IsVehicleType(Kind))
			{
				TUniquePtr<FMcVehicle> Vehicle = FMcVehicle::Create(Kind);
				Vehicle->Body.Position = Feet;
				Host->GetManager().Add(MoveTemp(Vehicle));
				return true;
			}
			return Host && Host->SpawnMob(Kind, Feet) != nullptr;
		}

		virtual void GetXp(const FString& Player, int32& OutLevel, int32& OutPoints) const override
		{
			const AMcPlayerPawn* Pawn = FindPawn(Player);
			if (Pawn && Pawn->Data)
			{
				const FMcPlayerStats& Stats = Pawn->Data->Stats;
				OutLevel = Stats.XpLevel;
				OutPoints = McSurvival::GetTotalXp(Stats) - McSurvival::GetTotalXpForLevel(Stats.XpLevel);
			}
		}
		virtual void AddXp(const FString& Player, int32 Amount, bool bLevels) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data)
			{
				return;
			}
			FMcPlayerStats& Stats = Pawn->Data->Stats;
			if (bLevels)
			{
				Stats.XpLevel = FMath::Max(0, Stats.XpLevel + Amount);
			}
			else
			{
				McSurvival::AddXp(Stats, Amount);
			}
		}
		virtual bool SetXp(const FString& Player, int32 Amount, bool bLevels) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data)
			{
				return false;
			}
			FMcPlayerStats& Stats = Pawn->Data->Stats;
			if (bLevels)
			{
				Stats.XpLevel = FMath::Max(0, Amount);
				return true;
			}
			const int32 InLevel = McSurvival::GetXpToNextLevel(Stats.XpLevel);
			if (Amount > InLevel)
			{
				return false;
			}
			Stats.XpProgress = static_cast<float>(Amount) / InLevel;
			return true;
		}
		virtual int32 Clear(const FString& Player, FName Item, int32 MaxCount, bool bTest) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data)
			{
				return 0;
			}
			int32 Found = 0;
			const auto Take = [&](FMcItemStack& Stack)
			{
				if (Stack.IsEmpty() || (!Item.IsNone() && Stack.Item != Item))
				{
					return;
				}
				const int32 Count = MaxCount < 0 ? Stack.Count : FMath::Min(Stack.Count, MaxCount - Found);
				if (bTest || MaxCount == 0)
				{
					Found += Stack.Count;
					return;
				}
				Found += Count;
				Stack.Count -= Count;
				if (Stack.Count <= 0)
				{
					Stack = FMcItemStack();
				}
			};
			for (const TSharedPtr<FMcContainer>& Container : { Pawn->Data->Inventory, Pawn->Data->Armor, Pawn->Data->Offhand })
			{
				for (FMcItemStack& Stack : Container->GetAll())
				{
					Take(Stack);
				}
			}
			return Found;
		}

		virtual bool IsEffect(FName Effect) const override
		{
			FName Parsed;
			return McEffects::Parse(Effect.ToString(), Parsed);
		}
		virtual bool GiveEffect(const FString& Player, FName Effect, int32 Amplifier, int32 DurationTicks) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			return Pawn && Pawn->Data && McEffects::Add(Pawn->Data->Stats, Effect, Amplifier, DurationTicks);
		}
		virtual int32 ClearEffects(const FString& Player, FName Effect) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			return Pawn && Pawn->Data ? McEffects::Remove(Pawn->Data->Stats, Effect) : 0;
		}
		virtual int32 GetEnchantmentMaxLevel(FName Enchantment) const override
		{
			const FMcEnchantmentDef* Def = FMcItemRegistry::Get().FindEnchantment(Enchantment);
			return Def ? Def->MaxLevel : 0;
		}
		virtual EEnchantResult Enchant(const FString& Player, FName Enchantment, int32 Level) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data || Pawn->Data->Inventory->Get(Pawn->Data->SelectedSlot).IsEmpty())
			{
				return EEnchantResult::NoItem;
			}
			FMcItemStack& Held = Pawn->Data->Inventory->GetMutable(Pawn->Data->SelectedSlot);
			const FMcItemRegistry& Registry = FMcItemRegistry::Get();
			TArray<FName> Allowed;
			Registry.GetEnchantmentsFor(Held.Item, Allowed);
			TMap<FName, int32> Have = Held.GetEnchantments();
			if (!Allowed.Contains(Enchantment))
			{
				return EEnchantResult::Unsupported;
			}
			for (const TPair<FName, int32>& Other : Have)
			{
				if (Other.Key != Enchantment && !Registry.AreCompatible(Other.Key, Enchantment))
				{
					return EEnchantResult::Unsupported;
				}
			}
			Have.Add(Enchantment, Level);
			Held.SetEnchantments(Have);
			return EEnchantResult::Done;
		}

		virtual FMcServerAccess LoadAccess() const override { return FMcServerAccess::Load(); }
		virtual void SaveAccess(const FMcServerAccess& Access) override { Access.Save(); }
		virtual bool IsAllowlistOn() const override { return FMcServerProperties::Get().GetBool(TEXT("white-list")); }
		virtual void SetAllowlistOn(bool bOn) override { FMcServerProperties::SetAndSave(TEXT("white-list"), bOn ? TEXT("true") : TEXT("false")); }

	private:
		AMcGameMode& Mode;
	};
}

FString AMcGameMode::GetPlayerName(const AController* Controller)
{
	return Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : FString();
}

APlayerController* AMcGameMode::FindPlayer(const FString& Name) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (GetPlayerName(It->Get()).Equals(Name, ESearchCase::IgnoreCase))
		{
			return It->Get();
		}
	}
	return nullptr;
}

void AMcGameMode::BroadcastChat(const FString& Text, uint32 Rgb)
{
	UE_LOG(LogMcClient, Log, TEXT("[Chat] %s"), *Text);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMcPlayerPawn* Pawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		if (Pawn && Pawn->Data)
		{
			Pawn->Data->TellOwner(Text, Rgb);
		}
	}
}

void AMcGameMode::HandleChat(APlayerController* From, const FString& Text)
{
	FMcCommandSender Sender;
	Sender.Name = GetPlayerName(From);
	// The player playing a world of their own (or hosting it) can use every command, as with cheats on; on a dedicated
	// server it is what ops.json says.
	const bool bHost = GetNetMode() != NM_DedicatedServer && From && From->IsLocalController();
	Sender.PermissionLevel = McCommands::GetPermissionLevel(Sender.Name, bHost, bHost ? FMcServerAccess() : FMcServerAccess::Load());
	if (Text.StartsWith(TEXT("/")))
	{
		UE_LOG(LogMcClient, Log, TEXT("%s issued server command: %s"), *Sender.Name, *Text);
	}
	FGameCommandTarget Target(*this);
	for (const FMcCommandReply& Reply : McCommands::HandleChat(Text, Sender, Target))
	{
		Target.SendTo(Sender.Name, Reply);
	}
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
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapHandle);
	PreLoadMapHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

FTransform AMcGameMode::GetSpawnTransform() const
{
	// Before the world exists, guess the classic flat surface (y = -61).
	const bool bWorld = WorldActor && WorldActor->GetBlockWorld();
	const int32 SurfaceY = bWorld ? WorldActor->GetSurfaceY() : -61;
	const FIntPoint Column = bWorld ? WorldActor->GetSpawnColumn() : FIntPoint::ZeroValue;
	const FVector Block(Column.X + 0.5, SurfaceY + 1 + SpawnHeightAboveSurface, Column.Y + 0.5);
	return FTransform(FRotator::ZeroRotator, McCoords::BlockToUnreal(Block));
}

