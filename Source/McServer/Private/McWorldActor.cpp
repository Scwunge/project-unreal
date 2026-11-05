#include "McWorldActor.h"
#include "McSculkMobs.h"
#include "McChestLink.h"
#include "McBannerEntity.h"
#include "McJavaImport.h"
#include "McTnt.h"
#include "McCache.h"
#include "McRails.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McFlatGenerator.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McFeature.h"
#include "McMob.h"
#include "McGenRegion.h"
#include "McNoiseGenerator.h"
#include "McPortal.h"
#include "McSimulation.h"
#include "McSpawners.h"
#include "McPhysics.h"
#include "McServer.h"
#include "McStructureSpawns.h"
#include "McJavaImport.h"
#include "McWorldStorage.h"
#include "McLaunch.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "Tasks/Task.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

AMcWorldActor::AMcWorldActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// Clients get one too, holding the chunks streamed to them.
	bReplicates = true;
	bAlwaysRelevant = true;
}

AMcWorldActor::~AMcWorldActor() = default;

void AMcWorldActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMcWorldActor, SyncedDayTime);
	DOREPLIFETIME(AMcWorldActor, SyncedGameTime);
	DOREPLIFETIME(AMcWorldActor, bTimeFrozen);
	DOREPLIFETIME(AMcWorldActor, Weather);
	DOREPLIFETIME_CONDITION(AMcWorldActor, Dimension, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMcWorldActor, bSecondary, COND_InitialOnly);
}

void AMcWorldActor::InheritFrom(const AMcWorldActor& Primary)
{
	bSecondary = true;
	Seed = Primary.Seed;
	WorldName = Primary.WorldName;
	WorldDir = Primary.WorldDir;
	Level = Primary.Level;
	Level.Seed = Primary.Seed;
	Level.bFlat = false;
	bFlat = false;
	ViewDistance = Primary.ViewDistance;
	EntityProxyClass = Primary.EntityProxyClass;
}

int32 AMcWorldActor::AddAnchor(FIntPoint Chunk, int32 Radius)
{
	const int32 Id = NextAnchorId++;
	Anchors.Add(Id, FAnchor{ Chunk, Radius });
	SettledCentres.Reset();
	return Id;
}

void AMcWorldActor::RemoveAnchor(int32 Id)
{
	Anchors.Remove(Id);
	SettledCentres.Reset();
}

bool AMcWorldActor::AreChunksLoaded(FIntPoint Chunk, int32 Radius) const
{
	if (!BlockWorld)
	{
		return false;
	}
	for (int32 DZ = -Radius; DZ <= Radius; ++DZ)
	{
		for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			if (!BlockWorld->HasChunk(Chunk + FIntPoint(DX, DZ)))
			{
				return false;
			}
		}
	}
	return true;
}

void AMcWorldActor::SetDayTime(double Ticks)
{
	DayTicks = Ticks;
	SyncedDayTime = static_cast<int64>(FMath::FloorToDouble(Ticks));
	TicksSinceSync = 0.0;
}

bool AMcWorldActor::HasWeather() const
{
	// The Nether (a ceiling) and the End (no sky light) never rain.
	return BlockWorld && BlockWorld->GetDimension().bHasSkylight && !BlockWorld->GetDimension().bHasCeiling;
}

void AMcWorldActor::OnRep_SyncedDayTime()
{
	// The server's count a moment ago. Small differences are the clocks drifting apart; leave those be so the sky
	// doesn't twitch, and take the server's word for anything bigger (/time set, a long hitch).
	const double ServerTicks = static_cast<double>(SyncedDayTime);
	if (FMath::Abs(DayTicks - ServerTicks) > 4.0)
	{
		DayTicks = ServerTicks;
	}
}

void AMcWorldActor::OnRep_SyncedGameTime()
{
	const double ServerTicks = static_cast<double>(SyncedGameTime);
	if (FMath::Abs(GameTicks - ServerTicks) > 4.0)
	{
		GameTicks = ServerTicks;
	}
}

void AMcWorldActor::BeginPlay()
{
	Super::BeginPlay();

	// Development: -McDimension=minecraft:the_nether or minecraft:the_end.
	FString DimensionArg;
	const bool bDimensionArg = !bSecondary && HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("-McDimension="), DimensionArg);
	if (bDimensionArg)
	{
		Dimension = DimensionArg;
		NoiseSettings = DimensionArg == TEXT("minecraft:the_end") ? TEXT("minecraft:end") : DimensionArg == TEXT("minecraft:the_nether") ? TEXT("minecraft:nether") : TEXT("minecraft:overworld");
	}
	FMcDimensionType DimensionType;
	if (!HasAuthority())
	{
		// A client: an empty world that McNet fills with what the server sends.
		if (FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcDimensionType::Load(Dimension, DimensionType))
		{
			BlockWorld = MakeUnique<FMcBlockWorld>(DimensionType);
		}
		else
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't join: cache/ is missing or incomplete. Run tools/fetch_cache.py."));
		}
		return;
	}
	const TCHAR* CommandLine = FCommandLine::Get();

	// A named world (-McWorld=<name>, ?McWorld=<name> from the title screen, level-name on a dedicated server) is read
	// from Saved/Worlds/<name>, or made there.
	FString WorldArg;
	bool bHaveLevel = false;
	if (bSecondary)
	{
		// Another dimension of the first world: its save folder and seed come from there.
		bHaveLevel = !WorldName.IsEmpty();
	}
	else if (McLaunch::GetValue(GetWorld(), TEXT("McWorld"), WorldArg))
	{
		WorldName = McWorldSave::SanitizeName(WorldArg);
		if (WorldName.IsEmpty())
		{
			UE_LOG(LogMcServer, Error, TEXT("-McWorld=%s isn't a usable world name; running without a save."), *WorldArg);
		}
		else
		{
			WorldDir = McWorldSave::GetWorldDir(WorldName);
			bHaveLevel = McWorldSave::LoadLevel(WorldDir, Level);
			McWorldSave::LoadMaps(WorldDir);
			if (!bHaveLevel && IFileManager::Get().FileExists(*(WorldDir / TEXT("level.json"))))
			{
				// Starting a new world over this one's chunks would mix two seeds; leave it as it is.
				UE_LOG(LogMcServer, Error, TEXT("World %s has a level.json that can't be read; running without a save so it isn't overwritten."), *WorldName);
				WorldName.Reset();
				WorldDir.Reset();
			}
		}
	}
	if (bHaveLevel)
	{
		// The world carries on in the dimension it was in, unless the command line says otherwise.
		if (!bDimensionArg && !bSecondary)
		{
			Dimension = Level.Dimension;
		}
		if (Dimension == Level.Dimension && !bSecondary)
		{
			NoiseSettings = Level.NoiseSettings;
		}
	}

	if (!FMcBlockRegistry::Get().EnsureLoaded() || !FMcBiomeRegistry::Get().EnsureLoaded() || !FMcDimensionType::Load(Dimension, DimensionType))
	{
		UE_LOG(LogMcServer, Error, TEXT("Can't start the world: cache/ is missing or incomplete. Run tools/fetch_cache.py."));
		return;
	}

	FString Scene;
	if (!bSecondary)
	{
		bFlat |= McLaunch::HasFlag(GetWorld(), TEXT("McFlat")) || FParse::Value(CommandLine, TEXT("-McScene="), Scene);
	}
#if !UE_BUILD_SHIPPING
	// Development: -McFlatPreset=minecraft:snowy_kingdom picks the flat preset (a saved world keeps its own).
	if (!bSecondary)
	{
		FParse::Value(CommandLine, TEXT("-McFlatPreset="), FlatPreset);
	}
