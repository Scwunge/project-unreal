// The game mode (server only): one flat overworld and walking players. What each player sees is set up by
// their AMcPlayerController on their own machine.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "McGameRules.h"
#include "McGameMode.generated.h"

struct FMcPlayerData;
class AMcPlayerPawn;
class AMcWorldActor;

UCLASS()
class AMcGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMcGameMode();

	// Blocks above the surface that players start at.
	UPROPERTY(EditAnywhere, Category = "Mc")
	int32 SpawnHeightAboveSurface = 10;

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	// On a dedicated server: turns away players the allowlist doesn't have (when it is on) or who would be one too many,
	// unless they are operators (see FMcServerAccess).
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	// A leaving player is saved first (in a saved world, see AMcWorldActor).
	virtual void Logout(AController* Exiting) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	// Saves where a player is and what they have, if the world has a save. Called as they leave, while their pawn is
	// still there (AMcPlayerController::PawnLeavingGame): by Logout the engine has taken it away.
	void SavePlayer(const AController* Controller) const;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// A player who died (their things are already on the ground) starts again at the spawn, with a new body; done on the
	// next tick, as the old pawn is still ticking when it calls this.
	void RespawnPlayer(AController* Controller);

	// ---- Chat ----
	// A line a player typed: said to everyone, or run as a command (McCommands) with their permission level.
	void HandleChat(APlayerController* From, const FString& Text);
	// A line for every player (and the server log). Rgb is the colour, white by default.
	void BroadcastChat(const FString& Text, uint32 Rgb = 0xFFFFFF);
	// The player of that name (as they joined, ignoring case), or null.
	APlayerController* FindPlayer(const FString& Name) const;
	static FString GetPlayerName(const AController* Controller);

	// ---- Dimensions ----
	// The first world: the Overworld unless the world was saved in (or started with -McDimension in) another.
	AMcWorldActor* GetPrimaryWorld() const { return WorldActor; }
	// The world for a dimension id (e.g. "minecraft:the_nether"), null if it hasn't been made.
	AMcWorldActor* FindWorld(const FString& Dimension) const;
	// The world for a dimension, made on the spot if it hasn't been (with the first world's seed and save).
	AMcWorldActor* GetOrCreateWorld(const FString& Dimension);
	// Where new and returning players start, in the first world.
	FTransform GetSpawnTransform() const;
	// What a player left behind in a world with a save: the dimension, place and look they were last in. False if the world
	// has nothing for them.
	bool LoadSavedPlayer(const AController* Controller, FMcPlayerData& OutSaved) const;

	// The first world's saved rules and difficulty, or the defaults before there is a world.
	FMcGameRules GetGameRules() const;
	// Gives every world and every player the first world's rules; called when they change, a world is set up or a
	// player joins or respawns.
	void ApplyGameRules();

private:
	// Where a player starts: where a saved world last had them, else the spawn. bOutFlying says whether they were
	// flying when saved.
	FTransform GetStartTransform(const AController* Controller, bool& bOutFlying) const;
	// What a player is filed under in the world's playerdata folder.
	FString GetSaveId(const AController* Controller) const;
	// Puts a saved inventory and stats (if the world has them for this player) on a pawn that has just arrived; false
	// if there were none.
	bool RestoreSurvival(const AController* Controller, AMcPlayerPawn& Pawn) const;
	// A new player (or one back from the dead) starts in the world's game mode, or the dedicated server's.
	void ApplyDefaultGameMode(AMcPlayerPawn& Pawn) const;
	// A mob's bite or a blast at this block position (feet): hurts the nearest player within reach of it.
	void HurtPlayerNear(const AMcWorldActor& World, const FVector& Position, float Damage);
	void BlastPlayerNear(const AMcWorldActor& World, const FVector& Position, float Damage, const FVector& Push, const FVector& Center);
	// An awake conduit at Middle: Conduit Power for the players in World within Range who are in water or rain.
	void GiveConduitPower(const AMcWorldActor& World, const FVector& Middle, int32 Range);
	// Gives the player at the position a status effect (a witch's splash potion).
	void EffectPlayerNear(const AMcWorldActor& World, const FVector& Position, FName Effect, int32 Amplifier, int32 Duration);
	// Once a second: a player with Bad Omen in a village calls a raid on it (the omen turns into Raid Omen, 30 seconds
	// before the first wave), and the players near a raid are shown its bar.
	void TickVillages();
	FTimerHandle VillageTimer;
	// A raid on a village was won: the players round it become Heroes of the Village.
	void HonourHeroes(const AMcWorldActor& World, const FVector& Center, int32 Level);
	// Everything a world needs to be played in: its mobs show, hurt and sound, and its saves take the players with them.
	void SetUpWorld(AMcWorldActor& World);
	// Every player with a pawn; called whenever the world saves.
	void SavePlayers() const;

	FDelegateHandle SavingHandle;
	FDelegateHandle PreLoadMapHandle;

	// Development: -McCam=x,y,z,yaw,pitch puts the camera at a Minecraft position and angle (the screenshot
	// itself, -McShot, is taken by the player controller).
	void StartDevCapture();
	// Development: -McScene=<name> places tools/scenes/<name>.json and returns its camera.
	void LoadDevScene(const FString& Name, FString& OutCamera);

	UPROPERTY(Transient)
	TObjectPtr<AMcWorldActor> WorldActor;
	// Every world, the first included, once there is more than one.
	UPROPERTY(Transient)
	TArray<TObjectPtr<AMcWorldActor>> Worlds;
};
