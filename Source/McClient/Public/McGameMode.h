// The game mode (server only): one flat overworld and walking players. What each player sees is set up by
// their AMcPlayerController on their own machine.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "McGameMode.generated.h"

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
	// A leaving player is saved first (in a saved world, see AMcWorldActor).
	virtual void Logout(AController* Exiting) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FTransform GetSpawnTransform() const;
	// Where a player starts: where a saved world last had them, else the spawn. bOutFlying says whether they were
	// flying when saved.
	FTransform GetStartTransform(const AController* Controller, bool& bOutFlying) const;
	// What a player is filed under in the world's playerdata folder.
	FString GetSaveId(const AController* Controller) const;
	void SavePlayer(const AController* Controller) const;
	// Every player with a pawn; called whenever the world saves.
	void SavePlayers() const;

	FDelegateHandle SavingHandle;

	// Development: -McCam=x,y,z,yaw,pitch puts the camera at a Minecraft position and angle (the screenshot
	// itself, -McShot, is taken by the player controller).
	void StartDevCapture();
	// Development: -McScene=<name> places tools/scenes/<name>.json and returns its camera.
	void LoadDevScene(const FString& Name, FString& OutCamera);

	UPROPERTY(Transient)
	TObjectPtr<AMcWorldActor> WorldActor;
};