#endif
	FString SeedArg;
	const bool bSeedArg = !bSecondary && McLaunch::GetValue(GetWorld(), TEXT("McSeed"), SeedArg);
	if (bSeedArg)
	{
		Seed = McLaunch::ParseSeed(SeedArg);
	}
	double StartTime = DayTicks;
	const bool bTimeArg = !bSecondary && FParse::Value(CommandLine, TEXT("-McTime="), StartTime);
	if (bHaveLevel)
	{
		// What the world was made with wins over the command line, which only shapes a world being made.
		if (bSeedArg && Seed != Level.Seed)
		{
			UE_LOG(LogMcServer, Warning, TEXT("World %s was made with seed %lld; ignoring -McSeed=%lld."), *WorldName, Level.Seed, Seed);
		}
		Seed = Level.Seed;
		bFlat = Level.bFlat;
		if (!Level.FlatPreset.IsEmpty() && !bSecondary)
		{
			FlatPreset = Level.FlatPreset;
		}
		if (!bSecondary)
		{
			SetDayTime(Level.DayTime);
		}
		const FString* DaylightRule = Level.GameRules.Find(TEXT("doDaylightCycle"));
		bTimeFrozen = DaylightRule && DaylightRule->Equals(TEXT("false"), ESearchCase::IgnoreCase);
	}
	if (bTimeArg)
	{
		SetDayTime(StartTime);
	}
	bTimeFrozen |= FParse::Param(CommandLine, TEXT("McTimeFreeze"));
	if (DimensionType.bHasFixedTime)
	{
		// The Nether and the End have one time of day, so their clock doesn't run.
		bTimeFrozen = true;
	}
	if (Seed == 0)
	{
		Seed = static_cast<int64>(FPlatformTime::Cycles64() ^ (static_cast<uint64>(FDateTime::Now().GetTicks()) << 7));
	}

	FString Kind;
	if (bFlat)
	{
		FMcFlatSettings Settings;
		if (!FMcFlatSettings::LoadPreset(FlatPreset, Settings))
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't start the world: flat preset %s didn't load."), *FlatPreset);
			return;
		}
		Generator = MakeShared<FMcFlatGenerator, ESPMode::ThreadSafe>(Settings, DimensionType.MinY);
		Kind = TEXT("flat preset ") + FlatPreset;
	}
	else
	{
		FMcNoiseSettings Settings;
		TSharedPtr<FMcNoiseGenerator, ESPMode::ThreadSafe> Noise;
		if (FMcNoiseSettings::Load(NoiseSettings, Settings))
		{
			Noise = MakeShared<FMcNoiseGenerator, ESPMode::ThreadSafe>(Settings, static_cast<uint64>(Seed), NoiseSettings);
		}
		if (!Noise || !Noise->IsValid())
		{
			UE_LOG(LogMcServer, Error, TEXT("Can't start the world: noise settings %s didn't load."), *NoiseSettings);
			return;
		}
		Generator = Noise;
		Features = MakeShared<FMcFeatureLibrary>(Settings.SeaLevel, Settings.MinY, Settings.Height);
		if (!FMcBlockShapes::Get().EnsureLoaded() || !FMcBlockTags::Get().EnsureLoaded() || !Features->LoadBiomes())
		{
			UE_LOG(LogMcServer, Warning, TEXT("Some biome features didn't load; those are left out."));
		}
		Features->SetHeightSource([Noise](int32 X, int32 Z) { return Noise->FindSurfaceY(X, Z) + 1; });
		Features->SetBiomeSource([Noise](int32 X, int32 Y, int32 Z) { return Noise->GetBiome(X, Y, Z); });
		Kind = FString::Printf(TEXT("%s terrain, seed %lld"), *NoiseSettings, Seed);
	}

	BlockWorld = MakeUnique<FMcBlockWorld>(DimensionType);
	Simulation = MakeUnique<FMcSimulation>(*BlockWorld);
	// Levers, buttons and doors are heard by the players.
	Simulation->OnSound = [this](const FMcSoundEvent& Event)
	{
		MulticastSound(Event.Name, FVector_NetQuantize10(Event.Position), Event.Volume, Event.Pitch);
	};
	Simulation->OnNoteParticle = [this](const FVector& Position, int32 Note)
	{
		MulticastNote(FVector_NetQuantize10(Position), Note);
	};
	Simulation->OnBellRung = [this](const FIntVector& Pos, int32 Side)
	{
		MulticastBellRung(Pos, static_cast<uint8>(Side));
	};
	Entities = MakeUnique<FMcEntityHost>(*BlockWorld, Seed);
	if (!bSecondary)
	{
		FMcStructureSpawns::Get().SetRecording(true);
	}
	Entities->SetProxyClass(GetWorld(), EntityProxyClass);
	Entities->GetSpawners().OnCycle = [this](const FIntVector& Pos)
	{
		MulticastSpawnerCycled(Pos);
	};
	Simulation->OnBlocksPushed = [this](const TArray<FIntVector>& Cells, const FIntVector& Dir)
	{
		FPistonPush& Push = PistonPushes.AddDefaulted_GetRef();
		Push.Cells = Cells;
		Push.Dir = Dir;
	};
	// TNT lit by fire or redstone becomes a primed block.
	Simulation->OnLightTnt = [this](const FIntVector& Pos)
	{
		if (Entities)
		{
			FMcPrimedTnt::Ignite(Entities->GetManager(), Pos);
		}
	};
	// A falling block that lands where it can't stand (a torch, a slab) breaks into the item of the same name.
	Simulation->OnFallingBlockDropped = [this](const FIntVector& Pos, FMcStateId State)
	{
		if (Entities)
		{
			FMcItemStack Stack;
			Stack.Item = FMcBlockRegistry::Get().GetBlockOf(State).Name;
			Stack.Count = 1;
			Entities->GetManager().DropItem(FVector(Pos) + FVector(0.5), Stack);
		}
	};
	// An anvil that lands hurts what is in its cell: 2 a block fallen beyond the first, 40 at most (reference client,
	// rs anvil_damage: iron golems under anvils dropped 4 to 19 blocks took 8, 8, 10, 12, 16, 20, 28, 36). Whole
	// The distance is summed in a float tick by tick, so whole-block falls land on or just under the integer (the rule fits
	// 5 to 19 blocks fallen; the 4-block fall read 8, not the 6 it gives, 4 golems out of 4: unexplained).
	Simulation->OnFallingBlockLanded = [this](const FIntVector& Pos, FMcStateId State, double Fallen)
	{
		const FString Name = FMcBlockRegistry::Get().GetBlockOf(State).Name.ToString();
		if (!Entities || !Name.EndsWith(TEXT("anvil")) || Fallen <= 1.0)
		{
			return;
		}
		const float Damage = static_cast<float>(FMath::Min(2 * FMath::CeilToInt(static_cast<float>(Fallen) - 1.0f), 40));
		const FBox Cell(FVector(Pos), FVector(Pos) + FVector(1.0));
		TArray<FMcEntity*> Hit;
		Entities->GetManager().FindInBox(Cell, Hit);
		for (FMcEntity* Entity : Hit)
		{
			if (Entity->IsLiving() && !Entity->bRemoved)
			{
				static_cast<FMcMob&>(*Entity).Damage(Damage, nullptr);
			}
		}
		for (const FVector& Player : Entities->GetManager().PlayerPositions)
		{
			if (Cell.IsInside(Player + FVector(0.0, 0.5, 0.0)))
			{
				Entities->GetManager().HurtPlayer(Player, Damage);
			}
		}
	};
	// A minecart on a detector rail powers it.
	Entities->GetManager().OnDetector = [this](const FIntVector& Pos, bool bOccupied)
	{
		if (Simulation)
		{
			Simulation->SetDetectorRail(Pos, bOccupied);
		}
	};
	// An arrow in a target block gives it a signal for 20 ticks. Strength by where it hit: 15 in the middle of the face
	// down to 1 at the edge. Measured: the reference client's rs_target (arrows summoned at a 7x7 grid of spots on the face): power =
	// ceil(15 * (1 - 2 * d)), d the larger of the two offsets from the face centre, 1 at least; 20 ticks, then off.
	Entities->GetManager().OnArrowHit = [this](const FIntVector& Cell, const FVector& Hit, const FVector& Velocity)
	{
		if (!Simulation)
		{
			return;
		}
		const FVector Local = Hit - FVector(Cell);
		const FVector Abs = Velocity.GetAbs();
		// The two axes across the face the arrow came in by.
		const int32 Axis = Abs.X >= Abs.Y && Abs.X >= Abs.Z ? 0 : Abs.Y >= Abs.Z ? 1 : 2;
		double Away = 0.0;
		for (int32 Other = 0; Other < 3; ++Other)
		{
			if (Other != Axis)
			{
				Away = FMath::Max(Away, FMath::Abs(Local[Other] - 0.5));
			}
		}
		Simulation->HitTarget(Cell, FMath::Clamp(15 - FMath::FloorToInt32(Away * 2.0 * 15.0), 1, 15), 20);
	};
	// A villager shutting a door goes through the same path as any other change.
	Entities->GetManager().OnSetBlock = [this](const FIntVector& Pos, FMcStateId State) { SetBlock(Pos, State); };
	// Every sound the entities make goes to the players.
	Entities->GetManager().OnSound = [this](const FMcSoundEvent& Event)
	{
		MulticastSound(Event.Name, FVector_NetQuantize10(Event.Position), Event.Volume, Event.Pitch);
	};
	Entities->GetManager().OnParticle = [this](FName Name, const FVector& Position, const FVector& Delta, float Speed, int32 Count)
	{
		MulticastParticle(Name, FVector_NetQuantize10(Position), FVector_NetQuantize10(Delta), Speed, Count);
	};
	// What the entities and players do that can be heard as vibrations goes to the simulation (sculk sensors, shriekers, listeners).
	Entities->GetManager().OnGameEvent = [this](FName Name, const FVector& At, const FMcEntityManager::FGameEventTag& Tag)
	{
		if (!Simulation)
		{
			return;
		}
		FMcGameEventSource Source;
		Source.bPlayer = Tag.bPlayer;
		Source.bSneaking = Tag.bSneaking;
		Source.bWarden = Tag.bWarden;
		Source.bProjectile = Tag.bProjectile;
		Source.bDying = Tag.bDying;
		Source.bCreativeOrSpectator = Tag.bCreativeOrSpectator;
		Source.PlayerId = Tag.PlayerId;
		Simulation->EmitGameEvent(Name, FIntVector(FMath::FloorToInt32(At.X), FMath::FloorToInt32(At.Y), FMath::FloorToInt32(At.Z)), Source);
	};
	// Wardens hear the events, shriekers call them, creaking hearts raise creakings.
	McSculkMobs::Connect(*Simulation, *Entities, *BlockWorld);
	// Development: -McWeather=clear|rain|thunder, at full strength from the start (for screenshots).
	FString WeatherArg;
	if (!bSecondary && FParse::Value(CommandLine, TEXT("-McWeather="), WeatherArg))
	{
		const EMcWeather Forced = WeatherArg == TEXT("thunder") ? EMcWeather::Thunder : WeatherArg == TEXT("rain") ? EMcWeather::Rain : EMcWeather::Clear;
		Simulation->SetWeather(Forced, 0);
		Weather = static_cast<uint8>(Forced);
		RainLevel = Forced != EMcWeather::Clear ? 1.0f : 0.0f;
		ThunderLevel = Forced == EMcWeather::Thunder ? 1.0f : 0.0f;
	}
	if (!bSecondary)
	{
		FParse::Value(CommandLine, TEXT("-McStrike="), DevStrike, /*bShouldStopOnSeparator=*/ false);
	}
	BlockWorld->OnChunkAdded.AddLambda([this](FIntPoint Pos) { if (Entities) { Entities->OnChunkAdded(Pos); } });
	BlockWorld->OnChunkRemoved.AddLambda([this](FIntPoint Pos) { if (Entities) { Entities->OnChunkRemoved(Pos); } });
	Finished = MakeShared<TQueue<FFinishedChunk, EQueueMode::Mpsc>, ESPMode::ThreadSafe>();
	SurfaceY = Generator->FindSurfaceY(0, 0);
	if (DimensionType.bHasCeiling)
	{
		// The top block is the roof: look outwards from 0, 0 for a floor under it, above the lava.
		const int32 BelowRoof = DimensionType.MinY + DimensionType.LogicalHeight - 4;
		bool bFound = false;
		for (int32 Ring = 0; Ring <= 8 && !bFound; ++Ring)
		{
			for (int32 DZ = -Ring; DZ <= Ring && !bFound; ++DZ)
			{
				for (int32 DX = -Ring; DX <= Ring && !bFound; ++DX)
				{
					if (FMath::Max(FMath::Abs(DX), FMath::Abs(DZ)) != Ring)
					{
						continue;
					}
					const FIntPoint Column(DX * 8, DZ * 8);
					const int32 Floor = Generator->FindFloorY(Column.X, Column.Y, BelowRoof);
					if (Floor >= DimensionType.MinY)
					{
						SpawnColumn = Column;
						SurfaceY = Floor;
						bFound = true;
					}
				}
			}
		}
	}
	else if (bHaveLevel)
	{
		// A saved world keeps the spawn it was made with.
		SpawnColumn = FIntPoint(Level.Spawn.X, Level.Spawn.Z);
		SurfaceY = Level.Spawn.Y - 1;
	}
	else if (!bFlat && Dimension == TEXT("minecraft:overworld"))
	{
		// A new overworld: spawn on dry land, not over the ocean at 0, 0 (players drowned there on 2026-11-04). Look outwards in rings of 16 blocks.
		const int32 SeaLevel = 63;
		bool bLand = Generator->FindSolidTopY(0, 0) >= SeaLevel;
		for (int32 Ring = 1; Ring <= 40 && !bLand; ++Ring)
		{
			for (int32 DZ = -Ring; DZ <= Ring && !bLand; ++DZ)
			{
				for (int32 DX = -Ring; DX <= Ring && !bLand; ++DX)
				{
					if (FMath::Max(FMath::Abs(DX), FMath::Abs(DZ)) != Ring)
					{
						continue;
					}
					const FIntPoint Column(DX * 16, DZ * 16);
					if (Generator->FindSolidTopY(Column.X, Column.Y) >= SeaLevel)
					{
						SpawnColumn = Column;
						SurfaceY = Generator->FindSurfaceY(Column.X, Column.Y);
						bLand = true;
					}
				}
			}
		}
	}
	if (!WorldName.IsEmpty())
	{
		if (!bHaveLevel && !bSecondary)
		{
			Level = FMcLevelData();
			Level.Seed = Seed;
			Level.Spawn = FIntVector(SpawnColumn.X, SurfaceY + 1, SpawnColumn.Y);
			Level.Dimension = Dimension;
			Level.NoiseSettings = NoiseSettings;
			Level.bFlat = bFlat;
			Level.FlatPreset = bFlat ? FlatPreset : FString();
			FString Mode, Name;
			if (McLaunch::GetValue(GetWorld(), TEXT("McGameMode"), Mode))
			{
				Level.GameMode = McLaunch::IsCreativeMode(Mode) ? TEXT("creative") : TEXT("survival");
			}
			Level.Name = McLaunch::GetValue(GetWorld(), TEXT("McName"), Name) ? Name : WorldName;
		}
		StartStorage(WorldName);
	}
	ApplyGameRules(GetGameRules());
	UE_LOG(LogMcServer, Log, TEXT("World ready: %s, %s, %d sections from y=%d, surface at spawn y=%d%s"),
		*Dimension, *Kind, DimensionType.NumSections(), DimensionType.MinY, SurfaceY,
		Storage ? *FString::Printf(TEXT(", saved as %s (%s)"), *WorldName, bHaveLevel ? TEXT("loaded") : TEXT("new")) : TEXT(""));
}

