// Hosts one dimension: owns its blocks, generates chunks around the players and unloads the rest. Replicated
// so clients have one too: theirs starts empty and is filled by McNet's chunk streaming.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "McBlockWorld.h"
#include "Containers/Queue.h"
#include "McChunkGenerator.h"
#include "McChunkSave.h"
#include "McEntityHost.h"
#include "McSimulation.h"
#include "McShulkerLids.h"
#include "McWorldSave.h"
#include "GameFramework/Controller.h"
#include "McWorldActor.generated.h"

class FMcFeatureLibrary;
class FMcWorldStorage;
namespace McJavaImport { class FLiveImport; }

UCLASS()
class MCSERVER_API AMcWorldActor : public AActor
{
	GENERATED_BODY()

public:
	AMcWorldActor();
	virtual ~AMcWorldActor() override;

	// Dimension type id, from the jar's dimension_type/. Sent to clients when the actor is made, so the one they build
	// is the same dimension.
	UPROPERTY(EditAnywhere, Replicated, Category = "Mc")
	FString Dimension = TEXT("minecraft:overworld");

	// A world made for a dimension besides the first (the Nether, the End), after the first one: it takes the seed, the
	// save and the terrain settings of the first (see InheritFrom) and never reads the command line's world options or
	// writes the level file. Set before BeginPlay, and sent to clients with the actor.
	UPROPERTY(Replicated)
	bool bSecondary = false;
	// Copies the seed, save folder and flat settings of the first world. Flat worlds have normal Nether and End terrain.
	void InheritFrom(const AMcWorldActor& Primary);

	// Which players this world looks after (loads chunks around, tells mobs about): all of them when empty. The game
	// mode sets it when it has more than one world, to the ones in this dimension.
	TFunction<bool(const AController&)> PlayerFilter;

	// Keeps the chunks within Radius of a chunk loaded (and generating) while nobody is there, as when a portal
	// trip waits for its destination. Returns an id for RemoveAnchor.
	int32 AddAnchor(FIntPoint Chunk, int32 Radius);
	void RemoveAnchor(int32 Id);
	// Whether every chunk within Radius of Chunk is in the world.
	bool AreChunksLoaded(FIntPoint Chunk, int32 Radius) const;

	// Terrain from the jar's worldgen/noise_settings/, unless the world is flat.
	UPROPERTY(EditAnywhere, Category = "Mc")
	FString NoiseSettings = TEXT("minecraft:overworld");

	// Flat worlds use a preset from the jar's worldgen/flat_level_generator_preset/. The command line can
	// ask for one with -McFlat (dev scenes with -McScene always are).
	UPROPERTY(EditAnywhere, Category = "Mc")
	bool bFlat = false;

	UPROPERTY(EditAnywhere, Category = "Mc")
	FString FlatPreset = TEXT("minecraft:classic_flat");

	// The world seed; 0 picks one at random. -McSeed=<number> sets it.
	UPROPERTY(EditAnywhere, Category = "Mc")
	int64 Seed = 0;

	// Chunks drawn around each player, like the View Distance video setting.
	UPROPERTY(EditAnywhere, Category = "Mc", meta = (ClampMin = 2, ClampMax = 32))
	int32 ViewDistance = 12;

	// Chunks being generated on worker threads at once.
	UPROPERTY(EditAnywhere, Category = "Mc", meta = (ClampMin = 1))
	int32 MaxChunksInFlight = 16;

	// Null until BeginPlay has loaded the registry, the dimension and the preset from cache/.
	FMcBlockWorld* GetBlockWorld() const { return BlockWorld.Get(); }
	// A player put the block at Pos down by hand (a hook then looks along its tripwire line at once).
	void PlayerPlaced(const FIntVector& Pos);
	// A player is breaking the block at Pos (it is still there).
	void PlayerBroke(const FIntVector& Pos);

	// World height of the top block at the spawn column, for spawning players above it. Under a ceiling (the Nether)
	// it is a floor with room to stand, and the column may be off 0, 0.
	int32 GetSurfaceY() const { return SurfaceY; }
	FIntPoint GetSpawnColumn() const { return SpawnColumn; }

