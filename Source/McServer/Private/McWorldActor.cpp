#include "McWorldActor.h"
#include "McChestLink.h"
#include "McBannerEntity.h"
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
#include "McServer.h"
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
	Entities = MakeUnique<FMcEntityHost>(*BlockWorld, Seed);
	Entities->SetProxyClass(GetWorld(), EntityProxyClass);
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
	// Every sound the entities make goes to the players.
	Entities->GetManager().OnSound = [this](const FMcSoundEvent& Event)
	{
		MulticastSound(Event.Name, FVector_NetQuantize10(Event.Position), Event.Volume, Event.Pitch);
	};
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
}

void AMcWorldActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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

bool AMcWorldActor::AttackMobs(const FVector& Eye, const FVector& Look, float Reach, float Damage, bool bCreative)
{
	return Entities && Entities->Attack(Eye, Look, Reach, Damage, bCreative);
}

int32 AMcWorldActor::FindVehicle(const FVector& Eye, const FVector& Look, float Reach)
{
	const FMcEntity* Found = Entities ? Entities->FindVehicle(Eye, Look, Reach) : nullptr;
	return Found ? Found->Id : 0;
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

void AMcWorldActor::MulticastSound_Implementation(FName Name, FVector_NetQuantize10 Position, float Volume, float Pitch)
{
	if (!bViewed)
	{
		return;
	}
	FMcSoundBus::Get().OnSound.Broadcast(FMcSoundEvent{ Name, FVector(Position), Volume, Pitch });
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
	if (Entities && !Arrived.Extras.Entities.IsEmpty())
	{
		Entities->RestoreChunk(Arrived.Extras.Entities);
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
	if (Simulation)
	{
		// Fixed 20 ticks a second however the frames fall; after a long hitch, catch up at most a few ticks.
		TickRemainder = FMath::Min(TickRemainder + DeltaSeconds, 0.25);
		for (; TickRemainder >= 0.05; TickRemainder -= 0.05)
		{
			Simulation->Tick();
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
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (const APawn* Pawn = It->IsValid() && IsPlayerHere(*It->Get()) ? It->Get()->GetPawn() : nullptr)
				{
					Players.Add(McCoords::UnrealToBlock(Pawn->GetActorLocation()));
				}
			}
			// The actors made for this world's mobs and items are its own (clients hide the ones of the dimension they are not in).
			const FDelegateHandle Claim = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateWeakLambda(this, [this](AActor* Spawned)
			{
				if (Spawned && !Spawned->GetOwner())
				{
					Spawned->SetOwner(this);
				}
			}));
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