void AMcWorldActor::SetGameRule(const FString& Rule, const FString& Value)
{
	Level.GameRules.Add(Rule, Value);
	if (Rule == TEXT("doDaylightCycle"))
	{
		bTimeFrozen = Value.Equals(TEXT("false"), ESearchCase::IgnoreCase);
	}
	ApplyGameRules(GetGameRules());
}

void AMcWorldActor::SetDifficulty(int32 Difficulty)
{
	Level.Difficulty = FMath::Clamp(Difficulty, 0, 3);
	ApplyGameRules(GetGameRules());
}

void AMcWorldActor::ApplyGameRules(const FMcGameRules& Rules)
{
	if (Entities)
	{
		Entities->SetRules(Rules);
	}
	if (Simulation)
	{
		Simulation->SetRandomTickSpeed(Rules.RandomTickSpeed);
		Simulation->SetWeatherCycle(Rules.bDoWeatherCycle);
	}
}

void AMcWorldActor::StartStorage(const FString& Name)
{
	Storage = MakeShared<FMcWorldStorage, ESPMode::ThreadSafe>(WorldDir / McWorldSave::GetDimensionFolder(Dimension));
	// A new world is on disk from the start, so a crash before the first autosave still leaves one to open.
	Level.DayTime = DayTicks;
	Level.GameRules.Add(TEXT("doDaylightCycle"), bTimeFrozen ? TEXT("false") : TEXT("true"));
	if (!bSecondary && !McWorldSave::SaveLevel(WorldDir, Level))
	{
		UE_LOG(LogMcServer, Error, TEXT("Can't write the level file of world %s; chunks will still be saved."), *Name);
	}
	// A Java world begun with McJavaImport::BeginImport is converted from here on, near its spawn first.
	LiveImport = McJavaImport::FLiveImport::Start(WorldDir, Dimension, Storage, FIntPoint(FMath::FloorToInt32(Level.Spawn.X / 16.0), FMath::FloorToInt32(Level.Spawn.Z / 16.0)));
}

void AMcWorldActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LiveImport)
	{
		// Before the last save, so nothing is being queued behind it.
		LiveImport->Stop();
		LiveImport.Reset();
	}
	// Chunks, mobs and players are all still here; this is the last chance to write them.
	if (Storage && BlockWorld)
	{
		SaveWorld();
	}
	// The entity actors have to go while the game world is still up; the destructor runs during garbage collection.
	if (Entities)
	{
		Entities->DestroyProxies();
	}
	Super::EndPlay(EndPlayReason);
}

void AMcWorldActor::SetEntityProxyClass(TSubclassOf<AActor> Class)
{
	EntityProxyClass = Class;
	if (Entities)
	{
		Entities->SetProxyClass(GetWorld(), Class);
	}
}

bool AMcWorldActor::AttackMobs(const FVector& Eye, const FVector& Look, float Reach, float Damage, bool bCreative, const FMcItemStack* Weapon, float Strength)
{
	return Entities && Entities->Attack(Eye, Look, Reach, Damage, bCreative, Weapon, Strength);
}