	// The time of day in game ticks (20 a second, 24000 a day; 0 is sunrise, 6000 noon, 12000 sunset, 18000 midnight),
	// not wrapped: the day count, and with it the moon's phase, is DayTime / 24000. The server counts it and tells
	// clients once a second, as the game's time packet does; clients run their own clock in between. Fractional,
	// so the sky moves smoothly between ticks.
	double GetDayTime() const { return DayTicks; }
	// Ticks since the world began, which /time set and the daylight rule don't touch (the clouds drift by it). Counted
	// on every machine and corrected from the server's once a second.
	double GetGameTicks() const { return GameTicks; }
	// -McTime=<ticks> sets where a new world starts (default 1000, the start of the day); -McTimeFreeze stops time.
	void SetDayTime(double Ticks);
	bool IsTimeFrozen() const { return bTimeFrozen; }
	void SetTimeFrozen(bool bFrozen) { bTimeFrozen = bFrozen; }

	// How hard it is raining and thundering, 0 to 1, on every machine: they ease a hundredth a tick towards the server's
	// weather, so a storm comes on over five seconds. Thunder counts only while it rains. Always 0 where there is no
	// weather (no sky light, or a ceiling). -McWeather=clear|rain|thunder starts a new world's weather at full.
	float GetRainLevel() const { return RainLevel; }
	// Server: sets the weather (/weather) for Duration ticks, or as long as the weather picks when 0. It eases in as usual.
	void SetWeather(EMcWeather NewWeather, int32 Duration);
	// Server: ticks this world has run (/time query gametime).
	int64 GetGameTime() const { return Simulation ? Simulation->GetGameTime() : 0; }
	float GetThunderLevel() const { return ThunderLevel * RainLevel; }

	// The actor class shown for each server-side entity (set by the game mode; it implements IMcEntityProxy).
	void SetEntityProxyClass(TSubclassOf<AActor> Class);
	FMcEntityHost* GetEntityHost() const { return Entities.Get(); }
	// The block simulation (fluids, redstone). Server only; null until the world starts.
	FMcSimulation* GetSimulation() const { return Simulation.Get(); }
	// Moves what pistons that started pushing sweep along; the server tick runs it after each simulation tick (tests that
	// tick the simulation themselves call it too).
	void MovePushedEntities();
	// A player's swing from Eye along Look: hurts the mob it reaches. True if one was hit.
	bool AttackMobs(const FVector& Eye, const FVector& Look, float Reach, float Damage, bool bCreative = false);
	// The id of the nearest vehicle along the ray, or 0.
	int32 FindVehicle(const FVector& Eye, const FVector& Look, float Reach);
	// The nearest living mob along the ray, or null.
	class FMcMob* FindMob(const FVector& Eye, const FVector& Look, float Reach);

	// Changes a block, loaded or not. A change to a chunk that isn't loaded is kept and put on it when it arrives. In
	// a world without a save (no -McWorld) changes to loaded chunks are kept too, so they survive unloading; in a
	// saved world the chunk's save keeps them. A portal beside Pos that this leaves without its frame goes out
	// unless bCheckPortals is false (which the code that builds portals uses, as it is part way through).
	void SetBlock(const FIntVector& Pos, FMcStateId State, bool bCheckPortals = true);

	// Server: a player's hand on the block at Pos flips a lever, presses a button, or opens or shuts a door, trapdoor
	// or fence gate (see FMcSimulation::UseBlock). Yaw is where they look. True if the block took the click.
	bool UseBlock(const FIntVector& Pos, float Yaw);

	// Server: a note block at Pos is struck (a punch) or tuned and played (a use); see FMcSimulation::PlayNote, TuneNote.
	bool PlayNote(const FIntVector& Pos);
	bool TuneNote(const FIntVector& Pos);

	// Whether this machine is showing this dimension to its player; sounds made in a dimension are only played where it is.
	void SetViewed(bool bInViewed) { bViewed = bInViewed; }

	// ---- Sound (M8) ----
	// Plays a sound at every player: the server calls this, and each machine (the server too, if it has a player)
	// hands it to the FMcSoundBus, where McAudio listens. Unreliable: a lost footstep is no loss. Position is in block
	// coordinates.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSound(FName Name, FVector_NetQuantize10 Position, float Volume, float Pitch);

