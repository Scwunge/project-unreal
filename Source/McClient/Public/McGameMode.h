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

private:
	FTransform GetSpawnTransform() const;

	// Development: -McCam=x,y,z,yaw,pitch puts the camera at a Minecraft position and angle (the screenshot
	// itself, -McShot, is taken by the player controller).
	void StartDevCapture();
	// Development: -McScene=<name> places tools/scenes/<name>.json and returns its camera.
	void LoadDevScene(const FString& Name, FString& OutCamera);

	UPROPERTY(Transient)
	TObjectPtr<AMcWorldActor> WorldActor;
};