int32 AMcWorldActor::FindVehicle(const FVector& Eye, const FVector& Look, float Reach)
{
	const FMcEntity* Found = Entities ? Entities->FindVehicle(Eye, Look, Reach) : nullptr;
	return Found ? Found->Id : 0;
}

FMcMob* AMcWorldActor::FindMob(const FVector& Eye, const FVector& Look, float Reach)
{
	return Entities ? Entities->FindMob(Eye, Look, Reach) : nullptr;
}

void AMcWorldActor::PlayerBroke(const FIntVector& Pos)
{
	if (Simulation && BlockWorld)
	{
		Simulation->PlayerBroke(Pos, BlockWorld->GetBlock(Pos));
	}
}

void AMcWorldActor::PlayerPlaced(const FIntVector& Pos)
{
	if (Simulation)
	{
		Simulation->PlayerPlaced(Pos);
	}
}

void AMcWorldActor::SetBlock(const FIntVector& Pos, FMcStateId State, bool bCheckPortals)
{
	if (!BlockWorld)
	{
		UE_LOG(LogMcServer, Warning, TEXT("Can't change the block at %s: the world isn't running."), *Pos.ToString());
		return;
	}
	// Above or below the dimension there is nothing to change, so nothing to remember.
	const FMcDimensionType& DimensionType = BlockWorld->GetDimension();
	if (Pos.Y < DimensionType.MinY || Pos.Y >= DimensionType.MinY + DimensionType.Height)
	{
		return;
	}
	const FIntPoint ChunkPos(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z));
	// A saved world's loaded chunks keep their own changes (they're written when they change); only a chunk that
	// isn't there yet needs the change remembered.
	if (!Storage || !BlockWorld->HasChunk(ChunkPos))
	{
		Edits.FindOrAdd(ChunkPos).Add(Pos, State);
	}
	const FMcStateId Old = BlockWorld->HasChunk(ChunkPos) ? BlockWorld->GetBlock(Pos) : FMcStateId(0);
	BlockWorld->SetBlock(Pos, State);
	if (HasAuthority() && Old != State && BlockWorld->HasChunk(ChunkPos))
	{
		// A block taken away is heard as it breaking, one put down as placing (a player's own block sounds are made
		// here rather than in the player code).
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId Heard = State == 0 ? Old : State;
		const FMcSoundEvent Event = McSound::GetBlockSound(Registry.GetBlockOf(Heard).Name, State == 0 ? McSound::EBlockSound::Break : McSound::EBlockSound::Place, FVector(Pos.X + 0.5, Pos.Y + 0.5, Pos.Z + 0.5));
		if (!Event.Name.IsNone())
		{
			MulticastSound(Event.Name, FVector_NetQuantize10(Event.Position), Event.Volume, Event.Pitch);
		}
		// Fluids draining away and fire going out aren't broken; blocks are.
		const FName OldName = Registry.GetBlockOf(Old).Name;
		if (State == 0 && Old != 0 && OldName != TEXT("minecraft:water") && OldName != TEXT("minecraft:lava") && OldName != TEXT("minecraft:fire")
			&& OldName != TEXT("minecraft:soul_fire") && OldName != TEXT("minecraft:moving_piston"))
		{
			MulticastBlockBroken(Pos, Old);
		}
	}
	// The halves of a double chest follow each other as one is placed or taken away.
	if (HasAuthority() && Old != State && BlockWorld->HasChunk(ChunkPos))
	{
		McChestLink::Sync(*BlockWorld, Pos, Old, State, [this](const FIntVector& At, FMcStateId Other) { SetBlock(At, Other, false); });
		McRails::Sync(*BlockWorld, Pos, Old, State, [this](const FIntVector& At, FMcStateId Other) { SetBlock(At, Other, false); });
		// A flag stands only while its banner does.
		const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
		if (Entities && McCache::PathOf(Blocks.GetBlockOf(Old).Name.ToString()).EndsWith(TEXT("_banner"))
			&& Blocks.GetBlockOf(Old).Name != Blocks.GetBlockOf(State).Name)
		{
			if (FMcBannerEntity* Flag = FMcBannerEntity::FindAt(Entities->GetManager(), Pos))
			{
				Flag->bRemoved = true;
			}
		}
		// One put down without a player's patterns (a command, a structure) is a plain flag.
		if (Entities && McCache::PathOf(Blocks.GetBlockOf(State).Name.ToString()).EndsWith(TEXT("_banner"))
			&& Blocks.GetBlockOf(Old).Name != Blocks.GetBlockOf(State).Name && !FMcBannerEntity::FindAt(Entities->GetManager(), Pos))
		{
			FMcBannerEntity::Stand(Entities->GetManager(), Pos, {});
		}
	}
	// A portal that has lost a piece of its frame goes out.
	if (HasAuthority() && bCheckPortals && !bFixingPortals && Old != State && BlockWorld->HasChunk(ChunkPos) && !McPortal::IsPortalBlock(State))
	{
		bFixingPortals = true;
		McPortal::RemoveBroken(*BlockWorld, Pos, [this](const FIntVector& At, FMcStateId Block) { SetBlock(At, Block, false); });
		bFixingPortals = false;
	}
}

void AMcWorldActor::MulticastBlockBroken_Implementation(FIntVector Pos, int32 State)
{
	if (bViewed && OnBlockBroken)
	{
		OnBlockBroken(Pos, static_cast<FMcStateId>(State));
	}
}

void AMcWorldActor::MulticastBellRung_Implementation(FIntVector Pos, uint8 Side)
{
	if (bViewed && OnBellRung)
	{
		OnBellRung(Pos, Side);
	}
}

void AMcWorldActor::MulticastSpawnerCycled_Implementation(FIntVector Pos)
{
	if (bViewed && OnSpawnerCycled)
	{
		OnSpawnerCycled(Pos);
	}
}

void AMcWorldActor::SetShulkerOpeners(const TMap<FIntVector, int32>& Openers)
{
	for (const TPair<FIntVector, int32>& Pair : Openers)
	{
		if (ShulkerOpeners.FindRef(Pair.Key) != Pair.Value)
		{
			MulticastShulkerOpeners(Pair.Key, Pair.Value);
		}
	}
	for (const TPair<FIntVector, int32>& Pair : ShulkerOpeners)
	{
		if (!Openers.Contains(Pair.Key))
		{
			MulticastShulkerOpeners(Pair.Key, 0);
		}
	}
	ShulkerOpeners = Openers;
}

void AMcWorldActor::MulticastShulkerOpeners_Implementation(FIntVector Pos, int32 Count)
{
	if (BlockWorld && !ShulkerLids)
	{
		ShulkerLids = MakeUnique<FMcShulkerLids>(*BlockWorld);
	}
	if (ShulkerLids)
	{
		ShulkerLids->SetOpeners(Pos, Count);
	}
}

void AMcWorldActor::GetShulkerOpeners(const FIntPoint& Chunk, TArray<FIntVector>& Boxes, TArray<int32>& Openers) const
{
	for (const TPair<FIntVector, int32>& Pair : ShulkerOpeners)
	{
		if (Pair.Value > 0 && McCoords::BlockToChunk(Pair.Key.X) == Chunk.X && McCoords::BlockToChunk(Pair.Key.Z) == Chunk.Y)
		{
			Boxes.Add(Pair.Key);
			Openers.Add(Pair.Value);
		}
	}
}

void AMcWorldActor::ShowShulkerOpen(const FIntVector& Pos, int32 Count)
{
	if (BlockWorld && !ShulkerLids)
	{
		ShulkerLids = MakeUnique<FMcShulkerLids>(*BlockWorld);
	}
	if (ShulkerLids)
	{
		ShulkerLids->SetOpen(Pos, Count);
	}
}

void AMcWorldActor::MulticastSound_Implementation(FName Name, FVector_NetQuantize10 Position, float Volume, float Pitch)
{
	if (!bViewed)
	{
		return;
	}
	FMcSoundBus::Get().OnSound.Broadcast(FMcSoundEvent{ Name, FVector(Position), Volume, Pitch });
}

void AMcWorldActor::MulticastNote_Implementation(FVector_NetQuantize10 Position, int32 Note)
{
	if (bViewed)
	{
		FMcSoundBus::Get().OnNote.Broadcast(FVector(Position), Note);
	}
}

void AMcWorldActor::MulticastParticle_Implementation(FName Name, FVector_NetQuantize10 Position, FVector_NetQuantize10 Delta, float Speed, int32 Count)
{
	if (bViewed)
	{
		FMcSoundBus::Get().OnParticle.Broadcast(Name, FVector(Position), FVector(Delta), Speed, Count);
	}
}

void AMcWorldActor::MulticastRecord_Implementation(FName Name, FVector_NetQuantize10 Position, bool bPlay)
{
	if (!bViewed)
	{
		return;
	}
	FMcSoundEvent Event{ Name, FVector(Position), 4.0f, 1.0f };
	Event.bStop = !bPlay;
	FMcSoundBus::Get().OnSound.Broadcast(Event);
}