	// A jukebox at Position starts its record (bPlay) or has it taken out, which silences it. Reliable: a record is
	// minutes long, and one that never started or never stopped is noticed.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastRecord(FName Name, FVector_NetQuantize10 Position, bool bPlay);

	// A music note rises at Position, in the colour of Note (0 to 24).
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastNote(FVector_NetQuantize10 Position, int32 Note);

	// Every machine showing this dimension: Count particles of the named kind (/particle) at Position, spread by Delta, at Speed.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastParticle(FName Name, FVector_NetQuantize10 Position, FVector_NetQuantize10 Delta, float Speed, int32 Count);

	// Every machine showing this dimension: the block State at Pos was broken (its bits fly; the view's OnBlockBroken).
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBlockBroken(FIntVector Pos, int32 State);
	TFunction<void(const FIntVector& Pos, FMcStateId State)> OnBlockBroken;

	// Every machine showing this dimension: the bell at Pos was rung from its Side (0 north, 1 south, 2 west, 3 east),
	// so it swings away from there (the view's OnBellRung).
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBellRung(FIntVector Pos, uint8 Side);
	TFunction<void(const FIntVector& Pos, uint8 Side)> OnBellRung;

	// Every machine showing this dimension: the spawner at Pos started a new wait (the view's spinning mob).
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSpawnerCycled(FIntVector Pos);
	TFunction<void(const FIntVector& Pos)> OnSpawnerCycled;

	// Shulker box lids, stepped 20 times a second on every machine: the boxes' collision while open, and the pushes
	// of opening lids. Null until the first tick.
	FMcShulkerLids* GetShulkerLids() const { return ShulkerLids.Get(); }
	// Server: how many have each shulker box open now (each tick, the boxes anyone has open); changes go to everyone.
	void SetShulkerOpeners(const TMap<FIntVector, int32>& Openers);
	// Every machine showing this dimension: the box at Pos is now open for Count.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastShulkerOpeners(FIntVector Pos, int32 Count);
	// Server: the boxes open in a chunk, for a player the chunk is going to. Client: a box already open when its chunk
	// came, shown open at once.
	void GetShulkerOpeners(const FIntPoint& Chunk, TArray<FIntVector>& Boxes, TArray<int32>& Openers) const;
	void ShowShulkerOpen(const FIntVector& Pos, int32 Count);

	// ---- Lightning (M8) ----
	// In a thunderstorm each loaded chunk has a 1 in 100,000 chance a tick of a strike (minecraft.wiki: "Lightning"), on
	// a random column of it, on the block above the top one, where the sky is open and rain (not snow) falls. A strike
	// thunders for everyone, cracks nearby, hurts what stands within reach (5) and lights fire round it.
	void Strike(const FVector& Position);
	// Every machine showing this dimension: a bolt came down at Position (block coordinates), its shape from BoltSeed.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastLightning(FVector_NetQuantize10 Position, int32 BoltSeed);
	// Where a machine's view hears of a bolt, to draw it and flash the sky.
	TFunction<void(const FVector& Position, int32 BoltSeed)> OnLightning;