void AMcWorldActor::MulticastLightning_Implementation(FVector_NetQuantize10 Position, int32 BoltSeed)
{
	if (bViewed && OnLightning)
	{
		OnLightning(FVector(Position), BoltSeed);
	}
}

void AMcWorldActor::FeedPressurePlates()
{
	if (!Simulation || !Entities || !BlockWorld)
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	TMap<FIntVector, int32> Counts;
	// One entity of the box (feet at Bottom, Width across) on the plates it touches; bLiving: a mob or player.
	const auto Stand = [&](const FVector& Feet, float Width, bool bLiving)
	{
		const int32 Y = FMath::FloorToInt32(Feet.Y);
		const double Half = Width * 0.5;
		for (int32 X = FMath::FloorToInt32(Feet.X - Half); X <= FMath::FloorToInt32(Feet.X + Half); ++X)
		{
			for (int32 Z = FMath::FloorToInt32(Feet.Z - Half); Z <= FMath::FloorToInt32(Feet.Z + Half); ++Z)
			{
				const FIntVector Cell(X, Y, Z);
				const FMcStateId State = BlockWorld->GetBlock(Cell);
				if (!Registry.IsValidState(State))
				{
					continue;
				}
				const FString Name = Registry.GetBlockOf(State).Name.ToString();
				if (Name == TEXT("minecraft:tripwire"))
				{
					Counts.FindOrAdd(Cell) += 1;
					continue;
				}
				if (!Name.EndsWith(TEXT("_pressure_plate")))
				{
					continue;
				}
				const bool bStone = Name == TEXT("minecraft:stone_pressure_plate") || Name == TEXT("minecraft:polished_blackstone_pressure_plate");
				if (!bStone || bLiving)
				{
					Counts.FindOrAdd(Cell) += 1;
				}
			}
		}
	};
	for (const TUniquePtr<FMcEntity>& Entity : Entities->GetManager().GetEntities())
	{
		if (!Entity->bRemoved)
		{
			Stand(Entity->Body.Position, Entity->Body.Width, Entity->IsLiving());
		}
	}
	for (const FVector& Player : Entities->GetManager().PlayerPositions)
	{
		Stand(Player, 0.6f, true);
	}
	for (const TPair<FIntVector, int32>& Pair : Counts)
	{
		if (FedPlates.FindRef(Pair.Key) != Pair.Value)
		{
			Simulation->SetPressurePlate(Pair.Key, Pair.Value);
		}
	}
	for (const TPair<FIntVector, int32>& Pair : FedPlates)
	{
		if (!Counts.Contains(Pair.Key))
		{
			Simulation->SetPressurePlate(Pair.Key, 0);
		}
	}
	FedPlates = MoveTemp(Counts);
}

void AMcWorldActor::SyncFallingBlocks()
{
	if (!Simulation || !Entities)
	{
		return;
	}
	FMcEntityManager& Manager = Entities->GetManager();
	TSet<int32> Alive;
	for (const FMcSimulation::FFallingBlock& Block : Simulation->GetFallingBlocks())
	{
		Alive.Add(Block.Id);
		int32& EntityId = FallingEntities.FindOrAdd(Block.Id, 0);
		FMcFallingBlockEntity* Entity = EntityId != 0 ? static_cast<FMcFallingBlockEntity*>(Manager.Find(EntityId)) : nullptr;
		if (!Entity)
		{
			TUniquePtr<FMcFallingBlockEntity> Fresh = MakeUnique<FMcFallingBlockEntity>();
			Entity = Fresh.Get();
			Entity->Block = FMcBlockRegistry::Get().GetBlockOf(Block.State).Name;
			EntityId = Manager.Add(MoveTemp(Fresh)).Id;
		}
		// Position is the bottom centre of the block.
		Entity->Body.Position = FVector(Block.Column.X + 0.5, Block.Y, Block.Column.Z + 0.5);
	}
	for (auto It = FallingEntities.CreateIterator(); It; ++It)
	{
		if (!Alive.Contains(It.Key()))
		{
			if (FMcEntity* Gone = Manager.Find(It.Value()))
			{
				Gone->bRemoved = true;
			}
			It.RemoveCurrent();
		}
	}
}

void AMcWorldActor::MovePushedEntities()
{
	if (!Entities || PistonPushes.IsEmpty())
	{
		return;
	}
	for (FPistonPush& Push : PistonPushes)
	{
		++Push.Step;
		const FVector Shift = FVector(Push.Dir) * (0.5 * Push.Step);
		for (const FIntVector& Cell : Push.Cells)
		{
			const FBox Shape(FVector(Cell) + Shift, FVector(Cell) + FVector(1.0) + Shift);
			TArray<FMcEntity*> Hit;
			Entities->GetManager().FindInBox(Shape, Hit);
			for (FMcEntity* Entity : Hit)
			{
				const FBox Box = Entity->Body.GetBox();
				// Touching is not overlapping; what stands on a block moves only when the block rises.
				if (Entity->bRemoved || Entity->IsVehicle() || Box.Min.X >= Shape.Max.X - UE_KINDA_SMALL_NUMBER || Box.Max.X <= Shape.Min.X + UE_KINDA_SMALL_NUMBER
					|| Box.Min.Y >= Shape.Max.Y - UE_KINDA_SMALL_NUMBER || Box.Max.Y <= Shape.Min.Y + UE_KINDA_SMALL_NUMBER
					|| Box.Min.Z >= Shape.Max.Z - UE_KINDA_SMALL_NUMBER || Box.Max.Z <= Shape.Min.Z + UE_KINDA_SMALL_NUMBER)
				{
					continue;
				}
				McPhysics::Move(*BlockWorld, Entity->Body, FVector(Push.Dir) * 0.5, false);
			}
		}
	}
	PistonPushes.RemoveAll([](const FPistonPush& Push) { return Push.Step >= 2; });
}

void AMcWorldActor::TickLightning()
{
	if (Weather != static_cast<uint8>(EMcWeather::Thunder) || !HasWeather() || !BlockWorld)
	{
		return;
	}
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	TArray<FIntVector> Strikes;
	for (const auto& Pair : BlockWorld->GetChunks())
	{
		if (LightningRandom.RandHelper(100000) != 0)
		{
			continue;
		}
		const FMcChunk& Chunk = *Pair.Value;
		const int32 LocalX = LightningRandom.RandHelper(16);
		const int32 LocalZ = LightningRandom.RandHelper(16);
		// The block above the top one that stops movement or holds a fluid.
		int32 Y = Chunk.GetMaxY();
		for (; Y >= Chunk.GetMinY(); --Y)
		{
			const FMcStateId State = Chunk.GetBlock(LocalX, Y, LocalZ);
			if (Shapes.GetCollision(State).Num() > 0 || Shapes.GetFluid(State) != EMcFluid::None)
			{
				break;
			}
		}
		const int32 Above = FMath::Min(Y + 1, Chunk.GetMaxY());
		if (Biomes.GetPrecipitation(Chunk.GetBiome(LocalX, Above, LocalZ), Above) != FMcBiomeRegistry::EPrecipitation::Rain)
		{
			continue;
		}
		Strikes.Add(FIntVector(Pair.Key.X * 16 + LocalX, Y + 1, Pair.Key.Y * 16 + LocalZ));
	}
	for (const FIntVector& Pos : Strikes)
	{
		Strike(FVector(Pos.X + 0.5, Pos.Y, Pos.Z + 0.5));
	}
}

bool AMcWorldActor::UseBlock(const FIntVector& Pos, float Yaw)
{
	return HasAuthority() && Simulation && Simulation->UseBlock(Pos, Yaw);
}

bool AMcWorldActor::PlayNote(const FIntVector& Pos)
{
	return HasAuthority() && Simulation && Simulation->PlayNote(Pos);
}

bool AMcWorldActor::TuneNote(const FIntVector& Pos)
{
	return HasAuthority() && Simulation && Simulation->TuneNote(Pos);
}

void AMcWorldActor::SetWeather(EMcWeather NewWeather, int32 Duration)
{
	if (HasAuthority() && Simulation)
	{
		Simulation->SetWeather(NewWeather, Duration);
		Weather = static_cast<uint8>(NewWeather);
	}
}