	// ---- Saving (M9) ----
	// -McWorld=<name> picks a world in Saved/Worlds, made if it isn't there yet: its level file gives the seed, time
	// and terrain (the command line's -McSeed, -McFlat and so on only apply to a world being made), its chunks are
	// loaded from the region files before any are generated, and the world is saved as it's played:
	//   - a chunk is written when it unloads (if it changed or was never saved), and all that changed every 6000 ticks
	//     (five minutes), a few a frame;
	//   - everything is written at EndPlay, and waited for.
	// Without the flag nothing is read or written.
	bool HasStorage() const { return Storage.IsValid(); }
	const FString& GetWorldName() const { return WorldName; }
	const FString& GetWorldDir() const { return WorldDir; }
	const FMcLevelData& GetLevel() const { return Level; }
	// Server: /gamerule and /difficulty, kept in level.json. Setting doDaylightCycle stops or starts the day.
	void SetGameRule(const FString& Rule, const FString& Value);
	void SetDifficulty(int32 Difficulty);
	// The rules this level is saved with, as the game reads them.
	FMcGameRules GetGameRules() const { return FMcGameRules::FromLevel(Level.GameRules, Level.Difficulty); }
	// Gives the mobs and the simulation of this world the rules (peaceful, doMobSpawning, doMobLoot, doTileDrops,
	// randomTickSpeed, doWeatherCycle). The game mode gives every dimension the first world's rules.
	void ApplyGameRules(const FMcGameRules& Rules);
	FMcWorldStorage* GetStorage() const { return Storage.Get(); }
	// Saves the level and every loaded chunk that changed, and waits until it's on disk. The game thread stalls for the
	// file writes only; EndPlay does this, and so can a command.
	void SaveWorld();
	// Marks a loaded chunk as changed when something altered it behind the block world's back (a chest's contents).
	void MarkChunkDirty(FIntPoint Chunk);
	// Called at every save, with the world about to be written: where the other things that belong to the world (the
	// players, see AMcGameMode) save themselves.
	FSimpleMulticastDelegate OnSaving;
	// Game ticks between automatic saves.
	static constexpr int32 AutosaveIntervalTicks = 6000;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_SyncedDayTime();
	UFUNCTION()
	void OnRep_SyncedGameTime();

private:
	void StreamChunks();
	// True if a chunk came back from a worker.
	bool AddGeneratedChunks();
	// Places features in pending chunks and moves the finished ones into the world, within a time budget. True if
	// it decorated or finished a chunk.
	bool DecorateChunks();

	// What a worker brings back: a chunk read from the save, or one just generated.
	struct FFinishedChunk
	{
		TUniquePtr<FMcChunk> Chunk;
		bool bFromDisk = false;
		FMcChunkSaveExtras Extras;
	};
	// Puts a finished chunk into the world: its edits, its mobs, and what's needed to know later whether it changed.
	void AddToWorld(FFinishedChunk Finished);
	// Applies the edits made while the chunk wasn't loaded (and, in a world with no save, all of them).
	void ApplyEdits(FMcChunk& Chunk);
	// Queues a loaded chunk to be written, if it changed or was never saved. True if it was queued.
	bool SaveChunk(FIntPoint Pos);
	void BeginAutosave();
	void StartStorage(const FString& Name);

	// Lives as long as the actor, not just until EndPlay: components (the renderer) end play after the
	// actor does and still unbind from it then.
	TUniquePtr<FMcBlockWorld> BlockWorld;
	// The 20 ticks a second: fluids flow and sand falls. Server only; made when the world starts.
	TUniquePtr<FMcSimulation> Simulation;
	TSubclassOf<AActor> EntityProxyClass;
	// Mobs and dropped items. Server only.
	TUniquePtr<FMcEntityHost> Entities;
	double TickRemainder = 0.0;
	TUniquePtr<FMcShulkerLids> ShulkerLids;
	TMap<FIntVector, int32> ShulkerOpeners;
	double LidRemainder = 0.0;
	// Shared with the worker tasks, which may outlive the actor by a moment.
	TSharedPtr<const FMcChunkGenerator, ESPMode::ThreadSafe> Generator;
	int32 SurfaceY = 0;
	FIntPoint SpawnColumn = FIntPoint::ZeroValue;

	// The clock this machine runs: the server's own count, a client's estimate from the last sync.
	double DayTicks = 1000.0;
	double TicksSinceSync = 0.0;
	UPROPERTY(ReplicatedUsing = OnRep_SyncedDayTime)
	int64 SyncedDayTime = 1000;
	double GameTicks = 0.0;
	UPROPERTY(ReplicatedUsing = OnRep_SyncedGameTime)
	int64 SyncedGameTime = 0;
	UPROPERTY(Replicated)
	bool bTimeFrozen = false;
	// The server's weather (EMcWeather), and the levels easing towards it.
	UPROPERTY(Replicated)
	uint8 Weather = 0;
	float RainLevel = 0.0f;
	float ThunderLevel = 0.0f;
	double WeatherRemainder = 0.0;
	bool HasWeather() const;
	// One tick of the storm's chances of a strike on each loaded chunk.
	void TickLightning();
	// Mirrors the simulation's falling blocks as entities (so clients see them) and drops what lands unplaced.
	void SyncFallingBlocks();
	// Tells the simulation how many entities stand on each pressure plate (minecraft.wiki, "Pressure Plate": wooden
	// and weighted plates feel every entity, stone ones only mobs and players).
	void FeedPressurePlates();
	TMap<FIntVector, int32> FedPlates;
	// Entities a piston's pushed blocks sweep along: each job's shapes (cells at the start) move half a block a tick for
	// two ticks and shove whatever they overlap along Dir (reference run pushent).
	struct FPistonPush { TArray<FIntVector> Cells; FIntVector Dir; int32 Step = 0; };
	TArray<FPistonPush> PistonPushes;
	// Simulation falling block id -> entity id.
	TMap<int32, int32> FallingEntities;
	FRandomStream LightningRandom{ 31 };
	// -McStrike=x,z (development): the column struck every three seconds.
	FString DevStrike;
	int32 DevStrikeTicks = 0;

	// Where the players were (and the view distance) when a frame last found nothing to load, unload or finish; while
	// they stay put there is nothing for StreamChunks to do. Empty when something is outstanding.
	TArray<FIntPoint> SettledCentres;
	TArray<int32> SettledRadii;
	int32 SettledViewDistance = 0;
	struct FAnchor
	{
		FIntPoint Chunk = FIntPoint::ZeroValue;
		int32 Radius = 0;
	};
	TMap<int32, FAnchor> Anchors;
	bool bViewed = true;
	bool bFixingPortals = false;
	int32 NextAnchorId = 1;
	bool IsPlayerHere(const AController& Controller) const { return !PlayerFilter || PlayerFilter(Controller); }

	// Chunks out on worker threads, and the finished ones coming back.
	TSet<FIntPoint> InFlight;
	TSharedPtr<TQueue<FFinishedChunk, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Finished;

	// Noise worlds decorate chunks before players get them. A generated chunk waits here until its 8 neighbours
	// are generated too, gets its features, and joins the world once its neighbours have theirs (features reach
	// up to a chunk out, so it's done changing then). Null library for flat worlds, which have no features.
	// A chunk generated next to ones already in the world (loaded from the save, or generated earlier) can't add to
	// them, so features crossing that border are cut; saved chunks keep theirs.
	TSharedPtr<FMcFeatureLibrary> Features;
	TMap<FIntPoint, TUniquePtr<FMcChunk>> Pending;
	TSet<FIntPoint> Decorated;

	// Block changes by chunk, keyed by world position.
	TMap<FIntPoint, TMap<FIntVector, FMcStateId>> Edits;

	// ---- Saving ----
	FString WorldName;
	FString WorldDir;
	FMcLevelData Level;
	// Shared with the worker tasks that read chunks from it, like the generator.
	TSharedPtr<FMcWorldStorage, ESPMode::ThreadSafe> Storage;
	// The rest of an imported Java world coming in, while its chunks are still being converted.
	TSharedPtr<McJavaImport::FLiveImport, ESPMode::ThreadSafe> LiveImport;
	// What a loaded chunk's save looks like, to tell whether it needs writing again.
	struct FChunkState
	{
		// The chunk's revision when it was loaded or last written.
		uint32 SavedRevision = 0;
		// Whether the chunk has been written (or came from the save) at all.
		bool bOnDisk = false;
		// Whether what it was saved with included mobs or items, so a chunk that has lost them is written again.
		bool bHadEntities = false;
		bool bDirty = false;
	};
	TMap<FIntPoint, FChunkState> ChunkStates;
	// Chunks still to be looked at by the autosave in progress.
	TArray<FIntPoint> AutosaveQueue;
	int32 TicksSinceAutosave = 0;
	// Chunks that joined the world this session, by where they came from; logged with each save.
	int32 NumChunksFromDisk = 0;
	int32 NumChunksGenerated = 0;

public:
	int32 GetNumChunksFromDisk() const { return NumChunksFromDisk; }
	int32 GetNumChunksGenerated() const { return NumChunksGenerated; }
};