void AMcWorldActor::Strike(const FVector& Position)
{
	UE_LOG(LogMcServer, Log, TEXT("Lightning struck %s at %s"), *Dimension, *Position.ToString());
	MulticastLightning(FVector_NetQuantize10(Position), LightningRandom.RandHelper(MAX_int32));
	if (Simulation)
	{
		const FVector Block = McCoords::UnrealToBlock(Position);
		Simulation->LightningStruck(FIntVector(FMath::FloorToInt32(Block.X), FMath::FloorToInt32(Block.Y), FMath::FloorToInt32(Block.Z)));
	}
	// The thunder carries everywhere (its volume makes its range huge); the crack is near.
	MulticastSound(FName("entity.lightning_bolt.thunder"), FVector_NetQuantize10(Position), 10000.0f, 0.8f + LightningRandom.FRand() * 0.2f);
	MulticastSound(FName("entity.lightning_bolt.impact"), FVector_NetQuantize10(Position), 2.0f, 0.5f + LightningRandom.FRand() * 0.2f);

	// What stands within reach is hurt: mobs within three blocks, and the nearest player.
	if (Entities)
	{
		FMcEntityManager& Manager = Entities->GetManager();
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (Entity->IsLiving() && !Entity->bRemoved && FVector::Dist(Entity->Body.Position, Position) <= 3.0)
			{
				static_cast<FMcMob&>(*Entity).Damage(5.0f, nullptr);
			}
		}
		Manager.HurtPlayer(Position, 5.0f);
	}

	// Fire where it struck and on a few blocks round it (Normal difficulty), wherever there is air over something solid.
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId Fire = Registry.FindDefaultState(TEXT("minecraft:fire")).Get(0);
	const FIntVector Centre(FMath::FloorToInt32(Position.X), FMath::FloorToInt32(Position.Y), FMath::FloorToInt32(Position.Z));
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	for (int32 Try = 0; Try < 5 && Fire != 0; ++Try)
	{
		const FIntVector Pos = Try == 0 ? Centre : Centre + FIntVector(LightningRandom.RandRange(-1, 1), LightningRandom.RandRange(-1, 1), LightningRandom.RandRange(-1, 1));
		if (Registry.IsAir(BlockWorld->GetBlock(Pos)) && Shapes.GetCollision(BlockWorld->GetBlock(Pos - FIntVector(0, 1, 0))).Num() > 0)
		{
			SetBlock(Pos, Fire);
		}
	}
}

void AMcWorldActor::ApplyEdits(FMcChunk& Chunk)
{
	const FIntPoint Pos = Chunk.GetPos();
	const TMap<FIntVector, FMcStateId>* ChunkEdits = Edits.Find(Pos);
	if (!ChunkEdits)
	{
		return;
	}
	for (const TPair<FIntVector, FMcStateId>& Edit : *ChunkEdits)
	{
		Chunk.SetBlock(McCoords::BlockToLocal(Edit.Key.X), Edit.Key.Y, McCoords::BlockToLocal(Edit.Key.Z), Edit.Value);
	}
	// Without a save, the edits are what survives the chunk unloading, so they stay.
	if (Storage)
	{
		Edits.Remove(Pos);
	}
}

void AMcWorldActor::AddToWorld(FFinishedChunk Arrived)
{
	const FIntPoint Pos = Arrived.Chunk->GetPos();
	// Before the edits, so a chunk that only changes by them counts as changed.
	const uint32 LoadedRevision = Arrived.Chunk->GetRevision();
	ApplyEdits(*Arrived.Chunk);
	if (Entities && Arrived.bFromDisk)
	{
		// Its animals were placed when it was first made; what's left of them is in the save.
		Entities->MarkSeeded(Pos);
	}
	// What the structures here spawn comes back with the chunk.
	FMcStructureSpawns::Get().DecodeChunk(Arrived.Extras.Structures);
	BlockWorld->AddChunk(MoveTemp(Arrived.Chunk));
	++(Arrived.bFromDisk ? NumChunksFromDisk : NumChunksGenerated);
	if (Storage)
	{
		FChunkState& State = ChunkStates.FindOrAdd(Pos);
		State = FChunkState();
		State.SavedRevision = LoadedRevision;
		State.bOnDisk = Arrived.bFromDisk;
		State.bHadEntities = !Arrived.Extras.Entities.IsEmpty();
	}
	if (Pos == FIntPoint(0, 0))
	{
		UE_LOG(LogMcServer, Display, TEXT("Chunk 0, 0 arrived with %d bytes of entities (host %d, from disk %d)"), Arrived.Extras.Entities.Num(), Entities ? 1 : 0, Arrived.bFromDisk ? 1 : 0);
	}
	if (Entities && !Arrived.Extras.Entities.IsEmpty())
	{
		Entities->RestoreChunk(Arrived.Extras.Entities);
	}
	// Banners that came from a Java world keep their patterns in a kept block entity; the flag is made from it once.
	if (Entities)
	{
		if (const FMcChunk* Added = BlockWorld->FindChunk(Pos))
		{
			McJavaImport::StandBanners(*Added, Entities->GetManager());
		}
	}
}

void AMcWorldActor::MarkChunkDirty(FIntPoint Chunk)
{
	if (FChunkState* State = ChunkStates.Find(Chunk))
	{
		State->bDirty = true;
	}
}

bool AMcWorldActor::SaveChunk(FIntPoint Pos)
{
	const FMcChunk* Chunk = Storage && BlockWorld ? BlockWorld->FindChunk(Pos) : nullptr;
	if (!Chunk)
	{
		return false;
	}
	FChunkState& State = ChunkStates.FindOrAdd(Pos);
	// Taken before the chunk leaves the world, which removes its mobs.
	FMcChunkSaveExtras Extras;
	Extras.bPopulated = true;
	if (Entities)
	{
		Extras.Entities = Entities->CaptureChunk(Pos);
	}
	Extras.Structures = FMcStructureSpawns::Get().EncodeChunk(Pos);
	// Block entities can be changed in place without the chunk's revision moving, so a chunk with any is always rewritten.
	const bool bNeeded = !State.bOnDisk || State.bDirty || Chunk->GetRevision() != State.SavedRevision
		|| !Chunk->GetBlockEntities().IsEmpty() || !Extras.Entities.IsEmpty() || State.bHadEntities;
	if (!bNeeded)
	{
		return false;
	}
	const bool bHasEntities = !Extras.Entities.IsEmpty();
	TArray<uint8> Bytes = McChunkSave::Encode(*Chunk, Extras);
	if (Bytes.IsEmpty())
	{
		UE_LOG(LogMcServer, Error, TEXT("Can't save chunk %d,%d: encoding it failed."), Pos.X, Pos.Y);
		return false;
	}
	Storage->QueueSave(Pos, MoveTemp(Bytes));
	State.SavedRevision = Chunk->GetRevision();
	State.bOnDisk = true;
	State.bDirty = false;
	State.bHadEntities = bHasEntities;
	return true;
}

void AMcWorldActor::SaveWorld()
{
	if (!Storage || !BlockWorld)
	{
		return;
	}
	OnSaving.Broadcast();
	if (!bSecondary)
	{
		Level.DayTime = DayTicks;
		Level.Dimension = Dimension;
		Level.GameRules.Add(TEXT("doDaylightCycle"), bTimeFrozen ? TEXT("false") : TEXT("true"));
		McWorldSave::SaveLevel(WorldDir, Level);
		McWorldSave::SaveMaps(WorldDir);
	}
	// Whatever an autosave had left to look at is covered by this.
	AutosaveQueue.Reset();
	int32 Queued = 0;
	for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Entry : BlockWorld->GetChunks())
	{
		Queued += SaveChunk(Entry.Key) ? 1 : 0;
	}
	Storage->Flush();
	UE_LOG(LogMcServer, Log, TEXT("Saved world %s: %d of %d chunks had changed%s; this session %d chunks came from the save and %d were generated"),
		*WorldName, Queued, BlockWorld->GetChunks().Num(),
		Storage->NumFailedWrites() > 0 ? *FString::Printf(TEXT(" (%d writes failed)"), Storage->NumFailedWrites()) : TEXT(""),
		NumChunksFromDisk, NumChunksGenerated);
}

void AMcWorldActor::BeginAutosave()
{
	OnSaving.Broadcast();
	if (!bSecondary)
	{
		Level.DayTime = DayTicks;
		Level.Dimension = Dimension;
		Level.GameRules.Add(TEXT("doDaylightCycle"), bTimeFrozen ? TEXT("false") : TEXT("true"));
		McWorldSave::SaveLevel(WorldDir, Level);
		McWorldSave::SaveMaps(WorldDir);
	}
	if (Entities)
	{
		Entities->RemoveStrays();
	}
	BlockWorld->GetChunks().GetKeys(AutosaveQueue);
}

void AMcWorldActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Both sides count: the server for everyone, a client between syncs. The server publishes whole ticks once a
	// second.
	GameTicks += DeltaSeconds * 20.0;
	if (HasAuthority() && Simulation && FMath::Abs(GameTicks - static_cast<double>(Simulation->GetGameTime())) > 4.0)
	{
		GameTicks = static_cast<double>(Simulation->GetGameTime());
	}
	if (HasAuthority() && FMath::FloorToDouble(GameTicks) > static_cast<double>(SyncedGameTime) + 19.0)
	{
		SyncedGameTime = static_cast<int64>(FMath::FloorToDouble(GameTicks));
	}
	if (!bTimeFrozen)
	{
		const double Ticks = DeltaSeconds * 20.0;
		DayTicks += Ticks;
		TicksSinceSync += Ticks;
		if (HasAuthority() && TicksSinceSync >= 20.0)
		{
			TicksSinceSync = 0.0;
			SyncedDayTime = static_cast<int64>(FMath::FloorToDouble(DayTicks));
		}
	}
	// The weather: the server says what it is, and every machine eases towards it a hundredth a tick.
	if (HasAuthority() && Simulation)
	{
		Weather = static_cast<uint8>(Simulation->GetWeather());
	}
	WeatherRemainder = FMath::Min(WeatherRemainder + DeltaSeconds, 0.25);
	for (; WeatherRemainder >= 0.05; WeatherRemainder -= 0.05)
	{
		const bool bWeather = HasWeather();
		const float RainTarget = bWeather && Weather != static_cast<uint8>(EMcWeather::Clear) ? 1.0f : 0.0f;
		const float ThunderTarget = bWeather && Weather == static_cast<uint8>(EMcWeather::Thunder) ? 1.0f : 0.0f;
		RainLevel = FMath::FInterpConstantTo(RainLevel, RainTarget, 1.0f, 0.01f);
		ThunderLevel = FMath::FInterpConstantTo(ThunderLevel, ThunderTarget, 1.0f, 0.01f);
	}
	if (BlockWorld && Generator)
	{
		StreamChunks();
	}
	if (BlockWorld && !ShulkerLids)
	{
		ShulkerLids = MakeUnique<FMcShulkerLids>(*BlockWorld);
	}
	LidRemainder = FMath::Min(LidRemainder + DeltaSeconds, 0.25);
	for (; ShulkerLids && LidRemainder >= 0.05; LidRemainder -= 0.05)
	{
		ShulkerLids->Tick();
		// The server moves the mobs and items in an opening lid's way; each player's own machine moves them.
		if (HasAuthority() && Entities)
		{
			for (const TUniquePtr<FMcEntity>& Entity : Entities->GetManager().GetEntities())
			{
				const FVector Push = ShulkerLids->GetPush(Entity->Body.GetBox());
				if (!Entity->bRemoved && !Push.IsZero())
				{
					McPhysics::Move(*BlockWorld, Entity->Body, Push, false);
				}
			}
		}
	}
	if (Simulation)
	{
		// Fixed 20 ticks a second however the frames fall; after a long hitch, catch up at most a few ticks.
		TickRemainder = FMath::Min(TickRemainder + DeltaSeconds, 0.25);
		for (; TickRemainder >= 0.05; TickRemainder -= 0.05)
		{
			Simulation->Tick();
			MovePushedEntities();
			SyncFallingBlocks();
			FeedPressurePlates();
			TickLightning();
			// Development: -McStrike=x,z strikes the top of that column every three seconds.
			if (!DevStrike.IsEmpty() && ++DevStrikeTicks % 60 == 0)
			{
				FString XText, ZText;
				if (DevStrike.Split(TEXT(","), &XText, &ZText))
				{
					const int32 X = FCString::Atoi(*XText);
					const int32 Z = FCString::Atoi(*ZText);
					int32 Y = BlockWorld->GetDimension().MaxY();
					while (Y > BlockWorld->GetDimension().MinY && FMcBlockRegistry::Get().IsAir(BlockWorld->GetBlock(FIntVector(X, Y, Z))))
					{
						--Y;
					}
					Strike(FVector(X + 0.5, Y + 1, Z + 0.5));
				}
			}
			TArray<FVector> Players;
			TArray<FVector> Looks;
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (const APawn* Pawn = It->IsValid() && IsPlayerHere(*It->Get()) ? It->Get()->GetPawn() : nullptr)
				{
					Players.Add(McCoords::UnrealToBlock(Pawn->GetActorLocation()));
					const FVector EyeUnreal = Pawn->GetActorLocation();
					Looks.Add((McCoords::UnrealToBlock(EyeUnreal + It->Get()->GetControlRotation().Vector()) - McCoords::UnrealToBlock(EyeUnreal)).GetSafeNormal());
				}
			}
			Entities->GetManager().PlayerLooks = Looks;
			// The actors made for this world's mobs and items are its own (clients hide the ones of the dimension they are not in).
			const FDelegateHandle Claim = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateWeakLambda(this, [this](AActor* Spawned)
			{
				if (Spawned && !Spawned->GetOwner())
				{
					Spawned->SetOwner(this);
				}
			}));
			// It rains, for the world's rules (a fishing bobber's bite comes sooner), once the rain is more than a fifth on.
			Entities->GetManager().bRaining = RainLevel > 0.2f;
			// Where the time never changes it is always night to the mobs (none burn).
			Entities->Tick(Players, BlockWorld->GetDimension().bHasFixedTime ? 18000.0 : DayTicks);
			GetWorld()->RemoveOnActorSpawnedHandler(Claim);
			if (Storage && ++TicksSinceAutosave >= AutosaveIntervalTicks)
			{
				TicksSinceAutosave = 0;
				BeginAutosave();
			}
		}
	}
	// An autosave looks at a few chunks a frame: each changed one is encoded here (the file writes are on a worker),
	// and doing a whole world's in one frame would be a hitch.
	for (int32 Budget = 8; Budget > 0 && !AutosaveQueue.IsEmpty(); --Budget)
	{
		SaveChunk(AutosaveQueue.Pop(EAllowShrinking::No));
	}
}

void AMcWorldActor::StreamChunks()
{
	const double ChunkUnits = McCoords::SectionSize * McCoords::UnitsPerBlock;

	// Load one ring past the view distance: the renderer only meshes chunks whose neighbours are all loaded.
	// Decorating needs two more rings generated around that.
	const int32 LoadRadius = ViewDistance + 1 + (Features ? 2 : 0);
	// Keep a margin before unloading so flying back and forth over a border doesn't churn.
	const int32 KeepRadius = ViewDistance + 3;

	// The chunks the players are in, then the anchors, each with how far around it chunks are wanted.
	TArray<FIntPoint> Centres;
	TArray<int32> Radii;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const APawn* Pawn = Controller && IsPlayerHere(*Controller) ? Controller->GetPawn() : nullptr;
		if (Pawn)
		{
			const FVector Location = Pawn->GetActorLocation();
			Centres.Add(FIntPoint(FMath::FloorToInt32(Location.X / ChunkUnits), FMath::FloorToInt32(Location.Y / ChunkUnits)));
			Radii.Add(LoadRadius);
		}
	}
	const int32 NumPlayers = Centres.Num();
	for (const TPair<int32, FAnchor>& Anchor : Anchors)
	{
		Centres.Add(Anchor.Value.Chunk);
		Radii.Add(Anchor.Value.Radius + 1 + (Features ? 2 : 0));
	}

	// With nobody to measure from, leave the world as it is: unloading now would drop every chunk (and, with them,
	// the mobs), only to generate it all again when the first pawn appears (a listen server's start, a death).
	const bool bAnyone = !Centres.IsEmpty();

	// Nothing has moved and nothing was left outstanding: the world is already what these players need.
	if (bAnyone && Centres == SettledCentres && Radii == SettledRadii && ViewDistance == SettledViewDistance)
	{
		return;
	}

	// Blocks within 6 chunks of a player get random ticks.
	if (Simulation)
	{
		TSet<FIntPoint> Ticking;
		for (int32 Index = 0; Index < NumPlayers; ++Index)
		{
			const FIntPoint& Centre = Centres[Index];
			for (int32 DZ = -6; DZ <= 6; ++DZ)
			{
				for (int32 DX = -6; DX <= 6; ++DX)
				{
					Ticking.Add(Centre + FIntPoint(DX, DZ));
				}
			}
		}
		Simulation->SetTickingChunks(Ticking.Array());
	}

	TArray<TPair<int32, FIntPoint>> Missing;
	for (int32 Index = 0; Index < Centres.Num(); ++Index)
	{
		const FIntPoint& Centre = Centres[Index];
		const int32 Around = Radii[Index];
		for (int32 DZ = -Around; DZ <= Around; ++DZ)
		{
			for (int32 DX = -Around; DX <= Around; ++DX)
			{
				const FIntPoint Pos = Centre + FIntPoint(DX, DZ);
				if (!BlockWorld->HasChunk(Pos) && !Pending.Contains(Pos))
				{
					Missing.Add({ DX * DX + DZ * DZ, Pos });
				}
			}
		}
	}
	Missing.Sort([](const TPair<int32, FIntPoint>& A, const TPair<int32, FIntPoint>& B) { return A.Key < B.Key; });

	bool bProgress = AddGeneratedChunks();
	bProgress |= DecorateChunks();

	for (const TPair<int32, FIntPoint>& Entry : Missing)
	{
		if (InFlight.Num() >= MaxChunksInFlight)
		{
			break;
		}
		// Two players can want the same chunk.
		if (InFlight.Contains(Entry.Value) || BlockWorld->HasChunk(Entry.Value) || Pending.Contains(Entry.Value))
		{
			continue;
		}
		InFlight.Add(Entry.Value);
		// A chunk in the save is read back instead of generated (on the worker, along with the decoding).
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [Generator = Generator, Finished = Finished, Storage = Storage, Chunk = BlockWorld->MakeChunk(Entry.Value)]() mutable
		{
			FFinishedChunk Result;
			Result.Chunk = MoveTemp(Chunk);
			TArray<uint8> Bytes;
			if (Storage && Storage->Load(Result.Chunk->GetPos(), Bytes) && McChunkSave::Decode(Bytes, *Result.Chunk, Result.Extras))
			{
				Result.bFromDisk = true;
			}
			else
			{
				// Never saved, or damaged (Decode leaves the chunk as it was): make it as a new one.
				Result.Extras = FMcChunkSaveExtras();
				Generator->Generate(*Result.Chunk);
			}
			Finished->Enqueue(MoveTemp(Result));
		});
	}

	bool bUnloaded = false;
	if (bAnyone)
	{
		TArray<FIntPoint> Far;
		for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Chunk : BlockWorld->GetChunks())
		{
			bool bNearAnyone = false;
			for (int32 Index = 0; Index < Centres.Num() && !bNearAnyone; ++Index)
			{
				const int32 Keep = FMath::Max(KeepRadius, Radii[Index]);
				bNearAnyone = FMath::Abs(Chunk.Key.X - Centres[Index].X) <= Keep && FMath::Abs(Chunk.Key.Y - Centres[Index].Y) <= Keep;
			}
			if (!bNearAnyone)
			{
				Far.Add(Chunk.Key);
			}
		}
		for (const FIntPoint& Pos : Far)
		{
			// Saved while it's still whole, with its mobs; the writing happens on a worker.
			SaveChunk(Pos);
			ChunkStates.Remove(Pos);
			BlockWorld->RemoveChunk(Pos);
		}
		bUnloaded = !Far.IsEmpty();
		for (auto It = Pending.CreateIterator(); It; ++It)
		{
			const FIntPoint Pos = It.Key();
			bool bNearAnyone = false;
			for (int32 Index = 0; Index < Centres.Num() && !bNearAnyone; ++Index)
			{
				const int32 Keep = FMath::Max(KeepRadius, Radii[Index]) + 2;
				bNearAnyone = FMath::Abs(Pos.X - Centres[Index].X) <= Keep && FMath::Abs(Pos.Y - Centres[Index].Y) <= Keep;
			}
			if (!bNearAnyone)
			{
				Decorated.Remove(Pos);
				It.RemoveCurrent();
				bUnloaded = true;
			}
		}
	}

	// Nothing left to load, generate, decorate or unload for these players: skip the work until they move.
	const bool bSettled = bAnyone && !bProgress && !bUnloaded && Missing.IsEmpty() && InFlight.IsEmpty();
	SettledCentres = bSettled ? Centres : TArray<FIntPoint>();
	SettledRadii = bSettled ? Radii : TArray<int32>();
	SettledViewDistance = ViewDistance;
}

bool AMcWorldActor::AddGeneratedChunks()
{
	bool bAny = false;
	FFinishedChunk Result;
	while (Finished->Dequeue(Result))
	{
		bAny = true;
		const FIntPoint Pos = Result.Chunk->GetPos();
		InFlight.Remove(Pos);
		if (BlockWorld->HasChunk(Pos) || Pending.Contains(Pos))
		{
			continue;
		}
		// A chunk from the save has had its features already; one just made waits for them.
		if (Features && !(Result.bFromDisk && Result.Extras.bPopulated))
		{
			Pending.Add(Pos, MoveTemp(Result.Chunk));
			continue;
		}
		AddToWorld(MoveTemp(Result));
	}
	return bAny;
}

bool AMcWorldActor::DecorateChunks()
{
	if (!Features || Pending.IsEmpty())
	{
		return false;
	}
	bool bChanged = false;
	// The chunks around Pos, row by row from the north-west, and which of them players already have.
	const auto Gather = [this](FIntPoint Pos, FMcChunk** Out, bool* bLive)
	{
		bool bAll = true;
		for (int32 Index = 0; Index < 9; ++Index)
		{
			const FIntPoint At = Pos + FIntPoint(Index % 3 - 1, Index / 3 - 1);
			const TUniquePtr<FMcChunk>* Waiting = Pending.Find(At);
			Out[Index] = Waiting ? Waiting->Get() : BlockWorld->FindChunk(At);
			bLive[Index] = !Waiting && Out[Index];
			bAll &= Out[Index] != nullptr;
		}
		return bAll;
	};

	const double Deadline = FPlatformTime::Seconds() + 0.006;
	for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Entry : Pending)
	{
		if (FPlatformTime::Seconds() > Deadline)
		{
			break;
		}
		if (Decorated.Contains(Entry.Key))
		{
			continue;
		}
		FMcChunk* Around[9];
		bool bLive[9];
		if (!Gather(Entry.Key, Around, bLive))
		{
			continue;
		}
		FMcGenRegion Region(Entry.Key, Around);
		for (int32 Index = 0; Index < 9; ++Index)
		{
			if (bLive[Index])
			{
				Region.SetReadOnly(Index);
			}
		}
		Features->Decorate(Region, static_cast<uint64>(Seed));
		Decorated.Add(Entry.Key);
		bChanged = true;
	}

	// Decorated chunks whose neighbours are all decorated (or already in the world) are finished.
	TArray<FIntPoint> Ready;
	for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Entry : Pending)
	{
		bool bDone = Decorated.Contains(Entry.Key);
		for (int32 Index = 0; Index < 9 && bDone; ++Index)
		{
			const FIntPoint At = Entry.Key + FIntPoint(Index % 3 - 1, Index / 3 - 1);
			bDone = Decorated.Contains(At) || BlockWorld->HasChunk(At);
		}
		if (bDone)
		{
			Ready.Add(Entry.Key);
		}
	}
	for (const FIntPoint& Pos : Ready)
	{
		FFinishedChunk Done;
		Done.Chunk = MoveTemp(Pending.FindChecked(Pos));
		Pending.Remove(Pos);
		Decorated.Remove(Pos);
		AddToWorld(MoveTemp(Done));
		bChanged = true;
	}
	return bChanged;
}

bool AMcWorldActor::FindNearestStronghold(const FVector& From, FVector2D& OutCorner) const
{
	FIntPoint Corner;
	if (!Features.IsValid() || !BlockWorld || BlockWorld->GetDimension().Name != TEXT("minecraft:overworld")
		|| !Features->FindNearestStronghold(static_cast<uint64>(Seed), From.X, From.Z, Corner))
	{
		return false;
	}
	OutCorner = FVector2D(Corner.X, Corner.Y);
	return true;
}

bool AMcWorldActor::FindGatewayExit(int32 X, int32 Z, FVector& OutFeet) const
{
	if (!Generator.IsValid())
	{
		return false;
	}
	const double Length = FMath::Sqrt(static_cast<double>(X) * X + static_cast<double>(Z) * Z);
	if (Length < 1.0)
	{
		return false;
	}
	const double Dx = X / Length, Dz = Z / Length;
	const auto HasBlocksAbove15 = [&](double Dist)
	{
		const int32 Cx = FMath::FloorToInt32(Dx * Dist) >> 4, Cz = FMath::FloorToInt32(Dz * Dist) >> 4;
		for (int32 Bx = 0; Bx < 16; Bx += 3)
		{
			for (int32 Bz = 0; Bz < 16; Bz += 3)
			{
				if (Generator->FindSolidTopY(Cx * 16 + Bx, Cz * 16 + Bz) > 15)
				{
					return true;
				}
			}
		}
		return false;
	};
	double Dist = 1024.0;
	if (HasBlocksAbove15(Dist))
	{
		while (Dist > 768.0 && HasBlocksAbove15(Dist))
		{
			Dist -= 16.0;
		}
	}
	else
	{
		while (Dist < 1280.0 && !HasBlocksAbove15(Dist))
		{
			Dist += 16.0;
		}
	}
	const int32 Cx = FMath::FloorToInt32(Dx * Dist) >> 4, Cz = FMath::FloorToInt32(Dz * Dist) >> 4;
	int32 BestY = INT32_MIN, BestX = 0, BestZ = 0;
	for (int32 Bx = 0; Bx < 16; ++Bx)
	{
		for (int32 Bz = 0; Bz < 16; ++Bz)
		{
			const int32 Top = Generator->FindSolidTopY(Cx * 16 + Bx, Cz * 16 + Bz);
			if (Top > BestY)
			{
				BestY = Top;
				BestX = Cx * 16 + Bx;
				BestZ = Cz * 16 + Bz;
			}
		}
	}
	if (BestY < 30)
	{
		// No island in reach: a small one would be made at height 75 in the game; here the middle of the chunk, high, to be landed on.
		return false;
	}
	OutFeet = FVector(BestX + 0.5, BestY + 1.0, BestZ + 0.5);
	return true;
}
